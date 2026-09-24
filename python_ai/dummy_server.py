import os
import sys
import time
import argparse
from concurrent import futures

# ==========================================
# 1. 契约文件 JIT 动态编译 (与 Pserver.py 同一套流程)
# ==========================================
# 目的：本文件不依赖 torch / opencv，只依赖 grpcio，用于在没有 GPU 或
# 未安装推理依赖的机器上联调 C++ 网关的异步链路与延迟探针。
current_dir = os.path.dirname(os.path.abspath(__file__))
project_root = os.path.dirname(current_dir)
proto_dir = os.path.join(project_root, "proto")
proto_file = os.path.join(proto_dir, "game_ai.proto")

if not os.path.exists(proto_file):
    print(f"[-] 致命错误：找不到契约文件 {proto_file}")
    sys.exit(1)

try:
    from grpc_tools import protoc
except ImportError:
    print("[-] 缺少依赖：请先执行 pip install grpcio grpcio-tools")
    sys.exit(1)

print(f"[Proto JIT] 正在编译契约: {proto_file}")
rc = protoc.main((
    '',
    f'-I{proto_dir}',
    f'--python_out={current_dir}',
    f'--grpc_python_out={current_dir}',
    proto_file,
))
if rc != 0:
    print(f"[-] 契约编译失败，protoc 返回 {rc}")
    sys.exit(1)
print("[Proto JIT] 契约编译完毕")

# ==========================================
# 2. 导入与服务实现
# ==========================================
import grpc                      # noqa: E402
import game_ai_pb2               # noqa: E402
import game_ai_pb2_grpc          # noqa: E402

# 每个假检测框：(中心x, 中心y, 宽, 高, 类别, 置信度)
FAKE_BOXES = [
    (640.0, 420.0, 180.0, 420.0, "person", 0.91),
    (300.0, 380.0, 120.0, 260.0, "person", 0.78),
]


class VisionAIServicer(game_ai_pb2_grpc.VisionAIServicer):
    def __init__(self, latency_ms: float, boxes: int):
        self.latency_ms = latency_ms
        self.boxes = boxes

    def AnalyzeFrame(self, request, context):
        t3_start = time.perf_counter()

        # 用可控 sleep 模拟推理耗时，便于观察 C++ 侧 T3/gRPC 分段延迟
        time.sleep(self.latency_ms / 1000.0)

        response = game_ai_pb2.FrameResponse()
        response.frame_id = request.frame_id          # 必须原样回传，异步回调靠它对齐
        for x, y, w, h, name, conf in FAKE_BOXES[: self.boxes]:
            bbox = response.boxes.add()
            bbox.x = x
            bbox.y = y
            bbox.width = w
            bbox.height = h
            bbox.class_name = name
            bbox.confidence = conf

        cost_us = int((time.perf_counter() - t3_start) * 1_000_000)
        response.inference_latency_us = cost_us

        print(f"[Dummy AI] frame={request.frame_id} payload={len(request.image_data)}B "
              f"boxes={len(response.boxes)} cost={cost_us / 1000.0:.2f}ms")
        return response


def serve(port: int, latency_ms: float, boxes: int):
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=4))
    game_ai_pb2_grpc.add_VisionAIServicer_to_server(
        VisionAIServicer(latency_ms, boxes), server)
    server.add_insecure_port(f'[::]:{port}')
    server.start()
    print(f"[Dummy AI] gRPC 服务已启动: 0.0.0.0:{port} "
          f"(模拟推理 {latency_ms}ms, 返回 {boxes} 个框)")
    server.wait_for_termination()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="VisionReactor 模拟 AI 节点")
    parser.add_argument('--port', type=int, default=50051)
    parser.add_argument('--latency-ms', type=float, default=20.0,
                        help='模拟单帧推理耗时 (毫秒)')
    parser.add_argument('--boxes', type=int, default=2, choices=[0, 1, 2],
                        help='返回的假检测框数量')
    args = parser.parse_args()
    try:
        serve(args.port, args.latency_ms, args.boxes)
    except KeyboardInterrupt:
        print("\n[Dummy AI] 已退出")
