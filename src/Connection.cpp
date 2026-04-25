#include "Connection.h"
#include "Socket.h"
#include "Channel.h"
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <memory> // shared_from_this
#include "Buffer.h"
#include "AsyncAIEngine.h"
#include <opencv2/opencv.hpp>// cv::Mat

#include <thread> // 对应 std::this_thread
#include <chrono> // 对应 std::chrono
#include <atomic> // std::atomic

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
    // readBuffer = ""; // 初始化
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
    // 将数据处理放到循环外进行（之前在n > 0就处理了）
    if (inputBuffer->readableBytes() > 0 && onMessageCallback) {
        onMessageCallback(shared_from_this());
    }
    // char buf[1024];
    // while(true) {
    //     memset(buf, 0, sizeof(buf));
    //     ssize_t bytes_read = read(sock->fd(), buf, sizeof(buf));
        
    //     if(bytes_read > 0) {
    //         // [CHANGE] 存入缓冲区，而不是直接 echo
    //         inputBuffer->append(buf, bytes_read);
    //     } else if(bytes_read == -1 && errno == EINTR) {
    //         continue;
    //     } else if(bytes_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    //         // 数据读完了,但连接不需要销毁，通知等待
    //         // 如果读到了数据，通知 Server
    //         if(inputBuffer->readableBytes() > 0 && onMessageCallback) {
    //             onMessageCallback( shared_from_this() );
    //         }
    //         break;
    //     } else if(bytes_read == 0) {
    //         // 断开连接
    //         // 通知上层清理
    //         if(deleteConnectionCallback) deleteConnectionCallback(sock);
    //         break;
    //     } else {
    //          if(deleteConnectionCallback) deleteConnectionCallback(sock);
    //          break;
    //     }
    // }
}

void Connection::handleClose() {
    state_ = kDisconnecting; // 状态切换：准备断开
    channel->disableReading(); // 不再接收新数据

    // 检查是否有残留的未读数据
    if (outputBuffer->readableBytes() == 0) {
        // 没有则直接通知server回收
        if (deleteConnectionCallback) deleteConnectionCallback(sock);
    } else {
        std::cout << "[Connection] 触发优雅挥手，发现 Buffer 仍有积压，延迟销毁！" << std::endl;
        // 留着 EPOLLOUT，让 handleWriteEvent 把剩下的数据发完
    }
}

// 业务处理 (运行在 Worker 线程)
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
        uint32_t body_len = inputBuffer->peekInt32();
        std::cout << "[Debug] 收到 Header，解析出的 Body 长度为: " << body_len << std::endl;
        if (body_len <= 0 || body_len > 10 * 1024 * 1024) {
            // std::cerr << "[-] 致命错误：非法的数据包长度 " << body_len << "，强制断开连接！\n";
            handleClose();
            break;
        }
        if (!current_frame_ctx_) {
            current_frame_ctx_ = std::make_shared<FrameContext>();
            std::cout << "[Trace] 新帧开始接收 -> TraceID: " << current_frame_ctx_->trace_id << std::endl;
        }

        if (inputBuffer->readableBytes() >= 4 + body_len) {
            inputBuffer->retrieve(4);// 丢弃包头
            std::string message = inputBuffer->retrieveAsString(body_len);
            // 计时器：T1结束
            current_frame_ctx_->t_parsed = LatencyProfiler::now();
            std::cout << "[Debug] 数据已齐，准备调用 AI 引擎..." << std::endl;
            
            /* 处理业务 */ 
            uint64_t current_frame_id = current_frame_ctx_->trace_id;
            std::cout << "[协议层] 成功切包！提取到完整图像载荷，大小: " 
                        << message.size() << " bytes -> FrameID: " << current_frame_id << "\n";
            // 发送图片数据
            engine_ptr->AnalyzeFrameAsync(current_frame_ctx_, std::move(message));
            // 发送结束后重置上下文
            current_frame_ctx_.reset();
        }else{// 有包头但数据未传完，退出循环并等待下一次 Epoll 触发可读事件
            std::cout << "[Debug] 数据未齐，当前缓冲区: " << inputBuffer->readableBytes() 
                      << " 字节，等待下一波..." << std::endl;
            break;
        }
    }
}

// 发送接口，提供给business调用
void Connection::send(const std::string& msg){
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
    nwrote = write(sock->fd() , msg.c_str() , msg.size() );
    if(nwrote >= 0){
        remaining = msg.size() - nwrote;
        if(remaining == 0) return;//没有剩余，直接返回
    }else{
        //nwrote == 0;
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
    }
}

// 写处理
void Connection::handleWriteEvent(){
    if (channel->isWriting()) {
        std::cout << "[HandleWrite] Epoll 触发可写，准备搬运 Buffer 数据..." << std::endl;
        // 取出 outputBuffer_ 中的积压数据继续写
        ssize_t n = write(sock->fd(), outputBuffer->peek(), outputBuffer->readableBytes());
        
        if (n > 0) {
            // 发送成功 n 字节，向后移动读游标readerIndex_
            outputBuffer->retrieve(n);
            std::cout << "[HandleWrite] 成功发送 " << n << " 字节，剩余积压 " << outputBuffer->readableBytes() << std::endl;
            // 如果发完了，立刻注销 EPOLLOUT，防止死循环
            if (outputBuffer->readableBytes() == 0) {
                std::cout << "[HandleWrite] 数据发送完毕，注销 EPOLLOUT" << std::endl;
                channel->disableWriting(); 
                //如果此时连接准备结束但尚未结束，调用回调函数通知释放connection
                if(state_ == kDisconnecting ){
                    std::cout << "[Connection] 残留数据发送完毕，释放Connection" << std::endl;
                    if (deleteConnectionCallback) deleteConnectionCallback(sock);
                }
            }
        }
    }
}