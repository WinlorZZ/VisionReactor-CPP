#pragma once

#include "Socket.h"
#include "Channel.h"
#include "vector"
#include "map"

class Epoll;
class Acceptor;
class Connection;

// 持有 Epoll、Acceptor 和一个 map<int, Connection*>，负责协调一切，包括最重要的资源回收
class Server {
public:
    Server(Epoll* epoll);// 构造时使用传入的epoll初始化acceptot
    ~Server();

    // 这两个函数是提供给 Acceptor 和 Connection 调用的回调
    // 虽然它们是回调，但在当前架构下需要是 public 的，才能被 bind 选中
    void handleNewConnection(Socket *sock); // 处理新连接
    void handleDeleteConnection(Socket *sock); // 处理连接断开

private:
    Epoll *ep;
    Acceptor *acceptor;
    std::map<int, Connection*> conns; // 核心：管理所有连接
};
