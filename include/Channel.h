#pragma once

#include <iostream>
#include <sys/epoll.h>
#include <functional> // for std::function?

class Epoll; // 前向声明

// using EventCallback = std::function<void()>; 
// 定义回调函数类型，也可以直接使用 std::function<void()>

class Channel {
public:
    Channel(Epoll *ep,int fd); // 构造函数, 初始化 fd 和 epoll 指针
    ~Channel();
    void setReadCallback(std::function<void()> cb);   // 设置读事件回调函数
    void setWriteCallback(std::function<void()> cb);  // 设置写事件回调函数
    void enableReading(); // 启用读事件监听，将事件添加到 epoll 实例中
    void handleEvent(); // 处理事件，调用相应的回调函数

private:
    int fd; // 文件描述符，每个channel对应唯一一个 fd
    Epoll* ep; // 指向 Epoll 实例的指针，用于注册和更新事件，每个 Channel 都关联一个 Epoll 实例
    uint32_t events;  // 关注的事件 (用户设置的)
    uint32_t revents; // 目前发生的事件 (epoll_wait 返回的)
    std::function<void()> readCallback;  // 存储读回调函数
    std::function<void()> writeCallback; // 存储写回调函数
};