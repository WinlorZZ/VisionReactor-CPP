#pragma once


class InetAddress; // 前向声明InetAddress类，告知编译器该类存在

class Socket{
    public:
        Socket();
        ~Socket();
        Socket(int fd);// 静态多态，用于包装已有的 fd (比如 accept 返回的)
        
        //禁用拷贝
        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;

        void bind(const InetAddress& addr);
        void listen();
        void setNonBlocking();

        int accept(InetAddress& client_addr);//有客户端连接时调用，返回新的连接socket的文件描述符
        int accept();//重载accept方法，当不需要获取客户端地址时使用
        int fd() const{ return _fd; }
    private:
        int _fd;//绑定的socket文件描述符
};
