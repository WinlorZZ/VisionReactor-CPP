import grpc
import time
from concurrent import futures

# 导入刚才 protoc 生成的契约文件
import game_ai_pb2
import game_ai_pb2_grpc

# 继承生成的 Servicer 基类，实现我们定义的 RPC 接口
class VisionAIServicer(game_ai_pb2_grpc.VisionAIServicer):
    
    # 这个函数名和参数，必须和 .proto 文件里定义的完全一致
    def AnalyzeFrame(self, request, context):
        # 1. 打印收到的请求日志，证明网络打通
        print(f"[*] Received Request -> Frame ID: {request.frame_id}, Timestamp: {request.timestamp_ms}")
        print(f"[*] Image Payload Size: {len(request.image_data)} bytes")

        # 2. 模拟 YOLO 模型前向传播的推理耗时 (比如 50 毫秒)
        # 在单体架构中，这 50ms 会卡死整个网络循环，但在我们的微服务中，C++ 是完全无感的
        time.sleep(0.05)

        # 3. 构造伪造的判定结果 (Dummy Data)
        # 假装识别出了 1P (左边，优势) 和 2P (右边，劣势)
        p1 = game_ai_pb2.PlayerState(x=100.0, y=200.0, width=50.0, height=120.0, hp_percent=90, drive_gauge=6)
        p2 = game_ai_pb2.PlayerState(x=300.0, y=200.0, width=50.0, height=120.0, hp_percent=30, drive_gauge=2)

        # 4. 组装 Response 契约并返回
        response = game_ai_pb2.FrameResponse(
            frame_id=request.frame_id,  # 必须原样奉还，否则 C++ 异步回调会错乱
            p1_state=p1,
            p2_state=p2,
            inference_latency_ms=50
        )

        print(f"[+] Successfully responded to Frame ID: {request.frame_id}\n")
        return response

def serve():
    # 1. 配置 gRPC 服务器的线程池 (这里给 4 个 Worker 线程，模拟并发处理能力)
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=4))
    
    # 2. 将我们的处理逻辑挂载到服务器底座上
    game_ai_pb2_grpc.add_VisionAIServicer_to_server(VisionAIServicer(), server)
    
    # 3. 绑定监听端口 (50051 是 gRPC 默认的经典测试端口)
    server.add_insecure_port('[::]:50051')
    
    print("🚀 Vision AI Dummy Server is running on port 50051...")
    
    # 4. 启动并阻塞主线程，保持服务器常驻
    server.start()
    server.wait_for_termination()

if __name__ == '__main__':
    serve()