#pragma once
#include <functional>
#include <string>

class EventLoop;
class Socket;
class Channel;

class Connection {
public:
    Connection(EventLoop *loop, Socket *sock);
    ~Connection();
    
    using Callback = std::function<void(Connection*)>; // 回调类型：传自己回去

    void setDeleteConnectionCallback(std::function<void(Socket*)> cb);
    
    // 设置消息回调
    void setOnMessageCallback(Callback cb);

    // 只负责读数据 (IO)
    void handleReadEvent();
    
    // 负责处理数据 (计算/业务)
    void business(); 

private:
    EventLoop *loop;
    Socket *sock;
    Channel *channel;

    std::function<void(Socket*)> deleteConnectionCallback;
    
    Callback onMessageCallback; 
    
    std::string readBuffer;// 储存读到的数据
};