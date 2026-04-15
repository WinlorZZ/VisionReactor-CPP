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
void AsyncAIEngine::AnalyzeFrameAsync(uint64_t frame_id,std::string&& image_date){
    vision::FrameRequest request;
    request.set_frame_id(frame_id);// 帧id
    request.set_timestamp_ms(98874);// 时间戳
    request.set_image_data(std::move(image_date));// 图片数据
    
    // AsyncClientCall* call = new AsyncClientCall;
    // 使用智能指针
    auto call = std::make_shared<AsyncClientCall>();
    call->frame_id = frame_id;
    call->create_time = std::chrono::steady_clock::now();

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
            // 复制出需要的数据，防止 call 被 delete 后指针失效
            // 深拷贝，防止指针逃逸
            
            //计算生命周期耗时
            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - call->create_time).count();

            vision::FrameResponse reply_copy = call->reply;
            
            threadpool->add([reply_copy, duration]() {
                std::cout << "[Worker Thread] [+] 拿到 AI 回调结果！Frame ID: " 
                          << reply_copy.frame_id() 
                          << " | 推理耗时: " 
                          << reply_copy.inference_latency_ms()
                          << "ms\n"
                          << " | C++端测量的全链路耗时: " << duration << "ms\n"
                          << "                -> 正在通过 TCP 将结果发回客户端...\n";
                
                for(int i = 0;i< reply_copy.boxes_size(); i++){
                    const auto&box = reply_copy.boxes(i);
                    std::cout << "                -> 锁定目标: [" << box.class_name() << "] "
                              << "置信度: " << box.confidence()
                              << " | 中心点: (" << box.x() << ", " << box.y() << ")"
                              << " | 宽高: " << box.width() << "x" << box.height() << "\n";
                }
            });
        }else {
            std::cout << "[CQ Thread] [-] 丢包或 AI 服务崩溃！ RPC 状态码: " 
                      << call->status.error_code() 
                      << " | 错误信息: " << call->status.error_message() << "\n";
        }
        // delete call;
    }
}