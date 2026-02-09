#include "Connection.h"
#include "Socket.h"
#include "Channel.h"
#include <iostream>
#include <functional> //std::bind
#include <unistd.h> // read, write, close
#include <cstring>  // memset, errno



Connection::Connection(Epoll *ep,Socket *clnt_sock) : ep(ep), clnt_sock(clnt_sock){
    ch = new Channel( ep,clnt_sock->fd() );
    // 使用std::bind设置好管理的channel对象的读函数
    ch->setReadCallback( std::bind(&Connection::echo,this) );
    // 开启读事件监听
    ch->enableReading();
}

Connection::~Connection(){// 在server使用delete connn时触发
    delete clnt_sock;//从系统内核->Acceptor->Server->Connection->销毁
    delete ch;// 销毁自己持有的内容
}

void Connection::echo(){
    char buf[1024];
    while(true) {
        memset(buf, 0, sizeof(buf));
        ssize_t bytes_read = read(clnt_sock->fd(), buf, sizeof(buf));
        // 根据bytes数量判断是否继续，一直读完全部
        if(bytes_read > 0 ) {
            std::string msg(buf, bytes_read);
            std::cout << "Message from client fd " << clnt_sock->fd() << ": " << msg << std::endl;// 显示信息
            write(clnt_sock->fd(), buf, bytes_read);// 写回客户端，因此我们收到的信息和我们发送的一直，这里可以改动变成其他内容？
        } else if(bytes_read == -1 && errno == EINTR){// errno作为全局变量，在read返回-1时就存好了
            continue; // 信号中断，重试
        } else if(bytes_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)){
            break; // 数据读完了，退出循环等待下一次事件
        } else if(bytes_read == 0){
            // 对于read函数的返回值来说，== 0代表对端关闭连接，只有 > 0 时代表读到的字节
            std::cout << "EOF, client fd " << clnt_sock->fd() << " disconnected" << std::endl;
            // 读完数据之后，通知管理者Server销毁
            if(deleteConnectionCallback){
                deleteConnectionCallback(clnt_sock);
            }
            // 重要：一旦调用了回调，Server 可能会立即 delete this
            // 所以回调之后千万不要再访问任何成员变量了
            break;
        } else {
             // 其他错误情况
             perror("read error");
             if(deleteConnectionCallback) deleteConnectionCallback(clnt_sock);
             break;
        }
    }
}

