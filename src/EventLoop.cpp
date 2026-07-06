#include "EventLoop.h"
#include "Epoll.h"
#include "Channel.h"
#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <sys/eventfd.h>
#include <unistd.h>
#include <vector>

EventLoop::EventLoop()
    : ep(new Epoll()),
      quit_(false),
      loopThreadId_(std::this_thread::get_id()),
      wakeupFd_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)),
      wakeupChannel_(nullptr) {
    if (wakeupFd_ < 0) {
        delete ep;
        throw std::runtime_error("eventfd create failed");
    }
    wakeupChannel_ = new Channel(this, wakeupFd_);
    wakeupChannel_->setReadCallback(
        std::bind(&EventLoop::handleWakeupRead, this));
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop() {
    delete wakeupChannel_;
    ::close(wakeupFd_);
    delete ep;
}

void EventLoop::loop() {
    while (!quit_) {
        std::vector<Channel*> chs;
        
        // 1. 调用 Epoll 等待事件发生
        // poll 会阻塞，直到有事件发生，然后把发生事件的 Channel 填入 chs
        chs = ep->poll(); 
        
        // 2. 遍历所有发生事件的 Channel，让它们处理事件
        for (auto it = chs.begin(); it != chs.end(); ++it) {
            (*it)->handleEvent(); // 这一步就是 Reactor 的分发逻辑
        }
        doPendingFunctors();
    }
}

void EventLoop::quit() {
    quit_.store(true);
    if (!isInLoopThread()) {
        wakeup();
    }
}

void EventLoop::updateChannel(Channel *ch) {
    // EventLoop 不直接操作 epoll 系统调用，而是委托给 Epoll 类
    ep->updateChannel(ch);
}

void EventLoop::removeChannel(Channel *ch) {
    ep->removeChannel(ch);
}

bool EventLoop::isInLoopThread() const {
    return std::this_thread::get_id() == loopThreadId_;
}

void EventLoop::runInLoop(std::function<void()> cb) {
    if (isInLoopThread()) {
        cb();
    } else {
        queueInLoop(std::move(cb));
    }
}

void EventLoop::queueInLoop(std::function<void()> cb) {
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingFunctors_.push_back(std::move(cb));
    }
    wakeup();
}

// 系统调用read和write的参数
// read(fd, buffer, size);
// write(fd, buffer, size);
// 操作对象的fd， 数据地址，操作数据的大小
void EventLoop::wakeup() {
    const uint64_t one = 1;
    ssize_t n;
    do {
        n = ::write(wakeupFd_, &one, sizeof(one));
    } while (n < 0 && errno == EINTR);
}

void EventLoop::handleWakeupRead() {
    uint64_t counterValue = 0;

    while (true) {
        const ssize_t bytesRead =
            ::read(wakeupFd_, &counterValue, sizeof(counterValue));

        if (bytesRead == static_cast<ssize_t>(sizeof(counterValue))) {
            // 成功读取并清零了当前计数；继续读取，排空期间新到达的唤醒。
            continue;
        }

        if (bytesRead < 0) {
            if (errno == EINTR) {
                // 系统调用被信号打断，本次没有完成读取，重新尝试。
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 非阻塞 eventfd 已经没有数据，本轮排空完成。
                return;
            }
        }

        // eventfd 正常读取固定为 8 字节；其他返回值或 errno 均视为异常并退出。
        return;
    }
}

void EventLoop::doPendingFunctors() {
    std::vector<std::function<void()>> functors;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        functors.swap(pendingFunctors_);
    }
    for (auto &functor : functors) {
        functor();
    }
}
