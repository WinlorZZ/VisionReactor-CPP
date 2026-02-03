#include "Epoll.h"
#include "InetAddress.h"
#include "Socket.h"
#include "util.h"


#include <iostream>
#include <unistd.h>      // close(), read(), write()
#include <fcntl.h>       // fcntl(), O_NONBLOCK
#include <errno.h>
#include <vector>
const int MAX_EVENTS = 1000;

int main(){
    Socket listen_socket;//创建Socket对象，自动调用构造函数
    InetAddress server_addr("127.0.0.1", 8888);//创建InetAddress对象，初始化IP和端口

    listen_socket.bind(server_addr);//调用Socket类的bind方法，绑定地址和端口
    listen_socket.listen();//开始监听
    listen_socket.setNonBlocking();//设置监听socket为非阻塞模式
    
    Epoll epoll;//创建Epoll对象，自动调用构造函数，创建epoll实例
    epoll.addFd(listen_socket.fd(), EPOLLIN | EPOLLET);//将监听socket添加到epoll实例中
    std::cout << "Epoll 服务器启动，监听端口 8888" << std::endl;

    while(true){//事件循环
        // 等待事件发生
        std::vector<epoll_event> events = epoll.poll(-1);//调用Epoll类的poll方法，等待事件发生
        for(int i = 0; i < events.size(); i++) {//遍历实际就绪事件数组
            if(events[i].data.fd == listen_socket.fd()) {
                //情况a：对于epoll监听的所有socket，如果是listen_socket.fd()，表面有新连接
                //将新连接加入epoll实例中
                while(true){
                    InetAddress client_addr;
                    int new_fd = listen_socket.accept(client_addr);
                    if(new_fd == -1){
                        if(errno == EAGAIN || errno == EWOULDBLOCK){
                            //errno来自<errno.h>，是系统调用出错时设置的全局变量
                            //没有更多连接可接受，跳出循环
                            break;
                        }
                        else{
                            perror("accept error");
                            break;
                        }
                    }
                    // 设置新连接为非阻塞模式
                    setNonBlocking(new_fd);//设置新连接为非阻塞模式
                    epoll.addFd(new_fd,EPOLLIN | EPOLLET);//将新连接socket添加到epoll实例中，关注读事件，边缘触发模式
                    std::cout << "接受新连接，文件描述符: " << new_fd << std::endl;
                }
            }
            else if(events[i].events & EPOLLIN) {// events[i].events & EPOLLIN表示检查就绪事件的事件类型是否包含读事件
                //情况b：已有连接有数据可读                
                handleReadEvent(events[i].data.fd, -1); // 这里的 epoll_fd 暂时没用到，传 -1
            }
            else {
                std::cout << "其他事件发生" << std::endl;
            }
        }
    }
    return 0;
}

