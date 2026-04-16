import os
import sys
from grpc_tools import protoc

# ==========================================
# 1. 契约文件 JIT 动态编译阶段 (Zero-Setup)
# ==========================================
# 获取当前 Pserver.py 所在目录 (python_ai)
current_dir = os.path.dirname(os.path.abspath(__file__))
# 获取项目根目录 (跨出 python_ai 文件夹)
project_root = os.path.dirname(current_dir)
# 定位到对面的 proto 文件夹
proto_dir = os.path.join(project_root, "proto")
proto_file = os.path.join(proto_dir, "game_ai.proto")

print(f"[Proto JIT] 检查契约文件: {proto_file}")
if os.path.exists(proto_file):
    print("[Proto JIT] 正在动态编译 gRPC 契约...")
    # 通过 Python 代码直接调用 protoc 编译器
    protoc.main((
        '',
        f'-I{proto_dir}',
        f'--python_out={current_dir}',
        f'--grpc_python_out={current_dir}',
        proto_file,
    ))
    print("[Proto JIT] 契约编译完毕！")
else:
    print("[-] 致命错误：找不到 game_ai.proto 文件！")
    sys.exit(1)

# ==========================================
# 2. 正常导入阶段 (此时 _pb2 文件必定已最新生成)
# ==========================================

import time
import grpc
import cv2
import numpy as np
from concurrent import futures
import torch
from ultralytics import YOLO
import game_ai_pb2
import game_ai_pb2_grpc

class VisionAIServicer(game_ai_pb2_grpc.VisionAIServicer):
    def __init__(self):
        print("===================================")
        print(f"[GPU Check] PyTorch CUDA 可用状态: {torch.cuda.is_available()}")
        if torch.cuda.is_available():
            print(f"[GPU Check] 识别到显卡: {torch.cuda.get_device_name(0)}")
        else:
            print("[GPU Check] 警告：未能识别到 CUDA，即将使用 CPU 进行推理！")
        print("===================================")

        print("[AI Engine] Loading YOLO model...")
        # 强制指定在第一块 GPU (cuda:0) 上加载模型
        # 如果你想保险，可以写 self.model = YOLO('yolov8n.pt') 
        # YOLOv8 默认会自动找 GPU，但我们可以强行提醒它
        self.model = YOLO('yolov8n.pt')
        self.model.to('cuda' if torch.cuda.is_available() else 'cpu')
        print("[AI Engine] Model loaded successfully.")

    def AnalyzeFrame(self, request, context):
        # 【T3 探针起点】：使用高精度单调时钟
        t3_start = time.perf_counter()
        
        print(f"[AI Engine] 收到 Frame: {request.frame_id}, Payload 大小: {len(request.image_data)} bytes")
        
        nparr = np.frombuffer(request.image_data, np.uint8)
        img_np = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

        if img_np is None:
            context.set_code(grpc.StatusCode.INVALID_ARGUMENT)
            context.set_details("Failed to decode image bytes.")
            return game_ai_pb2.FrameResponse()

        # OpenCV 解码与 YOLO 推理
        results = self.model(img_np, imgsz=640, verbose=False)

        response = game_ai_pb2.FrameResponse()
        response.frame_id = request.frame_id

        for r in results:
            boxes = r.boxes
            for box in boxes:
                x, y, w, h = box.xywh[0].tolist()
                conf = box.conf[0].item()
                cls_id = int(box.cls[0].item())
                
                bbox_msg = response.boxes.add()
                bbox_msg.x = x
                bbox_msg.y = y
                bbox_msg.width = w
                bbox_msg.height = h
                bbox_msg.confidence = conf
                bbox_msg.class_name = self.model.names[cls_id]

        # 【T3 探针终点】
        t3_end = time.perf_counter()
        
        # 计算微秒 (us)
        cost_us = int((t3_end - t3_start) * 1_000_000)
        
        # 【对齐契约】：字段名已升级为 us
        response.inference_latency_us = cost_us
        
        # 日志依然可以打印毫秒方便人类阅读
        print(f"Frame {request.frame_id} processed in {cost_us / 1000.0:.2f}ms. Found {len(response.boxes)} objects.")
        
        return response

def serve():
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=4))
    game_ai_pb2_grpc.add_VisionAIServicer_to_server(VisionAIServicer(), server)
    
    server.add_insecure_port('[::]:50051')
    server.start()
    print("[AI Engine] gRPC Server is running on port 50051...")
    server.wait_for_termination()

if __name__ == '__main__':
    serve()