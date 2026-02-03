#include <iostream>      // 输入输出流
#include <sys/socket.h>  // socket(), setsockopt(), bind(), listen(), accept()
#include <netinet/in.h>  // sockaddr_in, htonl(), htons(), INADDR_ANY
#include <arpa/inet.h>   // inet_aton(), inet_ntoa()
#include <sys/epoll.h>   // epoll_create(), epoll_ctl(), epoll_wait()
#include <fcntl.h>       // fcntl(), O_NONBLOCK
#include <cstring>       // memset()
#include <unistd.h>      // close(), read(), write()
#include <vector>        // std::vector

const int MAX_EVENTS = 1000;
const int BUFFER_SIZE = 1024;
// const int BUFFER_SIZE = 3;
const int PORT = 8888;

void setNonBlocking(int fd) {//设置非阻塞模式,形参是文件描述符
    int flags = fcntl(fd, F_GETFL, 0);
    //使用fcntl函数获取文件状态标志
    //参数是文件描述符，命令F_GETFL表示获取文件状态标志，第三个参数通常为0，代表不需要额外参数
    if(flags == -1){
        perror("fcntl F_GETFL error");//中文意为"获取文件状态标志错误"
        return;
    }
    if(fcntl(fd,F_SETFL,flags | O_NONBLOCK) == -1){
        perror("fcntl F_SETFL error");//中文意为"设置文件状态标志错误"
        return;
    }
}

