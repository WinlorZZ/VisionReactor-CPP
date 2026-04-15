#include <iostream>
#include "Channel.h"
#include "Epoll.h"
#include "EventLoop.h"

Channel::Channel(EventLoop *loop, int fd) 
    : loop(loop), fd(fd), events(0), revents(0), isadd(false), tied_(false) { };// 构造函数, 初始化 fd 和 loop 指针

Channel::~Channel() {
    // 析构函数暂时留空
    // 在这里处理从 Epoll 中自动移除的逻辑
}

void Channel::tie(const std::shared_ptr<void>& obj){
    tie_ = obj;
    tied_ = true;
}

// 启用读事件监听，将事件添加到 epoll 实例中
void Channel::enableReading(){
    events |= EPOLLIN | EPOLLET; 
    // 关注读事件，边缘触发模式，|=为位或赋值运算符，即 events = events | (EPOLLIN | EPOLLET)
    loop->updateChannel(this);//每次关注事件的变动都要更新channel
}

void Channel::enableWriting(){
    events |= EPOLLOUT;
    loop->updateChannel(this);
}

// 关闭写事件监听 (消除 CPU 炸弹)
void Channel::disableWriting(){
    events &= ~EPOLLOUT; // 擦除写标签，保留读标签
    loop->updateChannel(this);
}

// 关闭读事件监听
void Channel::disableReading(){
    events &= ~EPOLLIN;
    loop->updateChannel(this);
}
  
bool Channel::isWriting() const {
    return events & EPOLLOUT;
}

void Channel::handleEvent() {
    // 处理事件，调用相应的回调函数
    if(tied_){
        std::shared_ptr<void> guard = tie_.lock();
        if(guard){
            handleEventWithGuard();
        }else{
        // 提升失败，对象已销毁，安静地丢弃事件，完美避免段错误
        // std::cout << "[Channel] 探测到 Connection 已死亡，忽略本次幽灵事件" << std::endl;
        }    
    }else{
        handleEventWithGuard();
    }
}

void Channel::handleEventWithGuard(){
    if (revents & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
        // 有读事件发生
        // EPOLLIN ：普通数据可读
        // EPOLLPRI: 高优先级数据可读
        // EPOLLRDHUP: 对端关闭连接或者半关闭
        if (readCallback) {
            readCallback(); // 执行通过 setReadCallback 设置的 Lambda
        }
    }
    if( revents & EPOLLOUT ) {
        // 有写事件发生
        // // EPOLLOUT: 缓冲区可写
        if (writeCallback) {
            writeCallback(); // 执行写事件回调
        }
    }
}

// 设置读事件回调函数，回调的函数使用lambda表达式返回
void Channel::setReadCallback(std::function<void()> cb) {
    readCallback = cb;
}

// 设置写事件回调函数
void Channel::setWriteCallback(std::function<void()> cb) {
    writeCallback = cb;
}
