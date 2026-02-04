#include "Epoll.h"
#include "InetAddress.h"
#include "Socket.h"
#include "Channel.h"
#include "util.h"
#include <unistd.h>     // for close
#include <sys/epoll.h> // epoll_create(), epoll_ctl(), epoll_wait()

class Channel; // 前向声明

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

// void Epoll::addFd( int fd, uint32_t events){
//     struct epoll_event event;
//     event.events = events;// 设置关注的事件
//     event.data.fd = fd;
    
//     errif(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1 ,
//                     "epoll_ctl error");
//     return;    
// }

// 更新Channel对应的事件设置
void Epoll::updateChannel(Channel* channel){
    int fd = channel->getFd();
    struct epoll_event event;
    bzero(&event, sizeof(event));// 清零结构体，避免垃圾数据影响
    event.events = channel->getEvents(); // 从 Channel 获取关注的事件
    // 核心：将 data.ptr 指向 channel 对象本身！
    // 以前是 ev.data.fd = fd; 现在是存指针,多套了一层
    event.data.ptr = channel;
    
    // 判断是 新增(ADD) 还是 修改(MOD)
    if (channel->isInEpoll() == false) {
        // 如果不在 epoll 红黑树里，说明是新的，用 ADD
        errif(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1, "epoll add error");
        channel->setInEpoll(true); // 标记已添加
        // std::cout << "Epoll: Added fd " << fd << " to red-black tree" << std::endl;
    } else {
        // 如果已经在里面了，说明只是改事件（比如从读变成写），用 MOD
        errif(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event) == -1, "epoll mod error");
        // std::cout << "Epoll: Modified fd " << fd << " events" << std::endl;
    }
    return;
}

std::vector<Channel*> Epoll::poll(int timeout){
    int numFDs = epoll_wait(epoll_fd,events.data(),events.size(),timeout);
    errif( numFDs == -1 , "epoll_wait error");
    std::vector<Channel*> act_Channels;
    for(int i = 0;i < numFDs ; i++){
        // 这里只返回实际就绪的事件对应的 Channel 指针
        // 将它们收集到一个新的向量中返回
        Channel *ch = static_cast<Channel*>(events[i].data.ptr);//注意ptr
        // 告诉 Channel 刚才发生了什么事件
        ch->setRevents(events[i].events);// 实际发生的事件赋值给 Channel 对象的 revents
        act_Channels.push_back(ch);// 把指针加入返回列表
    }
    return act_Channels;
}