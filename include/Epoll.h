#pragma once
#include <sys/epoll.h> // epoll_create(), epoll_ctl(), epoll_wait()
#include <vector>

class Epoll {
public:
    Epoll();
    ~Epoll();// 在析构时关闭监听的socket

    //方法addFd用于向epoll实例中添加新的文件描述符fd及其对应的事件
    void addFd(int fd, uint32_t events);
    // 方法poll用于等待事件发生，并返回实际就绪的事件列表
    std::vector<epoll_event> poll(int timeout = -1);//设置为-1表示无限等待

private:
    int epoll_fd;// epoll实例的文件描述符
    // 内部使用的就绪事件数组，对应原代码里的 events 数组
    // 把它变成成员变量，避免每次 poll 都重新分配内存
    std::vector<struct epoll_event> events;
};