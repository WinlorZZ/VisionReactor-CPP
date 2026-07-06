# VisionReactor-CPP 架构与流程学习笔记

> 这份笔记不再按“今天学了哪个语法”组织，而是沿着系统的真实运行过程记忆：
>
> **服务启动 → 接受连接 → epoll 分发 → TCP 收包 → Buffer 拆包 → gRPC 推理 → CQ 回调 → TCP 回传 → 连接销毁**
>
> 相关图：
>
> - [可编辑类关系图](./Class%20Diagram.drawio)
> - [数据流说明](./Data%20Flow.md)
> - [完整流程图](./全流程.drawio)
> - [输出 Buffer 状态机](./Output%20Buffer%20发送状态机.md)

---

# 1. 先记住系统全景

## 1.1 项目解决什么问题

客户端通过 TCP 发送编码后的图像帧，C++ 网关负责：

1. 接受并维护 TCP 连接。
2. 从字节流中切出完整图像。
3. 通过异步 gRPC 调用 Python YOLO 服务。
4. 接收检测框与推理延迟。
5. 将结果发回原客户端。

当前稳定化分支已经具备网络底座与异步推理调用，但 **AI 结果回传客户端的闭环仍在待办中**。

## 1.2 一帧数据的主路径

```text
Client
  │  TCP: [4 字节大端长度][JPEG bytes]
  ▼
EventLoop::loop()
  │
  ▼
Epoll::poll()
  │ 返回活跃 Channel*
  ▼
Channel::handleEvent()
  │
  ▼
Connection::handleReadEvent()
  │ readv 读到 inputBuffer
  ▼
Connection::business()
  │ 长度前缀拆包
  ▼
AsyncAIEngine::AnalyzeFrameAsync()
  │
  ▼
Python YOLO gRPC Service
  │
  ▼
CompletionQueue
  │
  ▼
ThreadPool 结果处理
  │
  ├─ 当前：打印检测结果与延迟
  └─ 待实现：Connection::send() 回传客户端
```

## 1.3 五个架构不变量

比成员名更值得记忆的是这些规则：

1. **Connection 的 Buffer、Channel、连接状态只由 EventLoop 线程修改。**
2. **其他线程操作 Connection 时，通过 `queueInLoop()` 回投 EventLoop。**
3. **ET 模式下，accept/read/write 都要处理到 `EAGAIN`。**
4. **Channel 回调期间通过 `tie_` 临时保活 Connection。**
5. **停机时先停止 AsyncAIEngine，再停止 ThreadPool。**

如果几天后忘记代码，先回忆这五条，再顺着流程找函数。

---

# 2. 对象关系与所有权

## 2.1 顶层对象

```text
main
 ├─ EventLoop              栈对象，主 Reactor
 └─ Server                 栈对象，组装业务组件
     ├─ Acceptor           拥有监听 Socket 和监听 Channel
     ├─ ThreadPool         处理 CQ 返回后的任务
     ├─ AsyncAIEngine      拥有 gRPC Stub、CQ 和 CQ 线程
     └─ conns              fd → shared_ptr<Connection>
```

## 2.2 Connection 内部

```text
Connection
 ├─ Socket*                已连接 socket 的 RAII 包装
 ├─ Channel*               fd 与读写回调的绑定
 ├─ Buffer* inputBuffer    TCP 输入字节流
 ├─ Buffer* outputBuffer   暂时无法写入内核的数据
 └─ FrameContextPtr        单帧延迟追踪上下文
```

## 2.3 Socket 所有权转移

客户端 Socket 像一根接力棒：

```text
Acceptor::accept()
  → 创建 Socket*
  → newConnectionCallback(Socket*)
  → Server::handleNewConnection()
  → Connection 构造函数保存 Socket*
  → Connection 析构时 delete sock
```

规则：

- 同一裸指针只能有一个明确的销毁负责人。
- Acceptor 把客户端 Socket 交给 Server 后，不再销毁它。
- Server 又把 Socket 交给 Connection，最终由 Connection 负责。

