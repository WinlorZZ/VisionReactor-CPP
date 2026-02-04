#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <cstring>
#include <unistd.h>     // read, write, close
#include <fcntl.h>      // fcntl
#include <errno.h>      // errno, EAGAIN, EWOULDBLOCK
#include <sys/socket.h> // optionally for socket functions

#define BUFFER_SIZE 1024

// 简单的回显处理函数，用于处理读事件
void handleReadEvent(int fd) {

}

// 简单的错误检查工具：如果 condition 为真，打印错误并退出
inline void errif(bool condition, const char *errmsg) {
    if (condition) {
        perror(errmsg);
        exit(EXIT_FAILURE);
    }
}

// 设置文件描述符为非阻塞模式的函数
inline void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 处理读事件的函数
inline void handleReadEvent(int fd, int epoll_fd) {
    char buf[BUFFER_SIZE];//缓冲区
    while (true) { // 由于是 ET (边缘触发) 模式，必须一次性把数据读完
        memset(buf, 0, BUFFER_SIZE);// 清空缓冲区
        ssize_t n = read(fd, buf, sizeof(buf));//从文件描述符中读取数据
        if (n > 0) {
            std::cout << "收到分片: " << std::string(buf, n) << " (" << n << " bytes)" << std::endl;
            write(fd, buf, n); // Echo 回显
        } else if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 数据读完了，缓冲区已空，等待下次通知
                break;
            } else {
                // 真的出错了
                perror("read error");
                close(fd); // 记得关闭
                // Epoll 会自动移除关闭的 fd，但手动移除是更保险的习惯
                // epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr); 
                break;
            }
        } else if (n == 0) {
            // 客户端断开连接 (EOF)
            std::cout << "EOF, client(fd " << fd << ") disconnected" << std::endl;
            close(fd); 
            break;
        }
    }
}

#endif