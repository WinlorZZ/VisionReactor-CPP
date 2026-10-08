#include "InetAddress.h"

#include <arpa/inet.h>
#include <cstring>

InetAddress::InetAddress(const char* ip, uint16_t port){
	std::memset(&addr,0 , sizeof(sockaddr_in));//初始化结构体变量addr，全部置0
    addr.sin_family = AF_INET;//设置地址族为IPv4

    addr.sin_port = htons(port);//设置端口
    // 将字符串 IP 转换为网络字节序
    // addr.sin_addr.s_addr = inet_addr(ip);
    inet_pton(AF_INET, ip, &addr.sin_addr);//将点分十进制的IP地址字符串转换为网络字节序的二进制形式，并存储在addr.sin_addr中
}
