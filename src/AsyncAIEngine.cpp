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
}

// 发起异步请求 (在线程中运行)
void AsyncAIEngine::AnalyzeFrameAsync(uint64_t frame_id,std::string&& image_date){
    vision::FrameRequest request;
    request.set_frame_id(frame_id);// 帧id
    request.set_timestamp_ms(98874);// 时间戳
    request.set_image_data(std::move(image_date));// 图片数据
    
    AsyncClientCall* call = new AsyncClientCall;
    call->response_reader = stub_->PrepareAsyncAnalyzeFrame(&call->context, request, &cq_);
    call->response_reader->StartCall();
    call->response_reader->Finish(&call->reply, &call->status, (void*)call);
}

// 后台监听信箱 ,用死循环保持持续监听，独立线程
// 防止外部调用该函数导致死循环，保护线程安全
void AsyncAIEngine::AsyncCompleteRpc(){
    void* got_tag;// 万能指针，仅代表内存地址，指定返回的信息存放位置
    bool ok = false;
    
    while(cq_.Next(&got_tag, &ok)){
        // 将got_tag恢复成AsyncClientCall地址
        AsyncClientCall* call = static_cast<AsyncClientCall*>(got_tag);
        if( call->status.ok() && ok ){
            // 把处理结果打包成一个新的任务，扔回 ThreadPool
            // 复制出需要的数据，防止 call 被 delete 后指针失效
            // 深拷贝，防止指针逃逸
            vision::FrameResponse reply_copy = call->reply;
            
            threadpool->add([reply_copy]() {
                std::cout << "[Worker Thread] [+] 拿到 AI 回调结果！Frame ID: " 
                          << reply_copy.frame_id() 
                          << " | 推理耗时: " 
                          << reply_copy.inference_latency_ms()
                          << "ms\n"
                          << "                -> 正在通过 TCP 将结果发回游戏客户端...\n";
                // TODO: 未来在这里执行网络发包回客户端，或者异步 MySQL 落库
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
        delete call;
    }
}