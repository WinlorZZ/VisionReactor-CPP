#pragma once

#include "Socket.h"
#include "Channel.h"
#include <functional>
#include <string>

class Epoll;
class Socket;
class Channel;

// 接管Server传过来的ClientSocket 和对应的 Channel 
// 封装具体的业务逻辑（Echo），负责处理 TCP 的读写缓冲、业务逻辑，并在连接断开时通知上层销毁自己
class Connection{
public:
    Connection(Epoll *ep,Socket *clnt_sock);
    // ep用于创建channel，clnt_sock用于接管Server传过来的clnt_sock
    ~Connection();
    using DeleteConnectionCallback = std::function<void(Socket*)>; // 这里传 Socket* 方便 Server 找 map key

    void setDeleteConnectionCallback(std::function<void(Socket*)> cb) { deleteConnectionCallback = cb;};
private:
    Socket* clnt_sock;// 存储接受的sock
    Channel* ch;// 管理的channel
    Epoll *ep;// 受ep监视
    std::function<void(Socket*)> deleteConnectionCallback;

    // 核心业务逻辑：处理读事件 (就是以前的 handleReadEvent)
    // 为什么是 private？因为只有 Channel 会通过回调来调用它，外界不需要直接调用
    void echo(); // 替代之前的 handleReadEvent
    // 预留：将来处理半包/粘包以及写缓冲时会用到
    std::string readBuffer; 
    std::string writeBuffer;
};