## 2.4 为什么 `Server::conns` 使用 `shared_ptr`

Connection 可能同时被以下位置临时使用：

- `Server::conns`
- `Channel::handleEvent()` 中的 guard
- `shared_from_this()` 产生的回调参数
- EventLoop 待执行闭包

从 `conns` 擦除只表示 Server 放弃所有权，不保证 Connection 当场析构；最后一个 `shared_ptr` 离开作用域后才析构。

## 2.5 为什么 `Channel::tie_` 是 `weak_ptr`

如果 Channel 长期持有 `shared_ptr<Connection>`：

```text
Connection 拥有 Channel
Channel 又拥有 Connection
→ 循环引用
→ 两者都无法析构
```

因此 Channel 只保存 `weak_ptr`。事件发生时：

```cpp
auto guard = tie_.lock();
if (guard) {
    handleEventWithGuard();
}
```

局部 `guard` 只在本次回调期间保活 Connection，回调结束后自动释放。

---

# 3. 服务启动流程

## 3.1 `main()` 做了什么

```text
忽略 SIGPIPE
  → 读取 AI 服务地址
  → 构造 EventLoop
  → 构造 Server(loop, ai_target)
  → loop.loop()
```

### 为什么忽略 SIGPIPE

向已经关闭的 TCP 连接执行 `write()`，可能：

1. 返回 `-1`，`errno == EPIPE`。
2. 内核同时发送 `SIGPIPE`。
3. SIGPIPE 默认行为是终止整个进程。

因此入口处调用：

```cpp
signal(SIGPIPE, SIG_IGN);
```

代码仍需检查 `write()` 的返回值，忽略信号不等于忽略错误。

## 3.2 EventLoop 构造

EventLoop 创建：

- `Epoll`
- `eventfd`
- 绑定 eventfd 的 `wakeupChannel_`
- pending functor 队列

`eventfd` 也是文件描述符，可以注册进 epoll。其他线程向它写入计数值，阻塞在 `epoll_wait()` 的 EventLoop 就会被唤醒。

## 3.3 Server 构造

Server 依次创建：

1. `Acceptor`
2. `ThreadPool`
3. gRPC Channel 与 `AsyncAIEngine`
4. Acceptor 的新连接回调

构造完成后，EventLoop 已监听：

- 监听 Socket 的可读事件
- eventfd 的可读事件

---

# 4. Linux 文件描述符：所有事件的统一入口

Linux 中 Socket、epoll 实例、eventfd 都可以通过一个整数 fd 标识。

常见 fd：

- `0`：stdin
- `1`：stdout
- `2`：stderr
- `socket()`：返回 Socket fd
- `epoll_create1()`：返回 epoll fd
- `eventfd()`：返回唤醒 fd

fd 只是当前进程文件描述符表中的索引，不是网络连接对象本身。

```cpp
int fd1 = ::socket(AF_INET, SOCK_STREAM, 0);
int fd2 = ::eventfd(0, EFD_NONBLOCK);
```

内核通常分配当前最小的可用整数，但业务代码不能依赖具体数值。

---

# 5. 监听与建立连接

## 5.1 监听 Socket 的系统调用链

```text
socket()
  → setsockopt(SO_REUSEADDR)
  → bind()
  → listen()
  → fcntl(O_NONBLOCK)
  → epoll_ctl(ADD)
```

### `socket()`

```cpp
int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
```

- `AF_INET`：IPv4
- `SOCK_STREAM`：TCP 字节流
- `0`：选择默认 TCP 协议

### `setsockopt(SO_REUSEADDR)`

```cpp
int opt = 1;
::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
```

允许服务重启后更快重新绑定地址。`void*` 参数让同一个接口可以接收不同类型的选项值。

### `bind()`

```cpp
::bind(fd,
       reinterpret_cast<const sockaddr*>(&addr),
       sizeof(addr));
```

