#pragma once
#include <functional>
#include <string>
#include <memory>

class EventLoop;
class Socket;
class Channel;
class Buffer;

class Connection : public std::enable_shared_from_this<Connection>{
public:
    Connection(EventLoop *loop, Socket *sock);
    ~Connection();
    
    // using Callback = std::function<void(Connection*)>; // 回调类型：传自己回去

    void setDeleteConnectionCallback(std::function<void(Socket*)> cb);
    
    // 设置消息回调
    void setOnMessageCallback(std::function< void( std::shared_ptr<Connection> ) >cb);

    // 只负责读数据 (IO)
    void handleReadEvent();
    
    // 负责处理数据 (计算/业务)
    void business(); 

    // 发送接口，业务处理完后调用此接口发送数据
    void send(const std::string& msg);

    // 写回调，由EventLoop调用
    void handleWriteEvent();

private:
    EventLoop *loop;
    Socket *sock;
    Channel *channel;

    std::function<void(Socket*)> deleteConnectionCallback;
    
    std::function<void( std::shared_ptr< Connection >)> onMessageCallback; 
    
    // 储存读到的数据
    // std::string readBuffer;
    Buffer* inputBuffer;
    Buffer* outputBuffer;
};