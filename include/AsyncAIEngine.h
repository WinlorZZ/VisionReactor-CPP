#pragma once
#include <memory>
#include <string>
#include "ThreadPool.h"
#include <thread>
#include <unordered_map>
#include <mutex>         
#include <chrono>     
#include "LatencyProfiler.h"
// 引入 gRPC 核心库和生成的契约头文件
#include <grpcpp/grpcpp.h>
#include "game_ai.pb.h"
#include "game_ai.grpc.pb.h"

// using grpc::Channel;//使用该命名空间存在冲突
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Status;

using vision::VisionAI;
using vision::FrameRequest;
using vision::FrameResponse;

class AsyncAIEngine{
public:
    AsyncAIEngine(std::shared_ptr<grpc::Channel> gchannel,ThreadPool* pool);
    ~AsyncAIEngine();
    // 1. 发起异步请求 (在worker线程中运行)
    void AnalyzeFrameAsync(FrameContextPtr ctx,std::string&& image_date);
    void PrintLatencyLog(const FrameContextPtr& ctx);
    
private:

    // 2. 后台监听信箱 ,用死循环保持持续监听，独立线程
    void AsyncCompleteRpc();

    // 将一次调用的所有状态打包成一个结构体
    struct AsyncClientCall {
        FrameResponse reply; // 准备用来装 Python 返回结果的空容器
        ClientContext context; // gRPC 的网络控制信封
        Status status;  // 准备用来装成功/失败状态的变量
        // 运单号追踪器
        std::unique_ptr<ClientAsyncResponseReader<FrameResponse>> response_reader;
        // 记录任务的时间信息
        FrameContextPtr ctx; 
    };

    std::unique_ptr<VisionAI::Stub> stub_;// 存根
    CompletionQueue cq_; // 信箱
    std::thread cq_thread;// 运行cq的线程
    ThreadPool* threadpool;// 线程池指针
    // 管理tag
    std::mutex mu_;// 保护 active_calls_
    std::unordered_map<void*, std::shared_ptr<AsyncClientCall>> active_calls_;
    // 使用uormap存call，这样share_ptr的计数始终有1，使得call不会被释放
};