int main() {
    // 0. 
    // 1. 创建监听socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    //socket：一个系统调用，实际上是一个函数，用于创建一个新的套接字，返回一个文件描述符   
    //参数分别为：AF_INET表示IPv4地址族，SOCK_STREAM表示TCP流式套接字，0表示使用默认协议
    //使用int类型的listen_fd变量来存储返回的文件描述符
    if(listen_fd == -1){
        //文件描述符是一个非负整数
        //-1表示出错，0表示标准输入，1表示标准输出，2表示标准错误输出
        perror("socket error");
        return -1;
    }

    // 2. 设置端口复用
    int opt = 1;//定义一个整型变量opt，并初始化为1，表示启用该选项，opt变量用于存储选项值；其实就是一个设置选项的开关变量
    setsockopt(listen_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    //setsockopt：用于设置套接字选项
    //参数分别为：套接字文件描述符，选项级别（SOL_SOCKET表示套接字级别），选项名称（SO_REUSEADDR表示允许地址复用），选项值的指针，选项值的大小
    //使用选项值的引用和大小是因为setsockopt函数需要知道选项值存储的位置和大小，以便正确地读取和应用该选项，见原始socket.h对setsockopt的定义：
    //extern int setsockopt (int __fd, int __level, int __optname,
    //	       const void *__optval, socklen_t __optlen) __THROW;

    // 3. 绑定地址和端口
    struct sockaddr_in server_addr;
    //sockaddr_in结构体用于表示IPv4地址和端口
    //下面这一段是C的初始化结构体经典方式
    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family = AF_INET;//设置地址族为IPv4
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);//设置地址为任意可用地址
    server_addr.sin_port = htons(PORT);//设置端口

    if( bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1){
        //bind()  :用于将套接字绑定到特定的地址和端口，返回值为整数，表示绑定操作的结果
        //参数分别为：套接字文件描述符，指向sockaddr结构体的指针（需要强制转换为sockaddr类型），地址结构体的大小
        perror("bind error");
        close(listen_fd);
        return -1;
    }

    // 4. 开始监听
    if( listen(listen_fd, SOMAXCONN) == -1){
        //listen()：用于将套接字设置为监听状态，准备接受传入的连接请求，返回值为整数，表示监听操作的结果
        //参数分别为：套接字文件描述符，最大连接队列长度（SOMAXCONN表示系统允许的最大值）
        perror("listen error");
        return -1;
    }

    // 5. 创建epoll实例
    int epoll_fd = epoll_create1(0);//epoll_create1是一个系统调用，用于创建一个新的epoll实例，并返回一个文件描述符，参数0表示不设置特殊标志
    if(epoll_fd == -1){ //创建的epoll返回-1表示失败
        perror("epoll_create1 error");
        close(listen_fd);//关闭监听socket，释放资源
        //close():用于关闭文件描述符，释放资源
        //extern int close (int __fd);
        return -1;
    }

    // 6. 将监听socket添加到epoll实例中
    struct epoll_event event;
    //struct epoll_event
    // {
    //   uint32_t events;	/* Epoll events */
    //   epoll_data_t data;	/* User data variable */
    // } __EPOLL_PACKED;
    event.events = EPOLLIN | EPOLLET; 
    //设置event的事件类型,前面一个值表示关注读事件，后面一个表示边缘触发模式
    event.data.fd = listen_fd;
    // 核心关注点：
    // EPOLLIN: 有新连接进来（对于 listen_fd 来说，读事件就是新连接）
    // EPOLLET: 边缘触发模式 (建议全员 ET，养成习惯)

    setNonBlocking(listen_fd); //设置非阻塞模式

    // 将监听socket添加到epoll实例中
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event) == -1){
        perror("epoll_ctl error");
        return -1;
    }

    // 准备就绪事件数组
    std::vector<struct epoll_event> events(MAX_EVENTS);//创建一个epoll_event类型的向量/数组，大小为MAX_EVENTS，用于存储就绪事件
    std::cout <<"Epoll 服务器启动，监听端口 " << PORT << std::endl;

    // 7. 读取和处理事件循环
    while(true){

        int n_fds = epoll_wait(epoll_fd,events.data(),MAX_EVENTS,-1);
        //epoll_wait：等待事件发生，参数是epoll实例的文件描述符，就绪事件数组首地址（.data指向数组第一个元素），最大事件数，超时时间(-1表示无限等待)
        //返回值是就绪事件的数量，-1表示出错，比如被信号中断、资源临时不可用等
        if(n_fds == -1){
            perror("epoll_wait error");
            break;
        }
        // 处理就绪事件
        for(int i = 0;i < n_fds;i++){
            int current_fd = events[i].data.fd;//获取就绪事件对应的文件描述符
            if(current_fd == listen_fd){//情况a：监听socket有新连接进来
                //TODO: 接受新连接，添加到epoll实例中
                while(true){
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    //socklen_t是一种无符号整数类型，专门用于表示套接字地址结构的长度
                    int conn_fd = accept(current_fd, (struct sockaddr*)&client_addr, &client_len);
                    //accept()：接受传入的连接请求，返回一个新的套接字文件描述符，用于与客户端通信
                    //参数分别为：监听套接字文件描述符，指向存储客户端地址信息的sockaddr结构体的指针，指向地址长度的指针
                    //使用强制类型转换将sockaddr_in结构体指针转换为sockaddr类型
                    //见原始socket.h对accept的定义：
                    //extern int accept (int __fd, __SOCKADDR_ARG __addr,
                    //		   socklen_t *__restrict __addr_len);

                    if(conn_fd == -1){//accept返回-1表示出错,以下根据出错误原因进行处理
                        if(errno == EAGAIN || errno == EWOULDBLOCK){
                            //EAGAIN和EWOULDBLOCK表示当前没有更多的连接可接受（非阻塞模式下）
                            break;//跳出循环，停止接受新连接
                        }
                        else{
                            perror("accept error");
                            break;
                        }
                    }
                    //if之后表明接受到新连接，准备将其添加到epoll实例中

                    // 设置新连接为非阻塞模式
                    setNonBlocking(conn_fd);
                    // 将新连接添加到epoll实例中
                    struct epoll_event conn_event;
                    conn_event.events = EPOLLIN | EPOLLET; //关注读事件，边缘触发模式
                    conn_event.data.fd = conn_fd;
                    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &conn_event) == -1){
                        perror("epoll_ctl添加新连接错误");
                        close(conn_fd);
                    }

                    std::cout << "接受新连接，客户端IP: "
                              << inet_ntoa(client_addr.sin_addr)
                              << ", 端口: " << ntohs(client_addr.sin_port)
                              << ", 分配的文件描述符: " << conn_fd << std::endl;
                }
            }
            else if(events[i].events & EPOLLIN){//情况b：已有连接有数据可读写
                //TODO: 处理已有连接的数据读写
                bool close_conn = false;//标志位，表示是否关闭连接
                while(true){
                    char buf[BUFFER_SIZE];
                    ssize_t n = read(current_fd, buf, sizeof(buf));
                    //read()：从文件描述符中读取数据，参数分别为：文件描述
                    //符，存储数据的缓冲区，读取的最大字节数
                    //返回值是实际读取的字节数，-1表示出错，0表示连接关闭
                    if(n > 0){
                        std::cout << "收到分片: " << std::string(buf, n) << " (" << n << " bytes)" << std::endl;
                        write(current_fd, buf, n);
                        //write()：向文件描述符写入数据，参数分别为：文件描述符，存储数据的缓冲区，写入的字节数
                    }else if(n == 0){
                        close_conn = true;//客户端关闭连接
                        break;
                    }else{//n == -1
                        if(errno == EAGAIN || errno == EWOULDBLOCK){
                            //表示当前没有更多数据可读（非阻塞模式下）
                            break;//跳出读取循环，等待下一次事件通知
                        }else{
                            // 真的出错了 (如 Connection reset by peer,即连接被对方重置)
                            perror("read error");
                            close_conn = true;
                            break;
                        }
                    }
                }
                if(close_conn){
                    // 从 Epoll 树上移除 (虽然 close 会自动移除，但显式调用是好习惯)
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, current_fd, nullptr);
                    close(current_fd);
                    std::cout << "Client disconnected: FD=" << current_fd << std::endl;
                }
            }
        }
    }
    // 8. 清理资源
    close(epoll_fd);
    close(listen_fd);
    return 0;
}