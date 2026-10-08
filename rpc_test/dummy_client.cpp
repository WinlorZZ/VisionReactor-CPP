#include <iostream>
#include <memory>
#include <string>

// 引入 gRPC 核心库
#include <grpcpp/grpcpp.h>

// 引入用 protoc 生成的契约头文件
#include "../proto/game_ai.grpc.pb.h" 

// 引入命名空间，让代码更干净
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using game_ai::VisionAI;
using game_ai::FrameRequest;
using game_ai::FrameResponse;

// 封装一个 C++ 客户端类
class VisionAIClient {
public:
    // 构造函数：传入一个建立好的网络通道 (Channel) 并生成对应的 Stub (存根)
    VisionAIClient(std::shared_ptr<Channel> channel)
        : stub_(VisionAI::NewStub(channel)) {}

    // 发起分析请求的函数
    void AnalyzeFrame(uint64_t frame_id) {
        // 1. 准备 Request (按照 .proto 里的定义填入数据)
        FrameRequest request;
        request.set_frame_id(frame_id);
        request.set_timestamp_ms(123456789);
        request.set_image_data("fake_binary_image_data_here"); // 暂时传个假字符串假装是图片

        // 2. 准备接收 Response 的容器和上下文
        FrameResponse response;
        ClientContext context;

        std::cout << "[*] Sending request for Frame ID: " << frame_id << " ...\n";

        // 3. 发起同步 RPC 调用
        // 注意：在这个测试阶段，这行代码会阻塞，直到 Python 返回结果
        // 等我们合入你的 Reactor 线程池时，我们会把它改造成全异步的
        Status status = stub_->AnalyzeFrame(&context, request, &response);

        // 4. 处理返回结果
        if (status.ok()) {
            std::cout << "[+] RPC Success! Received Python AI Judgment:\n";
            std::cout << "    -> Frame ID: " << response.frame_id() << "\n";
            std::cout << "    -> P1 (HP): " << response.p1_state().hp_percent() << "%"
                      << " | X_Pos: " << response.p1_state().x() << "\n";
            std::cout << "    -> P2 (HP): " << response.p2_state().hp_percent() << "%"
                      << " | X_Pos: " << response.p2_state().x() << "\n";
            std::cout << "    -> Python Inference Latency: " << response.inference_latency_ms() << " ms\n";
        } else {
            std::cout << "[-] RPC Failed. Error Code: " << status.error_code() 
                      << ": " << status.error_message() << std::endl;
        }
    }

private:
    std::unique_ptr<VisionAI::Stub> stub_;
};

int main(int argc, char** argv) {
    std::cout << "=== C++ Vision AI Dummy Client Initializing ===\n";
    
    // 连接到本机的 50051 端口 (Python 服务端所在端口)
    // InsecureChannelCredentials 表示目前不使用 TLS/SSL 加密，纯内网裸奔最快
    VisionAIClient client(grpc::CreateChannel(
        "localhost:50051", grpc::InsecureChannelCredentials()));
    
    // 模拟发送第 1024 帧给 AI 分析
    client.AnalyzeFrame(1024);

    return 0;
}