## Linux 文件描述符 (File Descriptor)
在代码中多次出现的 int fd，其本质是内核维护的一个索引

概念：Linux 中“一切皆文件”Socket 是文件，Epoll 实例是文件，连显示器（标准输出）也是文件

- 特殊 ID：
    - 0: 标准输入 (stdin)

    - 1: 标准输出 (stdout)

    - 2: 标准错误 (stderr)

- Socket 的返回值

    - 调用 socket() 或 epoll_create() 时，系统会从 3 开始寻找最小未被使用的整数；
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
    - 套接字资源是什么？  
        客户端与服务器之间通信时，要进行连接；将连接这一行为抽象为一个实体，对于任一方（客户端或服务端），这个连接默认绑定的是自己，所以他需要知道这个连接的对方是谁，如何连接
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
    
1. `listen()`：开启监听模式
    ```c++
    listen(listen_fd, SOMAXCONN);
    ```
    - 参数 SOMAXCONN ： 系统内核允许的最大挂起连接队列长度（Backlog），

1. `epoll_create1(0)`
    - epoll_create1是一个系统调用，用于创建一个新的epoll实例，并返回一个文件描述符，参数0表示不设置特殊标志
    - epoll是监听过程的抽象，epoll是监听器，他有监听的对象，该对象用fd标识（可以监听其他监听器）和数量，有通知的对象，有行为模式，有标识牌epfd；  
    更多内容见[I/O多路复用](https://xiaolincoding.com/os/8_network_system/selete_poll_epoll.html#epoll)

1. `fcntl()` ：设置非阻塞
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
1. `epoll_ctl()` : 将指定的socket对象添加到对应的epoll实例中，以便监听
    ```c++
    int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
    ```
    - 参数 ：
        - `epfd` : 指定的epoll实例的fd文件描述符，由它管理某个socket对象
        - `op` : 事件行为模式
            - EPOLL_CTL_ADD ：事件注册/添加，例如连接进入
            - EPOLL_CTL_MOD ：事件发生修改，例如连接进行读写
            - EPOLL_CTL_DEL ：事件删除/移除，例如连接断开
        - `fd` ： 指定的被管理的socket对象（文件描述符）
        - `event` : 指定的struct epoll_event *对象，表示具体的事件细节，epfd根据该事件的行为来管理socket对象（事件抽象为一个实体，根据这个实体的动作来进行，比如新增一个事件，事件有变动，事件消失）
    - 返回值 ：默认返回0，返回-1代表出错
1. `epoll_wait()` :  等待事件发生
    ```c++
    int epoll_wait(int epfd, struct epoll_event *events, 
                        int maxevents, int timeout);
    ```
    - `epfd` : epoll实例的文件描述符
    - `events` : 就绪事件数组首地址（.data指向数组第一个元素）
    - `maxevents` : 最大事件数
    - `timeout` : 超时时间(-1表示无限等待)
    - 返回值`numFDs` ：就绪事件的数量，-1表示出错，比如被信号中断、资源临时不可用等


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
1. epoll_event ： 结构体epoll_event，用于存放某类事件和相关数据
    - 如果不用这个结构体会是什么样？  
        会导致接口写的很长，事件以及相关数据都需要专门的参数接受（也因此不能只返回事件fd，因为很多数据并不存这个socket/连接里），而如果集合为一个事件结构体会好得多也更灵活
    ```c++
        struct epoll_event {
        uint32_t     events;    // 感兴趣的事件类型及触发模式 (位掩码)
        epoll_data_t data;      // 用户数据
    };
    
    // data 是一个联合体 (Union)，即特殊的结构体，其所有数据成员互斥共享同一块内存
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


## Epoll ET 模式核心逻辑
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
- while(true) : 在边缘触发 (ET) 模式下，`epoll_wait()` 只在文件描述符的**状态发生“边缘”变化时**报告一次事件。边缘变化包括：
    - 从不可读变为可读 - 如客户端发送数据到达、监听 socket 有新连接请求
    - 从可读变为不可读 - 如缓冲区数据被读完
    - 从不可写变为可写 - 如 socket 缓冲区有空间可写
    - 从可写变为不可写 - 如发送缓冲区满了
    - 连接关闭 - 客户端断开连接（read() 返回 0）
    - 错误发生 - EPOLLERR 或 EPOLLHUP 事件
- 相比之下，默认的LT水平触发模式下，只要文件描述符处于"就绪"状态（例如缓冲区有数据可读），就会持续通知  
因此，为了确保在一次事件通知中处理完所有可读数据或可写空间（本例中为读取），我们需要在一个循环中**持续读/写**，直到 `read()` 或 `write()` 返回 `EAGAIN` 或 `EWOULDBLOCK`。

## 从面向过程到面向对象

1. 类头文件封装
    - 对Socket的封装`Socket.h`:对应实现socket的各类行为
    1. 使用默认的构造和析构  
        - 对于析构，在 RAII 模式下，将所有的`close(fd)`放到析构函数中，永远不要在类的成员函数里手动 close 那个被托管的资源（除非同时把 _fd 重置为 -1），否则会导致两次关闭同一个文件而发生错误
    1. 禁用拷贝构造
    1. 提供Socket的行为方法
        - `void bind(InetAddress* addr);` ：传入地址类的实例  
            ```c++
            void Socket::bind(const InetAddress& addr){ 
                ... 
                if( ::bind(fd,addr.getAddr(),addr.getAddrLen()) == -1 ){ ... } }
            ```
            封装成员函数`bind`时，使用系统调用的`bind()`需要指定命名空间  
            使用作用域解析运算符 `::` 来明确指定全局命名空间中的 `bind()`，其他系统调用如 `listen()`、`accept()` 等也可能有类似的问题
        - `void listen();` ： 开启监听
        - `void setNonBlocking();` : 设置非阻塞方式
        - `int accept(InetAddress& client_addr);` ： 提供接受方法
        - `int fd(){}` : 提供返回fd的方法，简单构造

    1. 构造类InetAddress，方便存储地址
        ```c++
        class InetAddress {
        public:
            InetAddress() = default;//默认构造方式，无参数
            InetAddress(const char* ip, uint16_t port);//静态多态构造方式，传入ip和端口参数，用于强制转化
            ~InetAddress() = default;
        
            struct sockaddr_in getAddr() const { return addr; }
            void setAddr(struct sockaddr_in _addr) { addr = _addr; }
        
        private:
            struct sockaddr_in addr{};
        };
        ```
    - 对Epoll的封装`Epoll.h`：对应实现监察者的功能，包括添加监察对象，准备就绪事件等
        - 私域里维护就绪数组，用于存储就绪事件
            ```c++
            private:
                int epoll_fd;// epoll实例的文件描述符
                // 把它变成成员变量，避免每次 poll 都重新分配内存
                std::vector<struct epoll_event> events;
            ```
        - 在构造函数中，使用参数列表初始化events就绪数组并创建epoll实例
            ```c++
            Epoll::Epoll() :  epoll_fd(-1), events(1024)
            ...
            epoll_fd = epoll_create1(0);
            ...
            ```
        - 在析构函数中，关闭本监察者
        - 功能函数
            - `void addFd(int fd, uint32_t events);` : 用于向epoll实例中添加新的文件描述符fd及其对应的事件
            - `std::vector<epoll_event> poll(int timeout = -1);` : 用于等待事件发生，并返回实际就绪的事件列表

## P3阶段记录

1. 各级封装的关系图
   - 总服务的持有关系![Gemini_Generated_Image_t4gw1xt4gw1xt4gw-1770368576586-3](./Record.assets/Gemini_Generated_Image_t4gw1xt4gw1xt4gw-1770368576586-3.png)
   - `Acceptor` 的回调链![Gemini_Generated_Image_t4gw1xt4gw1xt4gw](./Record.assets/Gemini_Generated_Image_t4gw1xt4gw1xt4gw.png)
   - `Connection` 的回调链![Gemini_Generated_Image_t4gw1xt4gw1xt4gw-1770368711102-6](./Record.assets/Gemini_Generated_Image_t4gw1xt4gw1xt4gw-1770368711102-6.png)

1. 语法
- [c++11语法：`auto`的使用](https://zhuanlan.zhihu.com/p/670102303)  
    - 对于某些较长或较奇怪的数据类型，可交给编译器自行推导，这样使代码更简洁  
      示例代码
        ```c++
        int main()
        {
            int a = 8;
            char b = 'x';
            auto* c = &a;
            auto& d = b;
        
            std::cout << "a typename is:" << typeid(a).name() << std::endl;
            std::cout << "b typename is:" << typeid(b).name() << std::endl;
            std::cout << "c typename is:" << typeid(c).name() << std::endl;
            std::cout << "d typename is:" << typeid(d).name() << std::endl;
        }
        
        // 输出
        /*
        a typename is:int
        b typename is:char
        c typename is:int *
        d typename is:char
        */
        ```
- [C++11语法：`std::function`与`std::bind`](https://zhuanlan.zhihu.com/p/381639427)  
    示例代码
    
    ```c++
    int f(int,char,double);
    auto reflect = std::bind(f,_3,_2,_1);     //翻转参数顺序
    int result = reflect(2.2,'c',10);    // f(10,'c',2.2); 
    ```
- lambda表达式的进一步补充
    ```c++
    servChannel->setReadCallback( [&]() {} );
    ```
    - 参数
        - `[&]` : 捕获上下文变量，按引用
        - `()` : 传入变量，本函数无
        - `{}` : 函数体
    - lambda表达式和传统的函数参数一样，都要在成员函数中声明
    ```c++
    void setReadCallback(std::function<void()> cb);
    ```
    - 参数`cb` : 类型为std::function，参数的返回类型由function指定`void()`
    - `setReadCallback()` : 只是设置回调，参数 cb 是临时的，必须用一个成员变量来保存这个回调函数，之后在事件处理时才能调用它
    ```c++
    private:
    ...
    std::function<void()> readCallback;  // 存储回调函数
    ```
- 逻辑运算与位运算
    ```c++
    if (x > 5 && y < 10)  // && = 逻辑与（AND）
    if (x > 5 || y < 10)  // || = 逻辑或（OR）
        
    if (revents & (EPOLLIN | EPOLLPRI | EPOLLRDHUP))// 位运算
    ```
    - 位运算用于检查二进制位
    - `|` = 位或（多个位合并）
    - `&` = 位与（检查是否包含某位）
      
    
    **为什么使用位运算？**
        标志位通常用二进制表示多个开关：
    ```c++
    EPOLLIN   = 0b0001 (读事件)
    EPOLLPRI  = 0b0010 (高优先级)
    EPOLLRDHUP= 0b0100 (连接关闭)
        
    revents = 0b0101  (同时有读和关闭事件)
        
    revents & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)
    = 0b0101 & 0b0111
    = 0b0101  ← 非零，说明包含了其中某个标志
    ```
    用位运算可以高效地检查多个标志位，是系统编程的常用做法


### 问题释义

#### Q1: 关于 `read()`和`write()` 函数返回值的含义

`read()`返回值：特别是在非阻塞（Non-blocking）模式下，需要严格区分“对端关闭”和“暂时没数据”

在 TCP 协议和 POSIX 标准中定义：

1. **`read()返回 > 0`**：成功读取到 N 个字节的数据。
2. **`read()返回 == 0`**：**EOF (End of File)**。在 TCP 层面，这表示对端发送了 `FIN` 包，明确告知“我不会再给你发任何数据了”。这是一个不可逆的状态，因此我们认定为 **“断开连接”**。
3. **`read()返回 == -1`**：出错了。此时需要检查全局变量 `errno`

**“读完数据” (缓冲区空) 的表现：**
在非阻塞模式（特别是我们使用的 ET 边缘触发模式）下，当我们把内核读取缓冲区的数据全部读完后，`read()` **不会返回 0，而是返回 -1**，并且操作系统会将 `errno` 设置为 **`EAGAIN`** (或 `EWOULDBLOCK`)。这代表“现在没数据，但连接还在，你可以待会儿再来试”

`write()`的返回值是一个有符号长整型ssize_t，对于当前项目，有以下含义

- `write()返回 > 0`：表示内核接受的字节数，并放入TCP发送缓冲区
  - 返回值 `== msg.size()`：请求发送的数据全部接受，此时`remaining==0`
  - 返回值  `< msg.size()`：请求发送的数据部分接受，此时`remaining == msg.size() - nwrote`、
- `write()返回 == -1：此时同样检查错误码
  - `errno == EAGAIN`  或者 `Q2: 关于系统调用的错误码 `errno` :在非阻塞模型中这不算错误，而是代表系统当前没有资源处理写入
  - 其他错误码包括：
    - `EPIPE`：客户端突然崩溃
    - `ECONNRESET`：对端重置了连接

这是 Unix/Linux 系统编程的标准机制：

1. 当一个系统调用（如 `read`, `write`, `accept`）失败时，它通常返回 `-1` 来标识失败
2. **与此同时**，操作系统内核会修改当前线程的一个全局整型变量 —— **`errno`**
3. `errno` 中存储了一个预定义的常量（如 `EAGAIN`, `EINTR` 等），用于指示具体的失败原因

**注意**：只有当函数返回指示失败的值（通常是 -1）时，读取 `errno` 才有意义。如果函数执行成功，`errno` 的值是未定义的（它可能残留了之前的错误码）

本项目中`errno`的类型：

- `EAGAIN` (Try again) / `EWOULDBLOCK` (Operation would block)
  - `read()`时出现：底层的 TCP 接收缓冲区已经读空了
  - `write()`时出现：底层的 TCP 发送缓冲区已经塞满了
  - 在大多数 Linux 系统中，这两个宏的值是相等的，但为了跨平台兼容性，严谨的代码会把两个都写上：`errno == EAGAIN || errno == EWOULDBLOCK`
- `EINTR` (Interrupted system call)
  - 当你的线程正在阻塞调用（或者甚至是非阻塞调用在内核态准备数据的瞬间），突然操作系统收到了一个外部信号（Signal，比如你在终端按了 Ctrl+C 产生的 `SIGINT`，或者子进程退出的 `SIGCHLD`）。内核为了去处理这个优先级更高的信号，强行打断了你的 `read` 或 `write`
  - 此时重新进行即可，`if (errno == EINTR) { continue; }` ，直接进入下一次 while 循环，重新调用 read/write
- `EPIPE` (Broken pipe)
  - 最容易在**写 (`write`)** 的时候发生
  - 在 Linux 下，往一个触发了 `EPIPE` 的 Socket 写数据，内核默认会给你的进程发送一个 `SIGPIPE` 信号，这个信号的默认行为是直接杀死整个进程
  - 需要在 `main` 函数的最开头加上一行代码忽略它：`signal(SIGPIPE, SIG_IGN);`防止程序崩溃
- `ECONNRESET` (Connection reset by peer)
  - 对端（客户端）硬重置了连接
- `EMFILE` (Too many open files) / `ENFILE`
  - 在主线程调用 `accept()` 接收新玩家连接时。Linux 系统对每个进程能打开的“文件描述符 (fd)”是有上限的（默认通常是 1024），超出这个上线时，`accept` 就会返回 -1，并报 `EMFILE`

​	


#### Q3: 关于 `accept` 时客户端地址信息的来源

**操作系统内核 (OS Kernel)** 在 `accept` 系统调用发生时写入内存

这是一个典型的 **“传出参数 (Out Parameter)”** 的用法。

1. **准备空表**：在 C++ 层，你 `new InetAddress()`，这相当于申请了一张空白的表格（内存空间）。
2. **传入地址**：调用 `listenSock->accept(clntAddr)` 时，你实际上是把这张空白表格的 **内存地址** 告诉了操作系统。
3. **内核填充**：当有客户端连接时，TCP 三次握手完成，内核从网络包头中提取出客户端的 IP 和端口号，然后**直接把这些数据写到了你提供的那个内存地址上**。
4. **读取结果**：系统调用返回后，你再去读取那个 `InetAddress` 对象，里面就有了数据。

### 二、 架构与内存管理 (Architecture & Memory)

#### Q4: 关于 Socket 的所有权转移与生命周期

Socket 指针就像一个接力棒：

1. **生产 (Acceptor)**：`Acceptor` 调用 `accept()` 创建了 `clntSock`。此时它是所有者。
2. **转移 1 (Acceptor -> Server)**：通过 `newConnectionCallback(clntSock)`，`Acceptor` 将所有权交给了 `Server`。`Acceptor` 以后不再负责它。
3. **转移 2 (Server -> Connection)**：`Server` 收到后，立即调用 `new Connection(..., clntSock)`，将所有权最终交给了 `Connection` 对象。
4. **持有与销毁 (Connection)**：`Connection` 对象在构造函数中将这个指针存在自己的成员变量里。当 `Connection` 对象最终被销毁时，它的**析构函数**负责执行 `delete sock;`。

这种设计确保了每个创建出来的 Socket 最终都有且只有一个“负责人”来清理它，完美避免了内存泄漏。

- 左值和右值
    - 左值：
        - 可以取地址，并且有持久存储位置的表达式，通常表示一个具名的变量或对象
        - 可以出现在赋值运算符的左侧（因此得名“左值”）
        - 生命周期通常超出单个表达式
    - 右值：
        - 不能取地址，并且生命周期只在当前表达式中的临时表达式，通常表示一个临时值或即将销毁的对象
        - 通常只能出现在赋值运算符的右侧
        - 生命周期短暂，在表达式结束后即被销毁
```c++
int x = 10; // x 是一个左值，它有内存地址
int* ptr = &x; // 可以取 x 的地址
int y = x; // x 作为左值被读取

