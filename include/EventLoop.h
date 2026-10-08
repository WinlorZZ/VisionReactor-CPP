#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class Epoll;    // 前置声明：EventLoop 拥有 Epoll
class Channel;  // 前置声明：EventLoop 操作 Channel

class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    // 核心功能：开始事件循环 (死循环)，主进程调用这个函数以开始监听等行为
    void loop();
    void quit();
    
    // 核心功能：更新通道 (其实是调用 Epoll->updateChannel)
    void updateChannel(Channel *ch);
    void removeChannel(Channel *ch);

    // 跨线程操作投递回 EventLoop，避免并发修改 Channel/epoll。
    void runInLoop(std::function<void()> cb);
    void queueInLoop(std::function<void()> cb);
    bool isInLoopThread() const;

private:
    void wakeup();
    void handleWakeupRead();
    void doPendingFunctors();

    Epoll *ep; // 真正干活的 Epoll 实例
    std::atomic<bool> quit_; // 停止标志
    const std::thread::id loopThreadId_;
    int wakeupFd_;
    Channel *wakeupChannel_;
    std::mutex pendingMutex_;
    std::vector<std::function<void()>> pendingFunctors_;
};
