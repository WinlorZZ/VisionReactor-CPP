## Linux 文件描述符 (File Descriptor)
在代码中多次出现的 int fd，其本质是内核维护的一个索引

概念：Linux 中“一切皆文件”Socket 是文件，Epoll 实例是文件，连显示器（标准输出）也是文件

- 特殊 ID：
    - 0: 标准输入 (stdin)

    - 1: 标准输出 (stdout)

    - 2: 标准错误 (stderr)

- Socket 的返回值

    - 当你调用 socket() 或 epoll_create() 时，系统会从 3 开始寻找最小未被使用的整数；
所以我们在打印时看到 listen_fd 通常是 3，epoll_fd 是 4，第一个客户端是 5
    - 测试代码示例：
        ```c++
        // 在 main 函数开头打印
        int fd1 = socket(AF_INET, SOCK_STREAM, 0);
        int fd2 = socket(AF_INET, SOCK_STREAM, 0);
        std::cout << "FD1: " << fd1 << std::endl; // 输出 3
        std::cout << "FD2: " << fd2 << std::endl; // 输出 4
        ```

## 核心函数调用解析
1. `socket()`：向内核申请创建一个新的套接字资源
    ```c++
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    ```
    - 参数：
        - AF_INET: IPv4 地址族。
        - SOCK_STREAM: TCP 流式协议（如果是 UDP 则用 SOCK_DGRAM）。
        - 0: 使用默认协议（TCP）
    - 使用int类型的listen_fd变量来存储返回的文件描述符
1. `setsockopt()`：用于设置套接字选项
    ```c++
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    //函数定义
    extern int setsockopt (int __fd, int __level, int __optname,
    	       const void *__optval, socklen_t __optlen) __THROW;
    ```
    - 参数：套接字文件描述符，选项级别（`SOL_SOCKET`表示套接字级别），选项名称（`SO_REUSEADDR`表示允许地址复用），选项值的指针，选项值的大小
    - `SO_REUSEADDR`：必加选项，允许服务器重启后，立即使用之前被占用的端口（否则要等 2 分钟 TIME_WAIT）
    - 使用选项值的引用和大小是因为 `setsockopt()` 函数需要知道选项值存储的位置和大小，以便正确地读取和应用该选项，见原始socket.h对setsockopt的定义
    - void 指针*：这里传 &opt 是因为该函数通用性极强，可以传 int 也可以传结构体，C 语言通过 void* 实现类似“泛型”的效果

1. `bind()`：给 socket 绑定“门牌号”（IP + 端口）
    ```c++
    bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    ```
    - 参数：
        - socket的文件描述符
        - 指向sockaddr结构体的指针（c语境下）（需要强制转换为sockaddr类型）
        - 地址结构体的大小
    - 强制类型转换 (struct sockaddr*)：C 语言实现“多态”的手段
    - bind 函数是通用的，它不知道你传的是 IPv4 (sockaddr_in) 还是 IPv6 (sockaddr_in6)
        - 系统定义了一个通用的基类结构体 sockaddr。我们必须把自己的 IPv4 结构体强转为通用结构体传进去，内核再根据内部的 family 字段判断具体类型
1. fcntl() ：设置非阻塞
    ```c++
    int flags = fcntl(fd, F_GETFL, 0);          // 1. 获取原有属性
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);     // 2. 加上非阻塞标签
    ...
    if(fcntl(fd,F_SETFL,flags | O_NONBLOCK) == -1){...}
    ```
    - 参数：
        - 件描述符
        - 命令F_GETFL表示获取文件状态标志,命令F_SETFL表示设置文件状态标志
        - 第三个参数通常为0，代表不需要额外参数
    - 位运算：使用 | (OR) 操作是为了保留文件原本的属性（如读写权限），只追加 O_NONBLOCK（非阻塞） 属性
        - 意义：Epoll ET 模式下，如果 socket 是阻塞的，一个读操作没数据就会卡死整个线程，所以必须设为非阻塞

