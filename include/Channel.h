#pragma once

#include <iostream>
#include <sys/epoll.h>
#include <functional> // for std::function?
#include <memory>//shared_ptr

class EventLoop; // 前向声明
class Epoll;
// using EventCallback = std::function<void()>; 
// 定义回调函数类型，也可以直接使用 std::function<void()>


// Channel
class Channel {
public:
    Channel(EventLoop *loop, int fd); // 构造函数, 初始化 fd 和 epoll 指针
    ~Channel();

    // 供 Connection 建立弱绑定
    void tie(const std::shared_ptr<void>& obj);

    void setReadCallback(std::function<void()> cb);   // 初始化读事件函数的方法
    void setWriteCallback(std::function<void()> cb);  // 初始化写事件函数的方法
    void enableReading(); // 启用读事件监听，将事件添加到 epoll 实例中
    void enableWriting();// 启用写事件监听，将事件添加到epoll 实例中
    void disableWriting();  // 关闭写事件监听 (消除 CPU 炸弹)
    void disableReading();  // 关闭读事件监听
    bool isWriting() const; // 判断当前是否挂载了写事件

    void handleEvent(); // 处理事件，调用相应的回调函数

    int getFd() const { return fd; } // 获取文件描述符
    uint32_t getEvents() const { return events; } // 获取关注的事件
    Epoll* getEpoll() const { return ep; } // 获取关联的 Epoll 实例

    bool isInEpoll() const { return isadd; } // 检查是否已添加到 Epoll 实例中
    void setInEpoll(bool inEpoll) { isadd = inEpoll; } // 修改标记isadd
    void setRevents(uint32_t revt) { revents = revt; } // 设置发生的事件

private:
    std::weak_ptr<void> tie_;
    bool tied_; // 标记是否已经绑定过
    void handleEventWithGuard();

    int fd; // 文件描述符，每个channel对应唯一一个 fd
    EventLoop *loop;
    Epoll* ep; // 指向 Epoll 实例的指针，用于注册和更新事件，每个 Channel 都关联一个 Epoll 实例
    bool isadd = false; // 标记该 Channel 是否已添加到 Epoll 实例中，默认为 false未添加
    uint32_t events = 0;  // 关注的事件 (用户设置的)
    uint32_t revents = 0; // 目前发生的事件 (epoll_wait 返回的，由setRevents设置)
    std::function<void()> readCallback;  // 存储读回调函数
    std::function<void()> writeCallback; // 存储写回调函数
};