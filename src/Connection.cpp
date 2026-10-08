#include "Connection.h"
#include "Socket.h"
#include "Channel.h"
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <memory> // shared_from_this
#include "Buffer.h"
#include "AsyncAIEngine.h"
#include "EventLoop.h"
#include "ResponseSerializer.h"

#include <cerrno>
#include <algorithm>
#include <cstdint>

Connection::Connection(EventLoop *loop, Socket *sock) : state_(kConnected), loop(loop), sock(sock) {
    //初始化channel
    channel = new Channel(loop, sock->fd());
    //初始化Buffer
    inputBuffer = new Buffer();
    outputBuffer = new Buffer();

    //绑定读事件
    std::function<void()> readCb = std::bind(&Connection::handleReadEvent, this);
    channel->setReadCallback(readCb);
    //绑定写事件
    std::function<void()> writeCb = std::bind( &Connection::handleWriteEvent,this );
    channel->setWriteCallback(writeCb);
    // channel->enableReading(); //将注册epoll这一步从构造函数中剔除，以便使用shared_ptr
    // 执行构造函数的时候，外层的 std::make_shared 还没有执行完，
    // 当前对象还处于‘半成品’状态，根本没有被 shared_ptr 管理
}

void Connection::connectEstablished(){
    // 此时shared_ptr可以使用了
    channel->tie( shared_from_this() );
    channel->enableReading();
}

Connection::~Connection() {
    delete channel;
    delete sock;
    delete inputBuffer;
    delete outputBuffer;
}

void Connection::setDeleteConnectionCallback(std::function<void(Socket*)> cb) { 
    deleteConnectionCallback = cb; 
}

void Connection::setOnMessageCallback(std::function<void( std::shared_ptr<Connection> )> cb) { 
    onMessageCallback = cb; 
}

// IO 读取 
// 将该函数设置给Connection管理的对应的channel，channel在被调用handleEvent时会使用该函数
void Connection::handleReadEvent() {
    int savedErrno = 0;
    // bool read_something = false;

    // ET 模式：必须用 while 循环读到 EAGAIN 为止
    while (true) {
        ssize_t n = inputBuffer->readFd(sock->fd(), &savedErrno);
        
        if (n > 0) {// 读到数据
            // read_something = true; 
            continue;// 继续下一次调用readFd，调用的时候已经存进inputBuffer了
        } else if (n == -1 && savedErrno == EINTR) {// 被系统中断打断，继续读
            continue; 
        } else if (n == -1 && (savedErrno == EAGAIN || savedErrno == EWOULDBLOCK)) {
            break; // 
        } else if (n == 0) {
            // 对端正常关闭 (FIN包)
            std::cout << "[Connection] 收到 FIN，准备断开..." << std::endl;
            handleClose();
            break; // 已经要断开了，直接跳出循环
        } else {
            // 发生了其他严重错误
            std::cout << "[Connection] 读取异常，强行断开..." << std::endl;
            handleClose();
            break;
        }
    }
    // 拆包和 inputBuffer 的消费固定在 EventLoop 线程，避免与 Worker 数据竞争。
    if (inputBuffer->readableBytes() > 0 && onMessageCallback) {
        onMessageCallback(shared_from_this());
    }
}

void Connection::handleClose() {
    if (state_ == kDisconnected) {
        return;
    }
    state_ = kDisconnecting; // 状态切换：准备断开
    channel->disableReading(); // 不再接收新数据

    // 检查是否有残留的未读数据
    if (outputBuffer->readableBytes() == 0) {
        // 没有则直接通知server回收
        state_ = kDisconnected;
        if (deleteConnectionCallback) deleteConnectionCallback(sock);
    } else {
        std::cout << "[Connection] 触发优雅挥手，发现 Buffer 仍有积压，延迟销毁！" << std::endl;
        // 留着 EPOLLOUT，让 handleWriteEvent 把剩下的数据发完
    }
}

// 原有的 business 方法保留不动，供旧协议路径（4字节长度前缀）使用
void Connection::business(AsyncAIEngine* engine_ptr) {
    // std::cerr << "[Critical Debug] 进入 business 函数成功！" << std::endl;
    if ( inputBuffer->readableBytes() == 0 ) return;// 当读缓冲区为空时返回
    // // 之前的版本，提取消息
    // std::string message(inputBuffer->peek(), inputBuffer->readableBytes());
    // inputBuffer->retrieveAll(); // 取出后立刻移动读游标
    if (!engine_ptr) {
                std::cerr << "[-] 致命错误：engine_ptr 是空指针！" << std::endl;
                return;
    }

    while (inputBuffer->readableBytes() >= 4) {
        // 包头解析
        // 注意：这里位于 T1 计时区间内，热路径上不做任何同步日志输出，
        // 否则日志 flush 会直接计入网关侧延迟。
        uint32_t body_len = inputBuffer->peekInt32();
        if (body_len <= 0 || body_len > kMaxFrameBytes) {
            // std::cerr << "[-] 致命错误：非法的数据包长度 " << body_len << "，强制断开连接！\n";
            handleClose();
            break;
        }
        if (!current_frame_ctx_) {
            current_frame_ctx_ = std::make_shared<FrameContext>();
        }

        if (inputBuffer->readableBytes() >= 4 + body_len) {
            inputBuffer->retrieve(4);// 丢弃包头
            std::string message = inputBuffer->retrieveAsString(body_len);
            // 计时器：T1结束
            current_frame_ctx_->t_parsed = LatencyProfiler::now();

            // 发送图片数据（帧号即 TraceID，结果侧的日志足以对应到具体帧）
            submitImageInLoop(engine_ptr, std::move(message), current_frame_ctx_);
            // 发送结束后重置上下文
            current_frame_ctx_.reset();
        }else{// 有包头但数据未传完，退出循环并等待下一次 Epoll 触发可读事件
            break;
        }
    }
}