`sockaddr` 是通用地址接口，IPv4 使用实际结构 `sockaddr_in`。

### `listen()`

将 Socket 转为监听 Socket。backlog 表示等待 accept 的连接队列上限，不等于服务器的最大并发连接数。

### `fcntl(O_NONBLOCK)`

```cpp
int flags = ::fcntl(fd, F_GETFL, 0);
::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

使用位或 `|` 保留原有标志，只追加非阻塞属性。

ET 模式必须搭配非阻塞 I/O，否则一次没有数据的调用就可能卡住整个 EventLoop。

## 5.2 `sockaddr_in` 与字节序

```cpp
struct sockaddr_in {
    sa_family_t    sin_family;
    in_port_t      sin_port;
    struct in_addr sin_addr;
};
```

网络字节序使用大端：

- `htons()`：16 位主机序 → 网络序
- `htonl()`：32 位主机序 → 网络序
- `ntohs()` / `ntohl()`：反向转换

长度前缀也必须统一使用网络字节序。

## 5.3 `accept()` 的输出参数

客户端地址由内核写入：

```text
程序准备 sockaddr_in 内存
  → 把地址传给 accept()
  → 内核从已完成握手的连接中读取对端地址
  → 写入这块内存
  → accept 返回新的连接 fd
```

这类参数称为输出参数。

## 5.4 ET 模式为什么循环 accept

监听 fd 从“没有连接”变成“有连接”时，ET 可能只通知一次。如果队列里同时有多个连接，只 accept 一个，剩余连接可能迟迟得不到下一次通知。

```cpp
while (true) {
    int fd = ::accept(...);
    if (fd >= 0) {
        // 创建 Connection
        continue;
    }
    if (errno == EINTR) continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
    // 其他错误
}
```

## 5.5 新连接回调链

```text
监听 Channel 可读
  → Acceptor::acceptNewConnection()
  → accept() 获得客户端 fd
  → newConnectionCallback(Socket*)
  → Server::handleNewConnection()
  → make_shared<Connection>
  → 放入 conns
  → 设置消息回调和删除回调
  → Connection::connectEstablished()
  → Channel::tie(connection)
  → enableReading()
```

这里使用 `std::function` 保存回调，使用 `std::bind` 或 lambda 将成员函数转换为可调用对象。

```cpp
acceptor->setNewConnectionCallback(
    std::bind(&Server::handleNewConnection,
              this,
              std::placeholders::_1));
```

回调必须保存在成员变量里；只把临时函数参数传进 setter 而不保存，函数返回后就无法在未来事件中调用。

---

# 6. epoll 与 Channel 如何分发事件

## 6.1 `epoll_event`

```cpp
struct epoll_event {
    uint32_t events;
    epoll_data_t data;
};

union epoll_data {
    void*    ptr;
    int      fd;
    uint32_t u32;
    uint64_t u64;
};
```

- `events`：事件位掩码，如 EPOLLIN、EPOLLOUT、EPOLLET。
- `data.ptr`：保存对应 Channel 指针。

union 的成员共享同一块内存；设置 `ptr` 后就按 `ptr` 读取。

## 6.2 `epoll_ctl`

```text
EPOLL_CTL_ADD：首次注册 Channel
EPOLL_CTL_MOD：修改关注事件
EPOLL_CTL_DEL：移除 Channel
```

Channel 只维护“我关注什么”，Epoll 负责调用内核接口。

```text
Channel::enableWriting()
  → events |= EPOLLOUT
  → EventLoop::updateChannel(this)
  → Epoll::updateChannel(this)
  → epoll_ctl(MOD)
```

## 6.3 `epoll_wait`

```cpp
int n = ::epoll_wait(epoll_fd,
                     events.data(),
                     events.size(),
                     timeout);
