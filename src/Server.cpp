#include "Server.h"
#include "Socket.h"
#include "Channel.h"
#include "Acceptor.h"
#include "Connection.h"
#include <functional>

Server::Server(Epoll *ep) : ep(ep) {
    acceptor = new Acceptor(ep);//初始化acceptor
    // std::placeholders::_1占位符，
    std::function<void(Socket*)> cb = std::bind(&Server::handleNewConnection,this,std::placeholders::_1);
    acceptor->setNewConnectionCallback(cb);// 设置好acceptor的新连接处理的回调函数
}

Server::~Server(){
    delete acceptor;
    for(auto &item : conns){
        delete item.second;// pair对中second为具体的值，释放该对象
    }
}

// 这个函数会被 Acceptor 触发
void Server::handleNewConnection(Socket *clnt_sock) {// clnt_sock从Acceptor传回来的
    std::cout << "Server: handling new connection for fd " << clnt_sock->fd() << std::endl;
    
    // 1. 新建Connection来进行业务
    // 注意：我们把 clnt_sock 指针的所有权移交给了 Connection 对象
    Connection *conn = new Connection(ep, clnt_sock);
    
    // 2. 将新建的对象加入map管理名单
    conns[clnt_sock->fd()] = conn;
    
    // 3. 将写好的函数传入Connetion对象，并通过Connection::set存储
    std::function<void(Socket*)> cb = std::bind(&Server::handleDeleteConnection, this, std::placeholders::_1);
    conn->setDeleteConnectionCallback(cb);
} 

// 这个函数会被 Connection 触发
void Server::handleDeleteConnection(Socket *clnt_sock){
    int fd = clnt_sock->fd();
    std::cout << "Server: cleaning up connection for fd " << fd << std::endl;
    // 1. 在名册中查找
    auto it = conns.find(fd);
    if(it != conns.end()){
        // 2. 找到对应的 Connection 对象
        Connection *conn = it->second;
        
        // 3. 从名册移除
        conns.erase(fd);
        
        // 4. 【核心动作】物理销毁 Connection 对象
        // 这会触发 Connection 的析构函数，进而自动关闭 fd，释放 Channel
        delete conn; 
        // 在这一步之后， clnt_sock 在 Connection 的析构函数里被 delete 了，这里不需要再 delete sock
    }
}

