#pragma once
#include <functional> //std::function

// class EventLoop; // 暂时还叫 Epoll，以后会叫 EventLoop，这里先写 Epoll
class Epoll;
class Socket;
class InetAddress;
class Channel;

//  持有 ListenSocket 和对应的 Channel。只进行 accept() ，拿到新连接的 fd 后，回调给上层（Server）
class Acceptor
{
public:
    // 回调函数类型：当有新连接时，把新 Socket 传给上层
    using NewConnectionCallback = std::function<void(Socket*)>;
    // 重命名一个函数类型，函数参数是 Socket*，无返回值

    Acceptor(Epoll *ep);
    ~Acceptor();
    
    void acceptNewConnection(); // 接受新连接，接管之前channel的handleEvent方法
    void setNewConnectionCallback(const std::function<void(Socket*)> cb){
        //Server通过调用该方法以设置函数NCCB
        newConnectionCallback = cb;
    }

private:
    Epoll *ep; // Acceptor 需要向 Epoll 注册自己的监听 Channel
    Socket *lis_sock; // 监听的 Socket
    Channel *acceptChannel; // 监听 Socket 对应的 Channel

    std::function<void(Socket*)> newConnectionCallback; // 保存上层传来的回调
};