```

返回值：

- `> 0`：就绪事件数量
- `0`：超时
- `-1`：失败，需要检查 errno

Epoll 将每个 `event.data.ptr` 恢复为 Channel，并写入 `revents`。

## 6.4 EventLoop 分发

```text
Epoll::poll()
  → vector<Channel*>
  → Channel::handleEvent()
  → tie_.lock() 得到 guard
  → 根据 revents 调用 readCallback/writeCallback
  → EventLoop::doPendingFunctors()
```

## 6.5 位运算为什么适合事件标志

```cpp
events |= EPOLLIN;       // 添加读事件
events &= ~EPOLLOUT;     // 删除写事件

if (revents & (EPOLLIN | EPOLLRDHUP)) {
    // 包含任意一种指定事件
}
```

多个布尔事件可以压缩在同一个整数的不同二进制位中。

---

# 7. ET 与非阻塞 I/O 的统一返回值模型

系统调用失败时通常返回 `-1`，然后通过 `errno` 区分原因。只有返回值表明失败时，读取 errno 才有意义。

## 7.1 read/readv

| 返回值 | 含义 | 处理 |
|---|---|---|
| `n > 0` | 收到 n 字节 | 消费数据，ET 下继续读 |
| `n == 0` | 对端发送 FIN | 进入关闭流程 |
| `n == -1, EINTR` | 被信号打断 | 重试 |
| `n == -1, EAGAIN/EWOULDBLOCK` | 当前接收缓冲区已读空 | 正常结束本轮排水 |
| 其他 `-1` | 真实错误 | 关闭连接 |

**读空不等于返回 0。**

- 返回 0：对端不会再发送数据。
- EAGAIN：暂时没有数据，连接仍然存在。

## 7.2 write

| 返回值 | 含义 | 处理 |
|---|---|---|
| `n == len` | 全部进入内核发送缓冲区 | 完成 |
| `0 < n < len` | 部分写 | 剩余数据放 outputBuffer |
| `-1, EINTR` | 被信号打断 | 重试 |
| `-1, EAGAIN/EWOULDBLOCK` | 内核发送缓冲区已满 | 等待 EPOLLOUT |
| `-1, EPIPE/ECONNRESET/...` | 对端异常或其他错误 | 关闭连接 |

常见 errno：

- `EAGAIN/EWOULDBLOCK`：当前操作会阻塞。
- `EINTR`：系统调用被信号打断。
- `EPIPE`：向已关闭连接写入。
- `ECONNRESET`：对端复位连接。
- `EMFILE`：当前进程 fd 达到上限。
- `ENFILE`：系统级 fd 达到上限。

## 7.3 ET 的统一原则

```text
accept：循环到 EAGAIN
read：循环到 EAGAIN
write：循环到 Buffer 空或 EAGAIN
```

LT 会在“仍然就绪”时继续通知；ET 主要通知状态边沿，因此程序必须主动排空。

---

# 8. TCP 收包、Buffer 与协议拆包

## 8.1 TCP 没有消息边界

TCP 只保证有序字节流：

```text
发送两次：AAA | BBBB
接收可能：A | AAB | BB | B
也可能：AAABBBB
```

因此不能假设一次 read 对应一帧图像。

当前 MVP 使用：

```text
[4 字节大端 body_length][body_length 字节图像]
```

解析器必须同时处理：

- 半个包头
- 完整包头但 body 不完整
- 一个完整包
- 多个包粘在一起

## 8.2 Buffer 双游标

```text
| prependable | readable | writable |
                 ↑          ↑
            readerIndex writerIndex
