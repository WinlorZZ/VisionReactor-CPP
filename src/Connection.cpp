#include "Connection.h"
#include "Socket.h"
#include "Channel.h"
#include <unistd.h>
#include <cstring>
#include <iostream>

Connection::Connection(EventLoop *loop, Socket *sock) : loop(loop), sock(sock) {
    channel = new Channel(loop, sock->fd());
    std::function<void()> cb = std::bind(&Connection::handleReadEvent, this);
    channel->setReadCallback(cb);
    channel->enableReading();
    readBuffer = ""; // 初始化
}

Connection::~Connection() {
    delete channel;
    delete sock;
}

void Connection::setDeleteConnectionCallback(std::function<void(Socket*)> cb) { 
    deleteConnectionCallback = cb; 
}

void Connection::setOnMessageCallback(Callback cb) { 
    onMessageCallback = cb; 
}

// 1. IO 读取 
// 将该函数设置给Connection管理的对应的channel，channel在被调用handleEvent时会使用该函数
void Connection::handleReadEvent() {
    char buf[1024];
    while(true) {
        memset(buf, 0, sizeof(buf));
        ssize_t bytes_read = read(sock->fd(), buf, sizeof(buf));
        
        if(bytes_read > 0) {
            // [CHANGE] 存入缓冲区，而不是直接 echo
            readBuffer.append(buf, bytes_read);
        } else if(bytes_read == -1 && errno == EINTR) {
            continue;
        } else if(bytes_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // 数据读完了,但连接不需要销毁，通知等待
            // 如果读到了数据，通知 Server
            if(!readBuffer.empty() && onMessageCallback) {
                onMessageCallback(this);
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

// 2. 业务处理 (运行在 Worker 线程)
void Connection::business() {
    if (readBuffer.empty()) return;// 当读缓冲区为空时返回

    // 可以在这里打印线程 ID 验证并发
    // std::cout << "Business logic in thread: " << std::this_thread::get_id() << std::endl;

    // 处理业务 (Echo)
    write(sock->fd(), readBuffer.c_str(), readBuffer.size());
    
    // 处理完清空
    readBuffer.clear();
}