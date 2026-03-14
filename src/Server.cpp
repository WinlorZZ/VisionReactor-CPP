#include "Server.h"
#include "Socket.h"
#include "Acceptor.h"
#include "Connection.h"
#include "ThreadPool.h" // 其他类头文件
#include "AsyncAIEngine.h"
#include <unistd.h>     // for sleep test
#include <iostream>
#include <memory>
#include <grpcpp/grpcpp.h>
#include "../proto/game_ai.grpc.pb.h"

Server::Server(EventLoop *loop) : loop(loop), acceptor(nullptr), threadPool(nullptr) {
    // 初始化线程池和监听器
    acceptor = new Acceptor(loop);
    threadPool = new ThreadPool(4); 
    // 设置acceptor的消息回调函数，让他以这个方式通知自己
    std::function<void(Socket*)> cb = std::bind(&Server::handleNewConnection, this, std::placeholders::_1);
    acceptor->setNewConnectionCallback(cb);
    auto gchannel = grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());
    aiengine = std::make_unique<AsyncAIEngine>(gchannel, threadPool);
}

Server::~Server() {
    delete acceptor;
    delete threadPool; // 自动停止线程
    // for(auto &item : conns) {
    //     delete item.second;
    // }
}

void Server::handleNewConnection(Socket *clnt_sock) {
    std::shared_ptr< Connection > conn = std::make_shared<Connection>(loop, clnt_sock);
    conns[clnt_sock->fd()] = conn;
    // 绑定conn对象中的两个回调
    // 绑定delete回调
    conn->setDeleteConnectionCallback(std::bind(&Server::handleDeleteConnection, this, std::placeholders::_1));
    
    // 绑定消息回调
    // 当 Connection 读完数据，会调用 Server::handleOnMessage
    conn->setOnMessageCallback(std::bind(&Server::handleOnMessage, this, std::placeholders::_1));
    conn->connectEstablished();
}

// 3. 任务分发中心 (主线程执行)
void Server::handleOnMessage(std::shared_ptr<Connection> conn) {
    // 把引擎指针给线程
    AsyncAIEngine* engine_ptr = aiengine.get();
    // 将任务扔进线程池，主线程立刻返回
    threadPool->add([conn , engine_ptr ](){
        // --- 以下代码在 Worker 线程执行 ---
        // std::cout << "Worker handling..." << std::endl;
        // sleep(3); // 可选：模拟耗时测试并发
        conn->business( engine_ptr ); // 执行具体的业务逻辑
    });
    //执行后，主线程立刻返回loop继续epoll_wait
}

void Server::handleDeleteConnection(Socket *clnt_sock) {
    int fd = clnt_sock->fd();
    auto it = conns.find(fd);// 通过fd查找conn，如果不在conns里，it = conns.end()
    if(it != conns.end()) {// 如果fd在自己的conns列表中
        conns.erase(fd);// 从列表移除
        std::cout << "[Server] 已从 Map 中移除 fd " << fd << "，Server 对此连接的所有权已解除。" << std::endl;
    }
}