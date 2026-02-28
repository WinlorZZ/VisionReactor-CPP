#include "EventLoop.h"
#include "Epoll.h"
#include "Channel.h"
#include <vector>

EventLoop::EventLoop() : ep(nullptr), quit(false) {
    ep = new Epoll(); // EventLoop 创建时，顺便把 Epoll 也创建了
}

EventLoop::~EventLoop() {
    delete ep;
}

void EventLoop::loop() {
    while (!quit) {
        std::vector<Channel*> chs;
        
        // 1. 调用 Epoll 等待事件发生
        // poll 会阻塞，直到有事件发生，然后把发生事件的 Channel 填入 chs
        chs = ep->poll(); 
        
        // 2. 遍历所有发生事件的 Channel，让它们处理事件
        for (auto it = chs.begin(); it != chs.end(); ++it) {
            (*it)->handleEvent(); // 这一步就是 Reactor 的分发逻辑
        }
    }
}

void EventLoop::updateChannel(Channel *ch) {
    // EventLoop 不直接操作 epoll 系统调用，而是委托给 Epoll 类
    ep->updateChannel(ch);
}