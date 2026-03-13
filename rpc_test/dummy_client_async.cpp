#include <iostream>
#include <memory>
#include <string>
#include <thread>

// 引入 gRPC 核心库和生成的契约头文件
#include <grpcpp/grpcpp.h>
#include "../proto/game_ai.grpc.pb.h"

using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Status;
using game_ai::VisionAI;
using game_ai::FrameRequest;
using game_ai::FrameResponse;

class VisionAIAsyncClient {
public:
    VisionAIAsyncClient(std::shared_ptr<Channel> channel)
        : stub_(VisionAI::NewStub(channel)) {}

    // 1. 发起异步请求 (在worker线程中运行)
    void AnalyzeFrameAsync(uint64_t frame_id) {
        // 准备请求数据
        FrameRequest request;
        request.set_frame_id(frame_id);
        request.set_timestamp_ms(123456789);
        request.set_image_data("fake_image_data");

        // 将这次请求的上下文放在堆内存上，保证异步返回时它还活着
        AsyncClientCall* call = new AsyncClientCall;
        
        std::cout << "[主线程] 正在发射请求 Frame ID: " << frame_id << "，发射完毕立刻离开！\n";

        // 向 Python 发起调用，但不等待。由把 cq_ 传进去作为接收回执的“信箱”
        call->response_reader = stub_->PrepareAsyncAnalyzeFrame(&call->context, request, &cq_);
        call->response_reader->StartCall();
        
        // 留下一个“小票 (Tag)”。这里直接用 call 对象的内存地址作为唯一凭证
        call->response_reader->Finish(&call->reply, &call->status, (void*)call);
    }

    // 2. 后台监听信箱 ,用死循环保持持续监听，独立线程
    void AsyncCompleteRpc() {
        void* got_tag;// 万能指针，仅代表内存地址，指定返回的信息存放位置
        bool ok = false;

        std::cout << "[CQ 监听线程] 已就位，正在等待 Python AI 的返回结果...\n";

        // 阻塞等待信箱 (CQ) 里的回执。只要算完一帧，这里就会接受并进入循环体；后续所有数据都从这里返回并处理
        while (cq_.Next(&got_tag, &ok)) {
            // 将got_tag恢复成AsyncClientCall地址
            AsyncClientCall* call = static_cast<AsyncClientCall*>(got_tag);

            if (call->status.ok()) {
                std::cout << "[CQ 监听线程] [+] AI 判定结果到达！Frame ID: " 
                          << call->reply.frame_id() 
                          << " | 1P 血量: " << call->reply.p1_state().hp_percent() << "%"
                          << " | 推理耗时: " << call->reply.inference_latency_ms() << "ms\n";
            } else {
                std::cout << "[-] RPC 调用失败。\n";
            }
            // 结果处理完了，清理内存
            delete call;
        }
    }

private:
    // 将一次调用的所有状态打包成一个结构体
    struct AsyncClientCall {
        FrameResponse reply; // 准备用来装 Python 返回结果的空容器
        ClientContext context; // gRPC 的网络控制信封
        Status status;  // 准备用来装成功/失败状态的变量
        std::unique_ptr<ClientAsyncResponseReader<FrameResponse>> response_reader;// 运单号追踪器
    };

    std::unique_ptr<VisionAI::Stub> stub_;
    CompletionQueue cq_; // 信箱
};

int main(int argc, char** argv) {
    std::cout << "=== C++ Vision AI 异步客户端启动 ===\n";
    
    VisionAIAsyncClient client(grpc::CreateChannel(
        "localhost:50051", grpc::InsecureChannelCredentials()));

    // 启动一个独立的后台线程，专门盯着 CQ 信箱
    // thread(&函数,&操作对象)
    std::thread thread_(&VisionAIAsyncClient::AsyncCompleteRpc, &client);

    // 模拟 Reactor 的 Worker 线程，瞬间连发 3 个帧画面请求！
    // 注意看终端输出：这 3 个请求会瞬间发完，根本不需要等 Python 返回！
    for (int i = 0; i < 3; i++) {
        client.AnalyzeFrameAsync(1000 + i);
    }

    // 挂起主线程，防止程序直接退出
    std::cout << "[主线程] 任务全部分发完毕，按 Ctrl+C 退出程序...\n";
    // 调用thread.join(),阻塞当前函数，等待thread_线程结束
    thread_.join(); 

    return 0;
}