void Connection::submitImageInLoop(AsyncAIEngine* engine_ptr,
                                   std::string&& image_data,
                                   FrameContextPtr ctx) {
    if (in_flight_requests_ >= kMaxInFlightRequests) {
        sendProtocolErrorInLoop(0, "OVERLOADED", "Too many in-flight frames; slow down the sender.");
        return;
    }

    if (!ctx) {
        ctx = std::make_shared<FrameContext>();
    }
    ctx->t_parsed = LatencyProfiler::now();
    ++in_flight_requests_;
    engine_ptr->AnalyzeFrameAsync(ctx, std::move(image_data), weak_from_this());
}

void Connection::completeAnalysis(const std::string& json) {
    std::weak_ptr<Connection> weakSelf = shared_from_this();
    loop->runInLoop([weakSelf, json]() {
        if (auto self = weakSelf.lock()) {
            if (self->in_flight_requests_ > 0) {
                --self->in_flight_requests_;
            }
            self->sendInLoop(ResponseSerializer::frameTcpPayload(json));
        }
    });
}

void Connection::sendProtocolErrorInLoop(uint64_t frame_id,
                                         const std::string& code,
                                         const std::string& message) {
    const std::string json = ResponseSerializer::errorJson(frame_id, code, message);
    sendInLoop(ResponseSerializer::frameTcpPayload(json));
}

// 发送接口可由任意线程调用，实际 I/O 始终回到 EventLoop 线程执行。
void Connection::send(const std::string& msg){
    std::weak_ptr<Connection> weakSelf = shared_from_this();
    loop->runInLoop([weakSelf, msg]() {// 在主线程（调用loop的线程）进行发送任务
        if (auto self = weakSelf.lock()) {//检查conn是否存活
            self->sendInLoop(msg);
        }
    });
}

void Connection::sendInLoop(const std::string& msg){
    if (state_ == kDisconnected) {
        return;
    }
    //先发送已有的数据
    if(outputBuffer->readableBytes() > 0){
        outputBuffer->append(msg.c_str(),msg.size() );
        return;
    }
    //write(sock->fd(), readBuffer.c_str(), readBuffer.size());
    //
    ssize_t nwrote = 0;// 记录
    size_t remaining = msg.size();
    bool faultError = false;
    do {
        nwrote = write(sock->fd() , msg.c_str() , msg.size() );
    } while (nwrote < 0 && errno == EINTR);

    if(nwrote >= 0){
        remaining = msg.size() - nwrote;
        if(remaining == 0) return;//没有剩余，直接返回
    }else{
        nwrote = 0;
        if(errno != EWOULDBLOCK && errno != EAGAIN){
            faultError = true;//发生意外错误，排除读取完全部数据的错误码
        }
    }
    // 如果没写完，追加到 outBuffer 并注册 EPOLLOUT
    if (!faultError && remaining > 0) {
        std::cout << "[Send] 内核缓冲区已满，剩余 " << remaining << " 字节转入 OutputBuffer" << std::endl;
        outputBuffer->append(msg.c_str() + nwrote, remaining);
        
        // 通过channel类对象，将写事件添加到epoll
        if (!channel->isWriting()) {
            channel->enableWriting(); 
        }
    } else if (faultError) {
        std::cerr << "[Send] 写入失败: " << std::strerror(errno) << std::endl;
        handleClose();
    }
}

// 写处理
void Connection::handleWriteEvent(){
    if (channel->isWriting()) {
        std::cout << "[HandleWrite] Epoll 触发可写，准备搬运 Buffer 数据..." << std::endl;
        while (outputBuffer->readableBytes() > 0) {
            const ssize_t n = write(sock->fd(), outputBuffer->peek(), outputBuffer->readableBytes());
            if (n > 0) {
                outputBuffer->retrieve(static_cast<size_t>(n));
                std::cout << "[HandleWrite] 成功发送 " << n
                          << " 字节，剩余积压 " << outputBuffer->readableBytes() << std::endl;
                continue;
            }

            if (n < 0 && errno == EINTR) {
                continue;
            }

            if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
                break;
            }

            std::cerr << "[HandleWrite] 写入失败: "
                      << (n < 0 ? std::strerror(errno) : "peer closed") << std::endl;
            handleClose();
            return;
        }

        if (outputBuffer->readableBytes() == 0) {
            std::cout << "[HandleWrite] 数据发送完毕，注销 EPOLLOUT" << std::endl;
            channel->disableWriting(); 
            //如果此时连接准备结束但尚未结束，调用回调函数通知释放connection
            if(state_ == kDisconnecting ){
                std::cout << "[Connection] 残留数据发送完毕，释放Connection" << std::endl;
                state_ = kDisconnected;
                if (deleteConnectionCallback) deleteConnectionCallback(sock);
            }
        }
    }
}