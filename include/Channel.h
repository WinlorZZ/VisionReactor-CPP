#pragma once

#include <iostream>
#include <sys/epoll.h>
#include <functional> // for std::function?


class EventLoop; // 前向声明
class Epoll;
// using EventCallback = std::function<void()>; 
// 定义回调函数类型，也可以直接使用 std::function<void()>


// Channel
class Channel {
public:
    Channel(EventLoop *loop, int fd); // 构造函数, 初始化 fd 和 epoll 指针
    ~Channel();
    void setReadCallback(std::function<void()> cb);   // 初始化读事件函数的方法
    void setWriteCallback(std::function<void()> cb);  // 初始化写事件函数的方法
    void enableReading(); // 启用读事件监听，将事件添加到 epoll 实例中
    void handleEvent(); // 处理事件，调用相应的回调函数

    int getFd() const { return fd; } // 获取文件描述符
    uint32_t getEvents() const { return events; } // 获取关注的事件
    Epoll* getEpoll() const { return ep; } // 获取关联的 Epoll 实例
    bool isInEpoll() const { return isadd; } // 检查是否已添加到 Epoll 实例中
    void setInEpoll(bool inEpoll) { isadd = inEpoll; } // 修改标记isadd
    void setRevents(uint32_t revt) { revents = revt; } // 设置发生的事件

private:
    int fd; // 文件描述符，每个channel对应唯一一个 fd
    EventLoop *loop;
    Epoll* ep; // 指向 Epoll 实例的指针，用于注册和更新事件，每个 Channel 都关联一个 Epoll 实例
    bool isadd = false; // 标记该 Channel 是否已添加到 Epoll 实例中，默认为 false未添加
    uint32_t events;  // 关注的事件 (用户设置的)
    uint32_t revents; // 目前发生的事件 (epoll_wait 返回的，由setRevents设置)
    std::function<void()> readCallback;  // 存储读回调函数
    std::function<void()> writeCallback; // 存储写回调函数
};