int a = 5; // 5 是一个右值（字面量）
int b = a + 3; // (a + 3) 是一个右值（计算结果的临时值）

// int* ptr = &(a + 3); // 错误：不能取右值的地址

void func(int val) { /* ... */ }
func(100); // 100 是一个右值

```
-  `std::move()`：通过将**左值转换为右值引用**来允许编译器选择移动构造函数或移动赋值运算符，从而实现资源的有效转移而不是昂贵的拷贝。在处理像 `std::thread` 这样的**只可移动类型**时，`std::move()` 是必不可少的
```c++
 // 创建一个局部线程对象
std::thread new_worker([](){ /* do some work */ });
// 将局部线程对象移动到 workers 向量中
// 如果没有 std::move，这里会尝试拷贝，但 std::thread 不可拷贝，会导致编译错误
workers.push_back(std::move(new_worker));
```
- `std::condition_variable`:主要用途是解决生产者-消费者问题或任何需要线程等待特定条件才能继续执行的场景
    1. 它是如何工作的？
        - 等待线程 (消费者):
            - 获取一个 `std::unique_lock` 来锁定 `std::mutex`
            - 调用 `cv.wait()` 或 `cv.wait_for()` 或 `cv.wait_until()` 方法。这些方法会自动释放互斥锁，并将线程置于等待状态
            - 当条件变量被通知时，线程会醒来，并重新获取互斥锁
            - 线程会检查条件是否真的满足（通常在一个 while 循环中），如果满足则继续执行，否则再次等待
    2. `cv.wait()` 的语法
        ```c++
        // 只有一个参数的版本；容易受到“虚假唤醒 (spurious wakeups)”的影响
        void wait(std::unique_lock<std::mutex>& lock);
        // 带有条件变量的版本
        template< class Predicate >
        void wait( std::unique_lock<std::mutex>& lock, Predicate pred );
        ```
        - `Predicate pred` ： 一个可调用对象（如 lambda 表达式、函数对象或函数指针），无参数，返回一个bool值
- `emplace_back()` : C++ 标准库中许多容器提供这个成员函数，主要作用是在容器的末尾就地构造一个新元素
  
    - 就地构造：当你使用 `emplace_back()` 时，你将直接在容器内部构造新元素（调用对应的构造函数）,并将其存储在容器中（当然你需要提供构造新元素的相关参数）
    - 与 `push_back()` 的区别  
        使用`push_back()`时，  
        - 接受一个已经存在的对象（或一个可以隐式转换为对象的值）
        - 如果传递的是一个左值，它会调用拷贝构造函数将对象拷贝到容器中
        - 如果传递的是一个右值（或临时对象），它会调用移动构造函数将对象移动到容器中
        - 这就意味着，使用该函数存在2次构造行为：一次是创建临时对象，另一次是将其拷贝/移动到容器中
        使用`emplace_back()`时，  
        - 接受用于构造新元素的参数
        - 直接在容器内部的内存位置调用元素的构造函数，使用这些参数来构造对象
        - 避免了创建临时对象以及随后的拷贝或移动操作，从而可能提高效率（尤其是在处理复杂对象或需要大量拷贝/移动操作时）
    
- `inline`: 用于向编译器建议将函数体在调用点展开，而不是进行常规的函数调用
    - 仅仅是建议：inline 只是对编译器的一个建议，编译器有权忽略这个建议。例如，如果函数体过大，或者包含复杂的控制流（如循环、递归），编译器可能会选择不将其内联
    - **头文件中的内联函数**：通常，内联函数的定义会放在头文件中，这样在每个包含该头文件的源文件中，编译器都能看到函数的完整定义，从而有机会进行内联
    - **避免重复定义错误**：如果内联函数定义在多个源文件中，编译器必须确保它们是完全相同的。如果定义不同，会导致链接错误。
    - **减少函数调用开销**：内联可以消除函数调用的压栈、跳转等开销
    - **可能增加代码大小**：如果一个内联函数被频繁调用，并且函数体较大，那么将其内联可能会导致最终生成的可执行文件变大，因为函数体会在每个调用点重复出现

## 模板编程

### 一、 可变参数模板 (Variadic Templates)

解决“我不知道用户会传多少个、什么类型参数”的问题

#### 关键字 模板`Template`

* **本质**：模板不是函数，而是**生成函数的规则**（编译器在编译期根据你的调用自动代写代码）
* **`template<typename T>`**：
  * **类型抽象**：`typename `代表任意类型，与`class`等价使用（注意与`class F(){};`区分），`T` 是占位符，可以代表 `int`, `double`, 甚至指针 `int*`，可调用对象（如 lambda 表达式、函数对象或函数指针）
  * **数量限制**：这种d写法指定了参数数量是**固定**的（这里是 1 个）

#### 核心符号：省略号 `...`

* **声明参数包 (Pack)**：`typename... Args`。告诉编译器：“这里有一袋子类型，这堆类型的集合叫Args”
* **展开参数包 (Expansion)**：`args...`。告诉编译器：“把袋子args里的东西按逗号顺序抖搂出来”
* 省略号位置的举例

  * `typename... Args`（左侧）：是**打包**

    `Args &&... args`（中间）：是**匹配**。`args`中是已经确定的参数，调用函数传进去的实参，`Args `是待推定的参数包，等待`args`告诉他

    `args...`（右侧）：是**拆包**。把袋子里的变量一个一个倒出来，用逗号隔开，等价于`int a，double b，int* a`（根据实际情况等价）


#### 举例

```c++
template<typename T>//未知类型，一个参数
template<class... Args>//未知类型，未知数量参数
template<typename T, class... Args>//混合写法，声明的同时代表当前处理的参数是T，其他的是Args
```

#### 处理包的两种方式

* **递归法 (C++11)**：通过“处理第一个 + 递归处理剩余”的逻辑，配合一个空的终止函数

  ```c++
  void print(){}//自我调用需要一个空参数的同名函数，在递归的最后一步时会调用这个函数来终止
  
  template<typename T, class... Args>
  void print(T first, Args... rest) {
      std::cout << first << std::endl; // 处理当前参数
      print(rest...); //自我调用
  }
  ```
* **折叠表达式 (C++17)**：一行代码处理全包。例如：`(std::cout << ... << args);`

---

### 三、 万能引用与完美转发 (Perfect Forwarding)

为了实现“极致性能”，参数在传递过程中不能有余的拷贝。

* **万能引用 (`Args &&... args`)**

  * 使用`&&`来对`args`中的实参进行引用，对于左值（可引用和可取地址）则是引用类型，对于右值是右值引用(&&a)
  * `...` 作用于 `Args &&` 整个模式，将其复制 N 次
  * 例如（伪代码）

    ```c++
    args = int a,double b, int* c, &d,f
    Args &&... args = int &a, double &b, int* &c, int* &&d,int &&f
    ```



* **`std::forward`**：

  * 配合 `...` 使用：`std::forward<Args>(args)...`，等价于`std::forward<A1>(a1), std::forward<A2>(a2), ...`
  * **作用**：像透明管道一样，把参数的原有属性（是临时值还是变量）原封不动地传给下一级函数
  * 补充：引用折叠规则 (Reference Collapsing)，在 C++ 模板中，当“引用的引用”出现时，遵循以下逻辑：
    - $T\& + \& \to T\& \quad$ (左值 + 左值 = 左值)
    - $T\& + \&\& \to T\& \quad$ (左值 + 右值 = 左值)
    - $T\&\& + \& \to T\& \quad$ (右值 + 左值 = 左值)
    - $T\&\& + \&\& \to T\&\& \quad$ (**只有两个都是右值引用，结果才是右值**)




---

### 四、 尾置返回类型 (Trailing Return Type)

* **语法**：`auto func(参数) -> 返回类型`，这里`auto`是与`->`组合使用的
* **必要性**：在模板中，返回类型往往取决于参数。如果写在前面，编译器还没读到参数，无法推导
* **类型萃取**：`std::result_of<F(Args...)>::type`（或 C++17 的 `std::invoke_result_t`），用于在编译期模拟调用，算出返回值到底是什么类型，白话讲就是：模拟调用`F(Args...)`，`F`作为函数名，`Args`作为参数，使用`result_of<>`接受返回值，然后给`type`取别名（例如`using type = int`），最后与auto使用确定返回值类型

---

### 五、 异步凭证：`std::future<>`

* **定义**：异步操作的“占位符”，其实是一个类对象

  ```c++
  std::future<T>//类比
  std::vector<int>
  ```

* **核心逻辑**：

  ​	**占位**：函数立即返回一个 `future` 对象，主线程不阻塞。

  ​	**执行**：任务在后台线程（或线程池）运行。

  ​	**填值**：任务算完后，把值填入 `future` 内部的共享空间。

  ​	**取值**：用户通过 `res.get()` 拿到结果。如果没算完，`get()` 会自动等待

### 六、类模板std::packaged_task<>

就像`vector<int>`一样，`packaged_task<>`也是如此，只是<>内接收的是一个**函数签名（Function Signature）**

- 关于函数签名

### 七、 看懂线程池任务提交函数

将上述所有知识点合为一体：

```cpp
template <class F, class... Args>
auto ThreadPool::add(F &&f, Args &&...args) 
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    // 1. 推导返回值类型
    using return_type = typename std::result_of<F(Args...)>::type;

    // 2. 封装任务：std::bind 绑定函数与参数包，std::forward 保证转发性能
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    // 3. 提取 future 凭证并返回给用户
    std::future<return_type> res = task->get_future();
    
    // 4. 将任务放入队列（略去锁操作）
    tasks.emplace([task]() { (*task)(); });
    
    return res;
}
```

**关于 `using return_type = typename std::result_of<F(Args...)>::type;`**：

- `return_type `是类型名，就像`int`
- `return_type() `它是**函数签名**，就像`int()`是`int func()`的签名一样
- 由于 `std::bind` 已经预先绑定了所有参数，所以任务对外表现为“无参”，故写作 `( )`。
- 这保证了线程池的调度器可以用统一的 `task()` 方式来触发它

以下代码可以这样理解：

```c++
auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
//just like
auto task = std::make_shared< std::vector<int> >( std::bind() )
```

对于为什么在这使用lambda表达式`tasks.emplace([task]() { (*task)(); });`

- 因为emplace只接受`std::queue<std::function<void()>>`类型的对象，而`task`本身是一个智能指针（类型是：`std::shared_ptr<std::packaged_task<return_type()>>`），因此将其放进表达式来伪装为`void`类型，这称为**类型擦除**
- 表达式内部：先解引用`*task`，再调用`(*task)();`

### 总结

1. **`...` 在左**是打包，**`...` 在右**是拆包
2. **`auto` ... `->**` 是为了解决“还没看到参数就得确定返回值”的尴尬
3. **`std::future`** 占的是“未来的值”，而不是类型
4. **模板**是编译期的魔法，**多线程**是运行期的艺术

## Buffer类

### 分散读 (Scatter I/O)

头文件包含`sys/uio.h`

#### 结构体`struct iovec`

```c++
struct iovec {
    void  *iov_base;    // 内存块的起始地址
    size_t iov_len;     // 这块内存的长度
};
```

#### 系统调用`readv()`

```c++
// 函数原型
ssize_t read(int fd, void *buf, size_t count);
ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
```

- `read()`参数
  - `fd`：你要读取的文件描述符
  - `buf`：接收数据的内存起始地址（比如 `char buffer[1024];` 的 `buffer`）
  - `count`：你期望读取的最大字节数（通常就是 `buf` 的容量大小）
- `readv()`参数
  - `fd`：文件描述符
  - `iov`：一个指向 `iovec` 结构体数组的指针
  - `iovcnt`： `iovec` 数组里有几个元素（即你要读进几块独立的内存，比如之前写的是 `2` 块）

传统的 `read()` 只能把数据读进一块连续的内存里，使用`readv(int fd, const struct iovec *iov, int iovcnt)` 可以拿着你准备好的 `iovec` 数组，执行**分散读**

- 内核在处理 `readv` 时，会先把网卡数据填满第一块内存 (`vec[0]`)
- 如果数据还有剩，内核会自动无缝地继续填入第二块内存 (`vec[1]`)，中间**不需要用户态代码干预**

## [TCP“粘包/半包”问题](https://blog.csdn.net/zhizhengguan/article/details/119452571)

UDP 协议是“面向报文”的，发一个包就是一个包，自带边界。但 **TCP 是“面向字节流”的**，在 TCP 眼里，如果没有人为规定，一条消息的长度是不确定的



# proto与gRPC

### 将proto文件生成为可操作的文件

```bash
protoc -I=. \
       --cpp_out=. \
       --grpc_out=. \
       --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` \
       your_service.proto
```