```

核心操作：

- `readableBytes()`：可以解析的数据量。
- `writableBytes()`：尾部剩余空间。
- `append()`：写入数据。
- `retrieve()`：移动读游标。
- `retrieveAll()`：读空后复位。
- `peekInt32()`：查看 4 字节大端长度，但不移动游标。

## 8.3 `readv()` 分散读

```cpp
struct iovec {
    void*  iov_base;
    size_t iov_len;
};
```

Buffer 准备两块空间：

```text
vec[0] → Buffer 自身 writable 区
vec[1] → 栈上 64 KiB extrabuf
```

一次 `readv()`：

1. 先写 Buffer 尾部。
2. 尾部不够时继续写 extrabuf。
3. 用户态再把 extrabuf 中的溢出部分 append 到 Buffer。

优势不是“网络数据完全零拷贝”，而是：

- 一次系统调用可以利用两块不连续空间。
- 常见小包直接进入 Buffer。
- 突发大包不用预先把 Buffer 扩得很大。

## 8.4 `business()` 排水解析

```text
readableBytes < 4
  → 等更多包头

读取 body_length
  → 0 或超过上限：非法，关闭

readableBytes < 4 + body_length
  → 半包，保留现场等待

数据完整
  → retrieve(4)
  → retrieveAsString(body_length)
  → 发起异步 RPC
  → 继续 while 检查下一个粘包
```

协议解析与 inputBuffer 消费都在 EventLoop 线程进行，避免 Worker 与 I/O 线程同时修改游标和 vector。

---

# 9. 从完整帧到异步 gRPC

## 9.1 Protobuf 与 gRPC 的分工

- Protobuf：定义消息结构并负责序列化。
- gRPC：根据 service 定义生成客户端 Stub 和服务端接口。

项目契约：

```text
FrameRequest
 ├─ frame_id
 ├─ image_data
 └─ timestamp_ms

FrameResponse
 ├─ frame_id
 ├─ repeated BBox
 └─ inference_latency_us
```

## 9.2 代码生成

```bash
protoc -I=. \
  --cpp_out=. \
  --grpc_out=. \
  --plugin=protoc-gen-grpc="$(which grpc_cpp_plugin)" \
  game_ai.proto
```

实际项目由 CMake 的 `add_custom_command()` 自动完成。

生成文件：

- `.pb.h/.pb.cc`：消息类型与序列化。
- `.grpc.pb.h/.grpc.pb.cc`：Service、Stub 和 RPC 接口。

## 9.3 Stub 是什么

Stub 是客户端代理：

```text
C++ 调用 Stub 方法
  → Protobuf 序列化
  → gRPC 传输
  → Python Service
```

业务代码无需直接构造 HTTP/2 帧。

## 9.4 单次异步调用的状态

`AsyncClientCall` 保存：

- `ClientContext`
- `FrameResponse reply`
- `Status`
- `ClientAsyncResponseReader`
- `FrameContextPtr`
- 待实现：原 Connection 的 `weak_ptr`

这些状态必须活到 CQ 返回完成事件。

## 9.5 tag 与 `active_calls_`

gRPC CompletionQueue 通过 `void* tag` 告诉程序“哪个异步操作完成”。

```text
创建 shared_ptr<AsyncClientCall>
  → tag = call.get()
  → active_calls_[tag] = call
  → Finish(..., tag)
  → CQ 返回同一 tag
  → map 找回 shared_ptr
  → erase map
```

必须先放入 map，再允许完成事件到达；否则极快 RPC 可能先返回，CQ 线程却找不到 tag。

## 9.6 Python 推理服务

```text
收到 FrameRequest
  → np.frombuffer(image_data)
  → cv2.imdecode()
  → YOLO(img)
  → 填充 BBox
  → 记录 inference_latency_us
  → 返回 FrameResponse
```

图像解码失败应返回 `INVALID_ARGUMENT`，不能把空图继续送入模型。

---

# 10. CQ 回调、线程池与跨线程发送

## 10.1 当前 CQ 路径

```text
cq_.Next(tag, ok)
  → 从 active_calls_ 取出 call
  → 检查 status.ok() 与 ok
  → 记录 t_grpc_recv
  → 拷贝 reply 与 FrameContext
  → 投递 ThreadPool
  → 打印检测框和延迟
