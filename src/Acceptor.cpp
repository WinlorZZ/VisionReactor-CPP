#include <cerrno>
#include <cstring>
#include <iostream>

#include "Acceptor.h"
#include "Channel.h"
#include "EventLoop.h"
#include "InetAddress.h"
#include "Socket.h"

Acceptor::Acceptor(EventLoop *loop) : loop(loop) {
    lis_sock = new Socket();
    InetAddress addr("127.0.0.1", 8888);
    lis_sock->bind(addr);
    lis_sock->listen();
    lis_sock->setNonBlocking();

    acceptChannel = new Channel(loop, lis_sock->fd());
    acceptChannel->setReadCallback(
        std::bind(&Acceptor::acceptNewConnection, this));
    acceptChannel->enableReading();
}

Acceptor::~Acceptor() {
    // Channel 必须先从 epoll 移除，再关闭它引用的 fd。
    delete acceptChannel;
    delete lis_sock;
}

void Acceptor::acceptNewConnection() {
    // ET 模式必须持续 accept，直到监听 socket 返回 EAGAIN。
    while (true) {
        InetAddress clnt_addr;
        const int fd = lis_sock->accept(clnt_addr);
        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                std::cerr << "accept error: " << std::strerror(errno) << std::endl;
            }
            break;
        }

        Socket *clnt_sock = new Socket(fd);
        clnt_sock->setNonBlocking();
        std::cout << "new client fd " << clnt_sock->fd()
                  << " IP: " << clnt_addr.getIP()
                  << " Port: " << ntohs(clnt_addr.getPort())
                  << std::endl;

        if (newConnectionCallback) {
            newConnectionCallback(clnt_sock);
        } else {
            delete clnt_sock;
        }
    }
}
