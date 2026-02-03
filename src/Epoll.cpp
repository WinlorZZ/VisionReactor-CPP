#include "Epoll.h"
#include "InetAddress.h"
#include "Socket.h"
#include "util.h"
#include <unistd.h>     // for close
#include <sys/epoll.h> // epoll_create(), epoll_ctl(), epoll_wait()

Epoll::Epoll() :  epoll_fd(-1), events(1024){//初始化events数组，大小为1024
    epoll_fd = epoll_create1(0);//创建epoll实例
    errif(epoll_fd == -1,"epoll_create1 error"); 
}

Epoll::~Epoll(){
    if(epoll_fd != -1){
        close(epoll_fd);
        epoll_fd = -1;
    }
}

void Epoll::addFd( int fd, uint32_t events){
    struct epoll_event event;
    event.events = events;
    event.data.fd = fd;
    
    errif(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1 ,
                    "epoll_ctl error");
    return;    
}

std::vector<epoll_event> Epoll::poll(int timeout){
    int numFDs = epoll_wait(epoll_fd,events.data(),events.size(),timeout);
    errif( numFDs == -1 , "epoll_wait error");
    std::vector<epoll_event> act_events;
    for(int i = 0;i < numFDs ; i++){
        // 这里只返回实际就绪的事件
        // 将它们收集到一个新的向量中返回
        act_events.push_back(events[i]);
    }
    return act_events;
}