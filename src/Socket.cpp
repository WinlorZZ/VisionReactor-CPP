#include "Socket.h"
#include "InetAddress.h"
#include "util.h"
#include <unistd.h>     // for close
#include <fcntl.h>      // for fcntl
#include <sys/socket.h> // for socket, bind, listen...

// 两个构造函数
// 默认构造方法：创建一个新的 socket
Socket::Socket() : _fd(-1) {
    // AF_INET: IPv4
    // SOCK_STREAM: TCP
    // 0: 自动选择协议
    _fd = socket(AF_INET, SOCK_STREAM, 0);//创建一个TCP socket
    errif(_fd == -1, "socket create error");

    // 允许端口复用 (防止 bind error: Address already in use)
    int opt = 1;
    setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

// 对应 socket(fd) ：用于包装已有的 fd (比如 accept 返回的)
Socket::Socket(int fd) : _fd(fd) {
    errif(_fd == -1, "socket create error");
}

// 析构函数：对应 close() -> RAII 的核心
Socket::~Socket() {
    if (_fd != -1) {
        close(_fd);
        _fd = -1; //以此避免悬挂指针（虽然这里是整数）
    }
}

// bind()方法实现，绑定地址和端口，与C写法完全一致
void Socket::bind(const InetAddress& addr){
    int fd = this->fd();
    errif( ::bind(fd,addr.getAddr(),addr.getAddrLen()) == -1 ,"bind error");
    return;
}

// listen()方法实现，开始监听
void Socket::listen(){
    int fd = this->fd();
    errif(::listen(fd,128) == -1 , "listen error");
    return;
}

// 设置非阻塞模式
void Socket::setNonBlocking(){
    int fd = this->fd();
    int flags = fcntl(fd, F_GETFL, 0);
    if(flags == -1){
        perror("fcntl F_GETFL error");
        return;
    }
    if(fcntl(fd,F_SETFL,flags | O_NONBLOCK) == -1){
        perror("fcntl F_SETFL error");
        return;
    }
    return;
}

// accept()方法实现，接受新连接
int Socket::accept(InetAddress& client_addr){
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    //socklen_t是一种无符号整数类型，专门用于表示套接字地址结构的长度
    int clnt_fd = ::accept(this->fd(), (struct sockaddr*)&addr, &len);
    if(clnt_fd != -1){//成功接受连接
        // 把拿到的客户端地址信息，填入传入的 InetAddress 对象中
        client_addr.setAddr(addr);
    }
    
    return clnt_fd;//返回新的连接socket的文件描述符
}

// 不需要客户端地址信息时的重载版本
int Socket::accept(){
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int clnt_fd = ::accept(this->fd(), (struct sockaddr*)&addr, &len);
    errif(clnt_fd == -1, "socket accept error");
    return clnt_fd;
}