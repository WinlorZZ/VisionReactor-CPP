#pragma once
#include <functional> //std::function
#include "EventLoop.h"

class EventLoop; 
class Epoll;
class Socket;
class InetAddress;
class Channel;
class EventLoop;
//  持有 ListenSocket 和对应的 Channel。只进行 accept() ，拿到新连接的 fd 后，回调给上层（Server）
class Acceptor
{
public:
    // 回调函数类型：当有新连接时，把新 Socket 传给上层
    using NewConnectionCallback = std::function<void(Socket*)>;
    // 重命名一个函数类型，函数参数是 Socket*，无返回值

    // Acceptor(Epoll *ep);
    Acceptor(EventLoop* loop); // 构造函数，接受一个 EventLoop 指针，初始化监听 Socket 和 Channel，并注册事件
    ~Acceptor();
    
    void acceptNewConnection(); // 接受新连接
    void setNewConnectionCallback(std::function<void(Socket*)> cb){
        //Server通过调用该方法以设置函数NCCB
        newConnectionCallback = cb;
    }

private:
    EventLoop *loop; // Acceptor 需要向 Epoll(受loop管理) 注册自己的监听 Channel
    Socket *lis_sock; // 监听的 Socket
    Channel *acceptChannel; // 监听 Socket 对应的 Channel

    std::function<void(Socket*)> newConnectionCallback; // 保存上层传来的回调
};