```

待实现闭环：

```text
AsyncClientCall 保存 weak_ptr<Connection>
  → CQ/Worker 中 weak_ptr.lock()
  → Connection 仍存活：序列化 JSON
  → conn->send(packet)
  → EventLoop::runInLoop()
  → sendInLoop()
```

## 10.2 为什么不能让 Worker 直接修改 Channel

即使给 outputBuffer 加锁，以下状态仍可能竞争：

- `Channel::events`
- `Channel::isadd`
- `epoll_ctl()`
- `Connection::state_`
- Connection 析构

更清晰的模型是线程归属：

```text
Worker 只产生“我要发送这些字节”的意图
EventLoop 负责真正修改连接状态并执行 write
```

## 10.3 `runInLoop()` 与 `queueInLoop()`

```text
调用者就在 EventLoop 线程
  → 直接执行闭包

调用者是其他线程
  → pendingFunctors_ 入队
  → write(eventfd)
  → epoll_wait 被唤醒
  → 读取 eventfd
  → doPendingFunctors()
```

pending 队列使用 mutex 保护，但 Connection 的复杂状态仍保持单线程访问。

---

# 11. TCP 发送与优雅关闭

## 11.1 快速路径

outputBuffer 为空时先直接 `write()`：

```text
全部写完
  → 返回

部分写 / EAGAIN
  → 剩余字节 append 到 outputBuffer
  → enableWriting()
```

## 11.2 EPOLLOUT 排水

正确的 ET 写路径应为：

```text
while outputBuffer 非空:
    write()
    ├─ n > 0       → retrieve(n)，继续
    ├─ EINTR       → 重试
    ├─ EAGAIN      → 保留 Buffer，等待下一次可写
    └─ 其他错误    → 关闭

Buffer 为空:
    disableWriting()
```

当前 `handleWriteEvent()` 仍只执行一次 write，完整排水循环属于下一项实现任务。

## 11.3 为什么平时不监听 EPOLLOUT

大多数 TCP Socket 在大多数时间都可写。如果永久关注 EPOLLOUT，`epoll_wait()` 会不断返回，造成 CPU 空转。

只有出现待发送积压时注册 EPOLLOUT，排空后立即注销。

## 11.4 连接状态机

```text
kConnected
  → handleClose()
kDisconnecting
  ├─ outputBuffer 非空：等待排空
  └─ outputBuffer 为空：通知 Server 删除
kDisconnected
```

Server 从 `conns` 擦除 Connection 后，真正析构时间由剩余 `shared_ptr` 决定。

Channel 析构前必须从 epoll 中移除，避免 `epoll_event.data.ptr` 指向已经释放的对象。

---

# 12. ThreadPool：把语法放回业务位置

ThreadPool 在当前架构中主要用于处理 CQ 返回后的任务，而不是消费 Connection 的 inputBuffer。

## 12.1 生产者—消费者模型

```text
生产者：threadPool->add(task)
  → mutex 加锁
  → task 入队
  → cv.notify_one()

消费者：Worker
  → cv.wait(lock, predicate)
  → task 出队
  → 解锁
  → 执行 task
```

`condition_variable::wait()` 会：

1. 原子地释放 mutex 并休眠。
2. 被通知后重新获取 mutex。
3. 再检查 predicate，防止虚假唤醒。

## 12.2 `add()` 中的模板知识

```cpp
template<class F, class... Args>
auto add(F&& f, Args&&... args)
    -> std::future<
        typename std::result_of<F(Args...)>::type>;
```

只需沿执行过程理解：

```text
F + Args
  → 推导 return_type
  → bind 把函数和参数绑定成无参任务
  → packaged_task<return_type()>
  → future 交给调用者
  → lambda 擦除成 function<void()>
  → 放入统一任务队列
