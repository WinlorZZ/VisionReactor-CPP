#pragma once
#include <functional>
#include <vector>

class Epoll;    // 前置声明：EventLoop 拥有 Epoll
class Channel;  // 前置声明：EventLoop 操作 Channel

class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    // 核心功能：开始事件循环 (死循环)，主进程调用这个函数以开始监听等行为
    void loop();
    
    // 核心功能：更新通道 (其实是调用 Epoll->updateChannel)
    void updateChannel(Channel *ch);

private:
    Epoll *ep; // 真正干活的 Epoll 实例
    bool quit; // 停止标志
};