## 关键结构体解析
1. sockaddr_in：核心功能为 存储 IPv4 地址和端口
    ```C
        struct sockaddr_in {
        __SOCKADDR_COMMON (sin_);
        in_port_t sin_port;			// 端口号  
        struct in_addr sin_addr;	
        //结构体in_addr
        //成员sin_addr用于存储IPv4地址

        /* Pad to size of `struct sockaddr'.  */
        unsigned char sin_zero[sizeof (struct sockaddr)
                - __SOCKADDR_COMMON_SIZE
                - sizeof (in_port_t)
                - sizeof (struct in_addr)];
    };
    ```
    - `sin_port` 和 `sin_addr.s_addr` 必须在 *赋值时* 进行字节序转换，网络字节序是大端模式，而你的计算机（特别是 x86/x64 架构）通常是小端模式。如果不进行转换，数据在网络传输或被其他系统解析时就会出错
    - `htons()`: 用于将主机字节序的端口号 (PORT) 转换为网络字节序
    - `htonl()`: 用于将主机字节序的 IPv4 地址 (INADDR_ANY 或具体 IP) 转换为网络字节序
    ```c++
    htons(PORT);
    htonl(INADDR_ANY);
    ```
    - 变量 PORT 和 INADDR_ANY 来自哪里？

        - PORT：这是我们在 .cpp 文件开头定义的常量 (const int PORT = 8888;)。

        - INADDR_ANY：这是系统宏（通常定义为 0），包含在 <netinet/in.h> 中。它表示“本机的所有 IP 地址”。如果不绑定这个，别人通过外网 IP 访问你可能连不上
1. epoll_event
    ```c++
        struct epoll_event {
        uint32_t     events;    // 感兴趣的事件类型及触发模式 (位掩码)
        epoll_data_t data;      // 用户数据 (核心！)
    };

    // data 是一个联合体 (Union)
    typedef union epoll_data {
        void        *ptr;       // 指针 (用于 Reactor 模式存对象)
        int          fd;        // 文件描述符 (我们目前只存这个)
        uint32_t     u32;
        uint64_t     u64;
    } epoll_data_t;
    ```
    - `events` 变量: 设置感兴趣的事件，由一个或多个宏标志位（通过 | 运算组合）决定，主要包含：
        - **事件类型** (如 `EPOLLIN`, `EPOLLOUT`, `EPOLLERR`, `EPOLLHUP`):
            - `EPOLLIN`：文件描述符可读。对于监听 Socket，表示有新连接请求；对于已连接 Socket，表示有数据可读。
            - `EPOLLOUT`：文件描述符可写。表示 Socket 缓冲区有空间可写数据。
            - `EPOLLERR`：文件描述符发生错误。
            - `EPOLLHUP`：文件描述符被挂断（连接关闭）。
        - **触发模式** (核心区分点，通过 `EPOLLET` 标志):
            - **水平触发 (Level Triggered, LT)**：这是 `epoll` 的默认模式。只要文件描述符处于“就绪”状态（例如，缓冲区有数据可读），`epoll_wait()` 就会**持续不断地**报告该事件，直到你完全处理完它。即使你只读取了一部分数据，下次 `epoll_wait()` 依然会报告。
            - **边缘触发 (Edge Triggered, ET)** (`EPOLLET` 标志)：只有当文件描述符的**状态发生“边沿”变化时**（例如，从不可读变为可读），`epoll_wait()` 才会**只报告一次**该事件。一旦报告，你就必须在事件处理函数中**尽可能多地**读取或写入数据，直到 `read()` 或 `write()` 返回 `EAGAIN` 或 `EWOULDBLOCK`（表示资源暂时不可用）。如果未处理完所有数据，`epoll` 不会再次通知，直到下次状态变化。ET 模式必须与非阻塞 I/O (`O_NONBLOCK`) 配合使用，效率更高。
        - 组合使用 (`EPOLLET | EPOLLIN`)：如 `epoll_server.cpp` 中所示，表示监听文件描述符的可读事件，并采用边缘触发模式。这意味着当有新连接到来时，只通知一次，程序需要循环调用 `accept()` 直到没有新连接。对于客户端 Socket，当有数据到来时，也只通知一次，程序需要循环调用 `read()` 直到数据读完。
    - 联合体 (Union)：ptr 和 fd 共用同一块内存。你只能用其中一个

1. Epoll ET 模式核心逻辑
```c++
// 读取逻辑标准写法
while (true) {
    int n = read(fd, buf, sizeof(buf));
    if (n > 0) {
        // 读到数据，处理...
    } else if (n == 0) {
        // 客户端断开连接
        close(fd);
        break;
    } else { // n == -1
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 数据读完了，正常退出循环
            break; 
        } else {
            // 真的出错了
            perror("read error");
            break;
        }
    }
}
```
- while(true) : 在边缘触发 (ET) 模式下，`epoll_wait()` 只在文件描述符的**状态发生“边沿”变化时**报告一次事件。因此，为了确保在一次事件通知中处理完所有可读数据或可写空间（本例中为读取），我们需要在一个循环中**持续读/写**，直到 `read()` 或 `write()` 返回 `EAGAIN` 或 `EWOULDBLOCK`。