```

### 参数包

- `typename... Args`：声明类型参数包。
- `Args&&... args`：接收任意数量实参。
- `args...`：展开参数包。

### 完美转发

```cpp
std::forward<Args>(args)...
```

保留每个参数原本的左值/右值属性，避免不必要拷贝。

### `packaged_task` 与 `future`

- `packaged_task<R()>`：把可调用对象包装成能够产生 R 的异步任务。
- `future<R>`：任务结果的读取端；调用 `get()` 时结果未完成会等待。

### 为什么再包一层 lambda

任务队列类型统一为：

```cpp
std::queue<std::function<void()>>
```

而 `packaged_task` 的返回类型可能不同。捕获 shared_ptr 后：

```cpp
tasks.emplace([task]() { (*task)(); });
```

所有任务都表现为 `void()`，这是一种类型擦除。

## 12.3 常用 C++ 工具在项目中的位置

### `std::function`

保存不同来源但签名相同的回调：

```cpp
std::function<void()> readCallback;
```

### `std::bind`

把成员函数、对象和占位参数绑定成回调：

```cpp
std::bind(&Server::handleOnMessage, this, _1)
```

### lambda

定义临时闭包并捕获上下文：

```cpp
[weakSelf, msg]() {
    if (auto self = weakSelf.lock()) {
        self->sendInLoop(msg);
    }
}
```

捕获值决定闭包持有哪些数据以及生命周期。

### `std::move`

把对象转换为可移动表达式，让目标对象接管资源。`std::move` 本身不移动，真正移动发生在移动构造或移动赋值中。

### `emplace_back`

在容器内部直接构造元素。是否比 `push_back` 更快要看参数与类型，不能机械地认为永远没有拷贝。

### `inline`

现代 C++ 中，`inline` 更重要的语言含义是允许相同定义出现在多个翻译单元；编译器是否展开函数由优化器决定。

---

# 13. 延迟追踪：一帧如何被观测

`FrameContext` 记录：

```text
t_start
  → t_parsed
  → t_grpc_sent
  → t_grpc_recv
  → t_finish
```

分段：

- T1：TCP 读取与协议拆包
- T2：构造并发起 gRPC
- T3：Python 图像解码与 YOLO 推理
- IPC：gRPC 往返减去 T3
- T4：回调结果处理

注意：

- T3 来自 Python 服务，时钟与 C++ 不同，因此只传“耗时”，不能直接比较两台机器的绝对时间点。
- 当前 `t_finish` 是结果处理完成，不等于浏览器真正收到结果。
- 如果要声明端到端延迟，需要客户端时间戳或客户端自行测量。

---

# 14. 停机与析构顺序

## 14.1 为什么先销毁 AsyncAIEngine

错误顺序：

```text
先销毁 ThreadPool
  → CQ 线程此时收到 RPC 完成事件
  → 尝试 threadpool->add()
  → 使用已释放对象
```

正确顺序：

```text
取消在途 ClientContext
  → cq_.Shutdown()
  → join CQ 线程
  → AsyncAIEngine 析构完成
  → 再销毁 ThreadPool
```

已经投递到 ThreadPool 的任务不能捕获已经析构的 AsyncAIEngine `this`。因此延迟打印函数改为 static，任务不依赖 Engine 对象存活。

## 14.2 最终目标停机流程

```text
SIGINT/SIGTERM
  → 停止接受新连接
  → 取消或排空在途 RPC
  → 停止 CQ
  → 停止 ThreadPool
  → 释放 Connections
  → 退出 EventLoop
```

当前信号驱动的完整优雅停机仍属于待办事项。

---

# 15. H.264 模块应该放在哪里理解

当前 H.264 代码完成的是：

- 搜索 Annex B 的 3/4 字节起始码。
- 切分 NALU。
- 识别 NALU type。
- 处理部分半包与大小阈值。

它没有完成：

- Access Unit/完整视频帧组装。
- SPS/PPS 状态管理。
- H.264 解码。
- 将压缩数据转换为 YOLO 可推理的 BGR 图像。

因此当前主线是长度前缀 JPEG，H.264 是实验模块。未来若接入，应放在：

```text
TCP H.264 字节流
  → NALU / Access Unit 组装
  → FFmpeg 解码
  → BGR Frame
  → 抽帧
  → gRPC 推理
