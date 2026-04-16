#include <iostream>
#include "AsyncAIEngine.h"


//传入gRPC通道和线程池指针ThreadPool*
AsyncAIEngine::AsyncAIEngine(std::shared_ptr<grpc::Channel> gchannel,
    ThreadPool* pool) : 
    stub_(vision::VisionAI::NewStub(gchannel)), 
    threadpool(pool){
    cq_thread = std::thread(&AsyncAIEngine::AsyncCompleteRpc,this);
}

//处理CQ退出和线程Jion
AsyncAIEngine::~AsyncAIEngine(){
    std::cout << "[AI Engine] 正在切断 AI 服务连接...\n";
    cq_.Shutdown();// 关闭信箱，停止Next等待
    cq_thread.join(); // 防止野指针
    {// 清空 Map
        std::lock_guard<std::mutex> lock(mu_);
        active_calls_.clear(); 
    }
}

// 发起异步请求 (在worker线程中运行)
void AsyncAIEngine::AnalyzeFrameAsync(FrameContextPtr ctx,std::string&& image_date){
    vision::FrameRequest request;
    request.set_frame_id(ctx->trace_id);// 帧id
    // request.set_timestamp_ms(98874);// 时间戳
    request.set_image_data(std::move(image_date));// 图片数据
    
    // AsyncClientCall* call = new AsyncClientCall;
    // 使用智能指针
    auto call = std::make_shared<AsyncClientCall>();
    call->ctx = ctx;
    call->ctx->t_grpc_sent = LatencyProfiler::now();// 替代下一行的创建时间
    // call->create_time = std::chrono::steady_clock::now();

    call->response_reader = stub_->PrepareAsyncAnalyzeFrame(&call->context, request, &cq_);
    call->response_reader->StartCall();

    //注册tag，由cq线程监视
    void* tag = (void*)call.get(); 
    call->response_reader->Finish(&call->reply, &call->status, tag);
    // call->response_reader->Finish(&call->reply, &call->status, (void*)call);
    {// 上锁，把 shared_ptr 加入 Map 
        std::lock_guard<std::mutex> lock(mu_);
        active_calls_[tag] = call;
    }
}

// 后台监听信箱 ,用死循环保持持续监听，独立线程
// 防止外部调用该函数导致死循环，保护线程安全
void AsyncAIEngine::AsyncCompleteRpc(){
    void* got_tag;// 万能指针，仅代表内存地址，指定返回的信息存放位置
    bool ok = false;
    
    while(cq_.Next(&got_tag, &ok)){
        // 将got_tag恢复成AsyncClientCall地址
        // AsyncClientCall* call = static_cast<AsyncClientCall*>(got_tag);
        std::shared_ptr<AsyncClientCall> call;
        {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = active_calls_.find(got_tag);
            if(it != active_calls_.end() ){
                call = it->second;
                // 从 Map 移除（引用计数变回 1，目前只有局部变量 call 持有）
                active_calls_.erase(it);
            }else{
                std::cerr << "[CQ Thread] 严重警告：收到未知 Tag，可能发生了幽灵回调！\n";
                continue;
            }
        }

        if( call->status.ok() && ok ){
            // 把处理结果打包成一个新的任务，扔回 ThreadPool
            
            // 时间信息更新
            call->ctx->t3_python_cost_us = call->reply.inference_latency_us();
            call->ctx->t_grpc_recv = LatencyProfiler::now();

            // auto now = std::chrono::steady_clock::now();
            // auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - call->create_time).count();

            // 复制出需要的数据，防止 call 被 delete 后指针失效
            // 深拷贝，防止指针逃逸
            vision::FrameResponse reply_copy = call->reply;
            FrameContextPtr ctx_copy = call->ctx;

            threadpool->add([reply_copy, ctx_copy, this]() {
                std::cout << "[Worker Thread] [+] 拿到 AI 回调结果！Frame ID: " 
                          << reply_copy.frame_id() 
                          << " | 推理耗时: " << reply_copy.inference_latency_us() / 1000.0 << " ms\n"
                          << "                -> 正在准备将结果发回客户端...\n";
                
                for(int i = 0; i < reply_copy.boxes_size(); i++){
                    const auto& box = reply_copy.boxes(i);
                    std::cout << "                -> 锁定目标: [" << box.class_name() << "] "
                              << "置信度: " << box.confidence()
                              << " | 中心点: (" << box.x() << ", " << box.y() << ")"
                              << " | 宽高: " << box.width() << "x" << box.height() << "\n";
                }

                // ==========================================
                // 【全链路闭环】：此处模拟将数据丢给 Connection 发送
                // ==========================================
                // 假设这里调用了 connection->send(最终的序列化数据);
                
                // 【探针注入：T4 终点】准备发回客户端前的一瞬间
                ctx_copy->t_finish = LatencyProfiler::now();

                // 打印终极性能 CT 报告！
                this->PrintLatencyLog(ctx_copy);
            });
        }else {
            std::cout << "[CQ Thread] [-] 丢包或 AI 服务崩溃！ RPC 状态码: " 
                      << call->status.error_code() 
                      << " | 错误信息: " << call->status.error_message() << "\n";
        }
        // delete call;
    }
}


void AsyncAIEngine::PrintLatencyLog(const FrameContextPtr& ctx) {
    // 1. 计算各个生命周期分段 (微秒)
    int64_t t1_us = LatencyProfiler::microseconds_between(ctx->t_start, ctx->t_parsed);
    int64_t t2_us = LatencyProfiler::microseconds_between(ctx->t_parsed, ctx->t_grpc_sent);
    int64_t t4_us = LatencyProfiler::microseconds_between(ctx->t_grpc_recv, ctx->t_finish);
    
    // Python 耗时直接来自回执
    int64_t t3_us = ctx->t3_python_cost_us; 

    // 2. 计算隐藏开销：跨语言 gRPC 通讯耗时
    // 发送与接收之间的总差值，减去 Python 纯算力开销，剩下的就是通讯序列化与网络开销
    int64_t total_grpc_round_trip = LatencyProfiler::microseconds_between(ctx->t_grpc_sent, ctx->t_grpc_recv);
    int64_t grpc_ipc_cost = total_grpc_round_trip - t3_us;

    // 3. 计算全链路总耗时
    int64_t total_us = LatencyProfiler::microseconds_between(ctx->t_start, ctx->t_finish);

    // 4. 转换为毫秒用于直观展示 (保留两位小数)
    auto to_ms = [](int64_t us) { return us / 1000.0; };

    printf("[Latency CT Scan] Frame: %lu | Total: %.2f ms\n", ctx->trace_id, to_ms(total_us));
    printf("  ├─ T1 (TCP Parse)   : %6.2f ms\n", to_ms(t1_us));
    printf("  ├─ T2 (C++ -> gRPC) : %6.2f ms\n", to_ms(t2_us));
    printf("  ├─ IPC (gRPC Trans) : %6.2f ms  <-- 跨语言开销\n", to_ms(grpc_ipc_cost));
    printf("  ├─ T3 (Python YOLO) : %6.2f ms  <-- AI 纯算力开销\n", to_ms(t3_us));
    printf("  └─ T4 (gRPC -> C++) : %6.2f ms\n", to_ms(t4_us));
    printf("--------------------------------------------------\n");
}