#include "Epoll.h"
#include "InetAddress.h"
#include "Socket.h"
#include "Channel.h"
#include "util.h"


#include <iostream>
#include <unistd.h>     // close(), read(), write()
#include <fcntl.h>      // fcntl(), O_NONBLOCK
#include <functional>   // std::function
#include <errno.h>
#include <vector>
const int MAX_EVENTS = 1000;

int main(){
    Socket *listen_socket = new Socket(); //创建Socket对象，自动调用构造函数
    InetAddress *server_addr = new InetAddress("127.0.0.1", 8888);//创建InetAddress对象，初始化IP和端口

    listen_socket->bind(*server_addr);//调用Socket类的bind方法，绑定地址和端口
    listen_socket->listen();//开始监听
    listen_socket->setNonBlocking();//设置监听socket为非阻塞模式
    
    Epoll *ep = new Epoll();//创建Epoll对象，自动调用构造函数，创建epoll实例

    // 创建Channel对象，关联监听socket和epoll实例
    Channel *server_Channel = new Channel(ep,listen_socket->fd());
    // epoll.addFd(listen_socket.fd(), EPOLLIN | EPOLLET);//将监听socket添加到epoll实例中

    server_Channel->setReadCallback( //设置读事件回调函数
        [&](){
            InetAddress clntAddr; // 存储客户端地址
            int connfd = listen_socket->accept(clntAddr);//接受新连接，返回新的连接socket文件描述符
            if(connfd == -1) return;//接受失败，直接返回
            std::cout << "接受新连接，文件描述符: " << connfd << std::endl;
            setNonBlocking(connfd);// 设置新连接为非阻塞模式，注意与listen_socket->setNonBlocking() 区别

            // 为这个【新连接】也创建一个 Channel
            Channel *clntChannel = new Channel(ep, connfd);// 设置同一个epoll实例，绑定新连接的文件描述符
            clntChannel->setReadCallback( //和上面一样，新连接也要设置读事件回调函数
                [connfd](){ //新连接的回调函数与之前不同，直接调用读处理函数
                    handleReadEvent(connfd);
                }// handleReadEvent函数被调用，同时通过setReadCallback将其存储在clntChannel对象中的readCallback变量里
            );
            // 将新连接的 Channel 加入 Epoll 实例，关注读事件，边缘触发模式
            clntChannel->enableReading();
        }
    );

    // 开启 Server Channel 的监听
    server_Channel->enableReading();
    std::cout << "服务器启动，Server Channel 正在监听端口 8888" << std::endl;

    //事件循环，但更干净
    while(true){
        // 获取活跃的 Channel 列表,epoll.poll 返回的不再是就绪事件列表，而是Channel指针列表
       std::vector<Channel*> activeChannels = ep->poll();
       for( int i = 0; i < activeChannels.size(); ++i ){
           activeChannels[i]->handleEvent(); // 调用 Channel 的事件处理方法
        }
    }
    
    delete listen_socket;
    delete server_addr;

    return 0;
}