```

---

# 16. 测试与性能：每层证明什么

## 16.1 单元测试

- Buffer：游标、扩容、搬移、边界。
- ThreadPool：任务执行与线程数量。
- Connection：生命周期和跨线程发送。
- EventLoop：eventfd 唤醒与 pending functor。
- H264Demuxer：起始码与 NALU 切分。

## 16.2 Sanitizer

- ASan：越界、use-after-free、部分泄漏。
- UBSan：未定义行为。
- TSan：数据竞争。

测试通过不能证明“并发一定正确”，但可以验证具体时序和不变量。

## 16.3 基准数据如何表述

Buffer 基准主要测内存追加、游标与搬移，不等于真实网络吞吐。

ThreadPool 基准测的是特定机器、线程数与任务模型下的调度表现，不是整个服务器 QPS。

简历数据必须注明：

- 硬件与编译模式
- 数据大小
- 并发模型
- 测量方法
- 多次运行结果

---

# 17. 复习时只走这三遍

## 第一遍：只讲对象

```text
谁拥有谁？
谁负责销毁？
谁在哪个线程修改？
```

## 第二遍：只讲一帧

```text
TCP 数据从哪里进入？
在哪里组成完整帧？
RPC 状态由谁保存？
结果如何回到原连接？
```

## 第三遍：只讲异常

```text
半包怎么办？
EAGAIN 怎么办？
客户端提前断开怎么办？
AI 超时怎么办？
停机时还有 RPC 怎么办？
```

## 一分钟复述模板

> 项目使用单 Reactor 管理非阻塞 TCP 连接，EventLoop 通过 epoll ET 分发 Channel 事件。Connection 使用双游标 Buffer 和长度前缀协议处理粘包半包，完整 JPEG 帧通过异步 gRPC 发往 Python YOLO。CompletionQueue 独立线程接收结果，再投递线程池处理。为避免跨线程修改 Channel 与 Buffer，其他线程调用 Connection::send 时会通过 eventfd 和 queueInLoop 回到 EventLoop 执行实际写入；Connection 生命周期由 Server 的 shared_ptr、Channel 的 weak_ptr tie 以及回调期间的局部 guard 共同保证。

---

# 附录 A：系统调用速查

| 系统调用 | 在项目中的作用 |
|---|---|
| `socket` | 创建监听或客户端 Socket |
| `setsockopt` | 设置 SO_REUSEADDR 等选项 |
| `bind` | 绑定本地 IP 与端口 |
| `listen` | 进入监听状态 |
| `accept` | 从已完成连接队列取出客户端 fd |
| `fcntl` | 设置 O_NONBLOCK |
| `epoll_create1` | 创建 epoll 实例 |
| `epoll_ctl` | 添加、修改、删除 Channel |
| `epoll_wait` | 等待就绪事件 |
| `eventfd` | 跨线程唤醒 EventLoop |
| `readv` | 将数据分散读入 Buffer 与 extrabuf |
| `write` | 将响应写入内核发送缓冲区 |
| `close` | 释放 fd |

# 附录 B：当前实现与待办边界

已经具备：

- [x] 非阻塞 TCP + epoll ET
- [x] Acceptor 循环 accept
- [x] Connection 生命周期保护
- [x] Buffer 长度前缀拆包
- [x] eventfd 跨线程投递
- [x] 异步 gRPC 请求与 CQ
- [x] 基础延迟追踪

尚待完成：

- [ ] AI 回应关联原 Connection
- [ ] JSON + 长度前缀响应
- [ ] ET 写事件循环排空
- [ ] RPC deadline 与错误响应
- [ ] 在途请求、输出缓冲区背压
- [ ] TCP → gRPC → TCP 集成测试
- [ ] 视频 Web Demo
- [ ] 优雅停机

具体实现顺序见 [todolist.md](./todolist.md)。
