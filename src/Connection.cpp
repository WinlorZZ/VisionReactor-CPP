#include "Connection.h"
#include "Socket.h"
#include "Channel.h"
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <memory> // shared_from_this
#include "Buffer.h"

#include <thread> // 对应 std::this_thread
#include <chrono> // 对应 std::chrono

Connection::Connection(EventLoop *loop, Socket *sock) : loop(loop), sock(sock) {
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
    channel->enableReading();
    // readBuffer = ""; // 初始化
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
    char buf[1024];
    while(true) {
        memset(buf, 0, sizeof(buf));
        ssize_t bytes_read = read(sock->fd(), buf, sizeof(buf));
        
        if(bytes_read > 0) {
            // [CHANGE] 存入缓冲区，而不是直接 echo
            inputBuffer->append(buf, bytes_read);
        } else if(bytes_read == -1 && errno == EINTR) {
            continue;
        } else if(bytes_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // 数据读完了,但连接不需要销毁，通知等待
            // 如果读到了数据，通知 Server
            if(inputBuffer->readableBytes() > 0 && onMessageCallback) {
                onMessageCallback( shared_from_this() );
            }
            break;
        } else if(bytes_read == 0) {
            // 断开连接
            // 通知上层清理
            if(deleteConnectionCallback) deleteConnectionCallback(sock);
            break;
        } else {
             if(deleteConnectionCallback) deleteConnectionCallback(sock);
             break;
        }
    }
}

// 业务处理 (运行在 Worker 线程)
void Connection::business() {
    std::string message = inputBuffer->retrieveAllAsString();
    if ( message.empty() ) return;// 当读缓冲区为空时返回

    /*处理业务 (Echo)*/ 

    // //大数据写入测试
    // if (message.find("TEST_FAT") != std::string::npos) {
    //     std::cout << "[Server] 接收到大包指令，生成 5MB 垃圾数据进行压测..." << std::endl;
        
    //     // 生成 5,242,880 字节的 'A'
    //     std::string fat_response(5 * 1024 * 1024, 'A'); 
    //     fat_response += "\n--- GIGANTIC DATA END ---\n";
        
    
    // std::string respond =fat_response;//测试用例
    // this->send(respond);
    // }

    // 崩溃测试
    if (message.find("TEST_CRASH") != std::string::npos) {
        // 获取当前引用计数（此时 Server 的 map 还没删，所以至少是 2）
        std::cout << "[Worker] 发现碰撞测试请求！当前引用计数: " 
                  << shared_from_this().use_count() << "，准备进入 5 秒深度睡眠..." << std::endl;

        // 【关键】：先睡 5 秒，给你充足的时间去按 Ctrl+C
        std::this_thread::sleep_for(std::chrono::seconds(5));

        std::cout << "[Worker] 醒来了！现在的引用计数是: " 
                  << shared_from_this().use_count() << std::endl;

        if (shared_from_this().use_count() == 1) {
            std::cout << "[Worker] 证实：Server 已删除连接，我是这块内存最后的守护者！" << std::endl;
        }

        this->send("Can you hear me now?\n");
        return;
    }

    // write(sock->fd(), readBuffer.c_str(), readBuffer.size());
    // // 处理完清空
    // readBuffer.clear();

    // 改用send发送
    
    // std::string respond = "Server Echo:" + message;
    // this->send(respond);
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
            // 发送成功 n 字节，向后移动读游标
            outputBuffer->retrieve(n);
            std::cout << "[HandleWrite] 成功发送 " << n << " 字节，剩余积压 " << outputBuffer->readableBytes() << std::endl;
            // 【解除 CPU 炸弹】如果发完了，立刻注销 EPOLLOUT！
            if (outputBuffer->readableBytes() == 0) {
                std::cout << "[HandleWrite] 数据发送完毕，注销 EPOLLOUT" << std::endl;
                channel->disableWriting(); 
            }
        }
    }
}