- line1：等同于 `--proto_path=.`

- line2：生成基础的 Protobuf 序列化文件（ `.pb.cc` 和 `.pb.h`），存放在当前目录

- line3：生成 gRPC 专用的网络通信文件（包含服务端的 `Service` 基类和客户端 `Stub` 存根的 `.grpc.pb.cc` 和 `.grpc.pb.h`）

- line4：通过这个参数指定 gRPC 插件的位置（使用`which grpc_cpp_plugin`自动在Linux系统中寻找该插件的绝对路径）

- 标准做法是将其写进CMakeLists.txt中
  ```cmake
  add_custom_command(
        OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
        COMMAND protobuf::protoc
        ARGS --grpc_out=${PROTO_BUILD_DIR}
             --cpp_out=${PROTO_BUILD_DIR}
             --plugin=protoc-gen-grpc=${gRPC_CPP_PLUGIN_EXECUTABLE}
             -I ${PROTO_SRC_DIR}
             ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
  )
  ```

### 关于stub与信箱cq

[stub](https://blog.csdn.net/ldcigame/article/details/152221072)：是客户端与服务端 之间通信的关键桥梁。它隐藏了底层网络调用、序列化、协议细节，使开发者能像调用本地函数一样调用远程服务

- 是 gRPC 客户端侧的代理对象，封装了对远程 gRPC 服务的调用逻辑
- 由 Protocol Buffers 编译器（`protoc`）配合 gRPC 插件 自动生成，提供强类型、面向接口的 API

