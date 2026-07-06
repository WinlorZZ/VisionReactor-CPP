# CLAUDE.md

此文件为 Claude Code (claude.ai/code) 在此仓库中工作时提供指导。

## 构建与测试命令

```bash
# 配置并构建
mkdir -p build && cd build
cmake .. && make -j$(nproc)

# 构建单个目标
make server -j$(nproc)
make buffer_test -j$(nproc)

# 运行单个测试
./build/buffer_test
./build/ThreadPool_test
./build/connection_test

# 运行基准测试
./build/Buffer_bench
./build/ThreadPool_bench

# 启动服务 (需先启动 Python AI 节点)
cd python_ai && python Pserver.py &
./build/server [ai地址:端口]   # 默认: 127.0.0.1:50051
```

依赖项：gRPC、Protobuf、OpenCV 4.x、pthread。gRPC/Protobuf 需从源码编译或通过 vcpkg 安装——apt 中不可用。

## 架构

这是一个 C++17 异步网络网关，通过 TCP 接收视频帧，经异步 gRPC 分发给 Python YOLOv8 服务进行推理，再将结果返回客户端。

**数据流：** `Client → epoll (ET) → Connection::handleReadEvent → Connection::business → AsyncAIEngine::AnalyzeFrameAsync → gRPC CompletionQueue → Python YOLOv8 → CQ 回调 → ThreadPool → 结果处理/延迟日志`

**线程归属：** Connection 的读写 Buffer、Channel 和 epoll 状态只允许在 EventLoop 线程修改。其他线程通过 `EventLoop::queueInLoop` 投递闭包，并由 `eventfd` 唤醒 Reactor。

**所有权链：** `Server` 持有 `EventLoop`（心跳）、`Acceptor`（监听 Socket）、`ThreadPool`（工作线程）、`AsyncAIEngine`（gRPC 存根 + CQ 线程），以及一个以 fd 为键的 `map<int, shared_ptr<Connection>>`。`Connection` 持有自身的 `Socket`、`Channel` 和两个 `Buffer`（输入/输出）。`AsyncAIEngine` 持有 gRPC `CompletionQueue` 及其轮询线程，以及一个 `map<tag, shared_ptr<AsyncClientCall>>` 用于管理在途 RPC。

**关键类：**
- `EventLoop` / `Epoll` — epoll 封装；`EventLoop::loop()` 阻塞于 `epoll_wait` 并分发活跃的 `Channel`
- `Channel` — 将 fd 与读/写回调绑定；支持 `tie(shared_ptr)` 实现跨线程生命周期安全
- `Connection` — 状态机（`kConnected → kDisconnecting → kDisconnected`）；处理 TCP 组帧（4 字节大端长度前缀），通过 `onMessageCallback` 将业务工作委托给 ThreadPool
- `Buffer` — `vector<char>` + 读写游标 + 8 字节预留头；ET 模式下使用 `readv` 配合 64KB 栈缓冲区读取；`prependInt32`/`peekInt32` 处理网络字节序
- `ThreadPool` — 固定数量工作线程，基于 `std::future` 的任务提交
- `AsyncAIEngine` — 封装 gRPC `CompletionQueue`；`AnalyzeFrameAsync` 发起异步 RPC，`AsyncCompleteRpc`（独立线程）处理完成事件并将结果重新投递至 ThreadPool
- `LatencyProfiler` / `FrameContext` — 逐帧计时，包含 6 个探针点（T1–T4）和一个全局 atomic trace-id 发号器
- `H264Demuxer`（新增，尚未接入 Connection）— 解析 H.264 Annex B 字节流为 NALU 单元

**回调绑定（Server → Connection）：**
- `Server::handleNewConnection` 绑定到 `Acceptor::newConnectionCallback` — accept 时调用
- `Server::handleOnMessage` 绑定到 `Connection::onMessageCallback` — 完整帧读取完毕后调用；将 `conn->business(engine)` 推入 ThreadPool
- `Server::handleDeleteConnection` 绑定到 `Connection::deleteConnectionCallback` — 从 `Server::conns` 映射中移除连接

**线协议：** 4 字节大端 body 长度 + body 数据。`Connection::business` 循环解析帧，直到缓冲区耗尽或遇到不完整帧。

**线程安全：** `AsyncAIEngine::active_calls_` 由 `mu_` 保护；`ThreadPool::tasks` 由其内部 mutex 保护。`Connection` 对象跨线程共享——`Channel::tie` 机制（weak_ptr）可防止 Connection 被销毁后 epoll 仍持有引用导致的悬空调用。

## 规则

### 新增功能后同步学习清单

每次为本项目添加新功能或引入新技术后，必须在 `doc/learn lists.md` 中追加相关的学习条目。要求：

1. **条目格式** — 在对应分类下添加 `- [标题](链接) — 一句话说明`，若无合适分类则新建分类
2. **链接优先级** — 优先官方文档、cppreference、man 手册页等第一手资料；其次用技术博客（如 CSDN、知乎专栏）；视频仅推荐 B 站 (`bilibili.com/video/`)
3. **内容范围** — 聚焦本次新增功能涉及的技术点（如新增 H.264 解析，则添加 H.264 NALU 结构、Annex B 格式相关的学习链接）
4. **不要重复** — 添加前先检查是否已有相同或高度相似的条目
5. **时机** — 在功能实现完成后、代码 commit 前完成追加
