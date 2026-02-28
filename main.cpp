#include "EventLoop.h"
#include "Server.h"

int main(){
    // 1. 创建事件循环 (主线程的心脏)
    EventLoop loop;
    // 2. 创建服务器 (拥有 Acceptor 和 ThreadPool)
    Server server(&loop);
    // 3. 启动事件循环 (开始 epoll_wait)
    // 程序将阻塞在这里，直到服务器停止
    loop.loop();
    return 0;
}

