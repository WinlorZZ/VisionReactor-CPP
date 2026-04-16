import time
import grpc
import cv2
import numpy as np
from concurrent import futures
from ultralytics import YOLO

import game_ai_pb2
import game_ai_pb2_grpc

class VisionAIServicer(game_ai_pb2_grpc.VisionAIServicer):
    def __init__(self):
        print("[AI Engine] Loading YOLO model...")
        self.model = YOLO('yolov8n.pt') 
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