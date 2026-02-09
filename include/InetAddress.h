#pragma once
#include <arpa/inet.h>

//封装IPv4地址和端口的类，自动化地址结构体的创建和管理
class InetAddress {
public:
    InetAddress() = default;
    ~InetAddress() = default;
    InetAddress(const char* ip, uint16_t port);
    
    //提供获取地址结构体的公共函数，返回sockaddr_in结构体
    const struct sockaddr* getAddr() const { return reinterpret_cast<const struct sockaddr*>(&addr); }
    //提供IP和端口号的方法
    const char* getIP() const { return inet_ntoa(addr.sin_addr); }// inet_ntoa：将网络地址转换成“.”点隔的字符串格式
    const short int getPort() { return addr.sin_port; };// 
    //提供获取地址结构体长度的公共函数，返回socklen_t类型
    socklen_t getAddrLen() const { return sizeof(addr); }

    //设置地址结构体，接收来自外部的sockaddr_in结构体参数，并赋值给类的成员变量addr
    void setAddr(struct sockaddr_in _addr) { addr = _addr; }

private:
    struct sockaddr_in addr;//使用结构体sockaddr_in存储IPv4地址和端口
};

// struct sockaddr_in
//   {
//     __SOCKADDR_COMMON (sin_);
//     in_port_t sin_port;			/* Port number. in_port_t = short int  */
//     struct in_addr sin_addr;		/* Internet address.  */

//     /* Pad to size of `struct sockaddr'.  */
//     unsigned char sin_zero[sizeof (struct sockaddr)
// 			   - __SOCKADDR_COMMON_SIZE
// 			   - sizeof (in_port_t)
// 			   - sizeof (struct in_addr)];
//   };

