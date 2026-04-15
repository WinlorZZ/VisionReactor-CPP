#include <iostream>
#include "Acceptor.h"
#include "Socket.h"
#include "Channel.h"
#include "Epoll.h"
#include "InetAddress.h"
#include "EventLoop.h"


Acceptor::Acceptor(EventLoop *loop) : loop(loop) {
    // 初始化socket和对应的addr，绑定、监听、非阻塞
    lis_sock = new Socket();
    InetAddress *addr = new InetAddress("127.0.0.1", 8888);
    //存储地址是临时变量最后要释放
    lis_sock->bind(*addr);
    lis_sock->listen();
    lis_sock->setNonBlocking();

    // 初始化Channel
    acceptChannel = new Channel(loop, lis_sock->fd());
    // 使用bind将设置好的功能函数通过ch::set函数存储在channnel对象私域空间内
    std::function<void()> cb = std::bind(&Acceptor::acceptNewConnection, this);
    acceptChannel->setReadCallback(cb);// 将返回的函数存储通过set存储在channel对象中，方便之后使用
    acceptChannel->enableReading();// 开启channel监听

    delete addr;
}

Acceptor::~Acceptor(){
    // 释放自己管理的数据
    // Epoll对象不属于Acceptor管理，只是一个关联的对象
    delete lis_sock;
    delete acceptChannel;
}

void Acceptor::acceptNewConnection(){ // 具体实现，该函数通过acceptor对象构造时存储在channel对象中以便调用
    InetAddress *clnt_addr = new InetAddress();// 临时变量存储地址，记得销毁
    Socket *clnt_sock = new Socket(lis_sock->accept(*clnt_addr));
    // 从lis_sock处接受客户端socket的相关信息
    // lis_sock调用accept时，接受clnt_addr并存储在clnt_socket中
    std::cout << "new client fd " << clnt_sock->fd() 
              << " IP: " << clnt_addr->getIP() 
              << " Port: " << ntohs(clnt_addr->getPort()) 
              << std::endl;

    clnt_sock->setNonBlocking();

    // 调用回调函数，把新 Socket 扔给 Server
    // 1. 防御性检查：确认 Server 已经设置好了newConnectionCallback，否则这个变量是nullptr，调用就会报错
    if(newConnectionCallback){
        // 2. 执行这个回调函数，将创建好的clnt_sock传回给server，这时候由上层持有clnt_sock
        newConnectionCallback(clnt_sock);
    }

    delete clnt_addr;
}

