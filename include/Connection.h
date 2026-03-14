#pragma once
#include <functional>
#include <string>
#include <memory>

class EventLoop;
class Socket;
class Channel;
class Buffer;
class AsyncAIEngine;

class Connection : public std::enable_shared_from_this<Connection>{
public:
    // 三个状态：连接保持，准备断开连接，连接已经断开
    enum StateE { kConnected, kDisconnecting, kDisconnected };
    
    // 初始化函数
    void connectEstablished(); 
    // 销毁函数
    void connectDestroyed();

    Connection(EventLoop *loop, Socket *sock);
    ~Connection();

    // using Callback = std::function<void(Connection*)>; // 回调类型：传自己回去

    void setDeleteConnectionCallback(std::function<void(Socket*)> cb);
    
    // 设置消息回调
    void setOnMessageCallback(std::function< void( std::shared_ptr<Connection> ) >cb);

    // 只负责读数据 (IO)
    void handleReadEvent();
    
    // 负责处理数据 (计算/业务)
    void business( AsyncAIEngine* engine_ptr ); 

    // 发送接口，业务处理完后调用此接口发送数据
    void send(const std::string& msg);

    // 写回调，由EventLoop调用
    void handleWriteEvent();

    // ?
    void handleClose();

private:
    StateE state_; // 当前连接状态

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