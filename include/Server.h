#pragma once

#include <map>
#include <functional>
#include <memory>//shared_ptr


// 前置声明
class EventLoop;
class Socket;
class Acceptor;
class Connection;
class ThreadPool; 
class AsyncAIEngine;

class Server {
public:
    Server(EventLoop *loop);
    Server(EventLoop *loop, const std::string& ai_target);
    ~Server();

    void handleNewConnection(Socket *sock);
    void handleDeleteConnection(Socket *sock);
    
    // 专门处理从 Connection 发来的业务请求
    void handleOnMessage(std::shared_ptr<Connection> conn);

private:
    EventLoop *loop; // 聚合
    Acceptor *acceptor;// 组合
    std::map<int, std::shared_ptr<Connection> > conns;// 组合，每个fd对应一个conn，通过查找fd来查找conn
    
    std::unique_ptr<AsyncAIEngine> aiengine;// 组合，且声明在线程池之前
    ThreadPool *threadPool; // 组合
};