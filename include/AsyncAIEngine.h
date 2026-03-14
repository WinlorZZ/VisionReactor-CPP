#pragma once

#include <memory>
#include <string>
#include <thread>
#include <grpcpp/grpcpp.h>
#include "../proto/game_ai.grpc.pb.h"
#include "ThreadPool.h"

// 引入 gRPC 核心库和生成的契约头文件
#include <grpcpp/grpcpp.h>
#include "../proto/game_ai.grpc.pb.h"

// using grpc::Channel;//使用该命名空间存在冲突
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Status;
using game_ai::VisionAI;
using game_ai::FrameRequest;
using game_ai::FrameResponse;

class AsyncAIEngine{
public:
    AsyncAIEngine(std::shared_ptr<grpc::Channel> gchannel,ThreadPool* pool);
    ~AsyncAIEngine();
    // 1. 发起异步请求 (在worker线程中运行)
    void AnalyzeFrameAsync(uint64_t frame_id,std::string&& image_date);

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
    };

    std::unique_ptr<VisionAI::Stub> stub_;// 存根
    CompletionQueue cq_; // 信箱
    std::thread cq_thread;// 运行cq的线程
    ThreadPool* threadpool;// 线程池指针

};