#pragma once

#include <map>
#include <functional>
#include <memory>//shared_ptr

class EventLoop;
class Socket;
class Acceptor;
class Connection;
class ThreadPool; // 前置声明

class Server {
public:
    Server(EventLoop *loop);
    ~Server();

    void handleNewConnection(Socket *sock);
    void handleDeleteConnection(Socket *sock);
    
    // [NEW] 专门处理从 Connection 发来的业务请求
    void handleOnMessage(std::shared_ptr<Connection> conn);

private:
    EventLoop *loop; // 聚合
    Acceptor *acceptor;// 组合
    std::map<int, std::shared_ptr<Connection> > conns;// 组合，每个fd对应一个conn，通过查找fd来查找conn
    
    ThreadPool *threadPool; // 组合
};