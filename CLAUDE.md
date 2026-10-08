# CLAUDE.md

此文件为 Claude Code (claude.ai/code) 在此仓库中工作时提供指导。

## 构建与测试命令

```bash
# 配置 + 编译（Debug，产物在 build/）
make build

# 运行全部单元测试 / 基准
make test
make bench

# 运行单个测试
./build/buffer_test
./build/connection_test

# 启动服务 (需先启动 Python AI 节点)
cd python_ai && python -u Pserver.py &   # -u 必须有，否则输出被缓冲，看起来像没启动
./build/server [ai地址:端口]   # 默认: 127.0.0.1:50051
```

依赖项：gRPC、Protobuf、OpenCV 4.x、OpenSSL、pthread。Ubuntu 22.04/24.04 直接用 apt 安装即可
（实测 apt 提供的 gRPC 1.51.1 + protobuf 3.21.12 可正常构建；也可用 vcpkg 或源码编译）。

**googletest**：本分支的 `CMakeLists.txt` 没有 `find_package(GTest)`，只走 `FetchContent` 从 GitHub 下载。
没有网络（或 CMake 需要走代理——它只读 `https_proxy` 环境变量、不读 git 的代理配置）时，
可以自己调 cmake 传本地源码：`-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=<googletest 源码目录>`。

## 架构

这是一个 C++17 异步网络网关，通过 TCP 或 WebSocket 接收图像帧，经异步 gRPC 分发给 Python YOLOv8 服务进行推理，再将结果返回客户端。

**数据流：** `Client → epoll (ET) → Connection::handleReadEvent → Connection::business → AsyncAIEngine::AnalyzeFrameAsync → gRPC CompletionQueue → Python YOLOv8 → CQ 回调 → ThreadPool → 结果处理/延迟日志`

**线程归属：** Connection 的读写 Buffer、Channel 和 epoll 状态只允许在 EventLoop 线程修改。其他线程通过 `EventLoop::queueInLoop` 投递闭包，并由 `eventfd` 唤醒 Reactor。

**所有权链：** `Server` 持有 `EventLoop`（主循环）、`Acceptor`（监听 Socket）、`ThreadPool`（工作线程）、`AsyncAIEngine`（gRPC 存根 + CQ 线程），以及一个以 fd 为键的 `map<int, shared_ptr<Connection>>`。`Connection` 持有自身的 `Socket`、`Channel` 和两个 `Buffer`（输入/输出）。`AsyncAIEngine` 持有 gRPC `CompletionQueue` 及其轮询线程，以及一个 `map<tag, shared_ptr<AsyncClientCall>>` 用于管理在途 RPC。

**关键类：**
- `EventLoop` / `Epoll` — epoll 封装；`EventLoop::loop()` 阻塞于 `epoll_wait` 并分发活跃的 `Channel`
- `Channel` — 将 fd 与读/写回调绑定；`tie()` 存的是 **`weak_ptr`**（不增加引用计数），只在 `handleEvent` 期间提升成局部 guard，保证事件回调期间对象不被析构
- `Connection` — 状态机（`kConnected → kDisconnecting → kDisconnected`）；处理 TCP 组帧（4 字节大端长度前缀）与 WebSocket 帧（握手、解掩码、帧封装）；每连接在途请求上限 2，超出回 `OVERLOADED`
- `Buffer` — `vector<char>` + 读写游标 + 8 字节预留头；ET 模式下使用 `readv` 配合 64KB 栈缓冲区读取；`prependInt32`/`peekInt32` 处理网络字节序
- `ThreadPool` — 固定数量工作线程，基于 `std::future` 的任务提交
- `AsyncAIEngine` — 封装 gRPC `CompletionQueue`；`AnalyzeFrameAsync` 发起异步 RPC，`AsyncCompleteRpc`（独立线程）处理完成事件并将结果重新投递至 ThreadPool
- `LatencyProfiler` / `FrameContext` — 逐帧计时，包含 6 个探针点（T1–T4）和一个全局 atomic trace-id 发号器
- `H264Demuxer`（新增，尚未接入 Connection）— 解析 H.264 Annex B 字节流为 NALU 单元

**回调绑定（Server → Connection）：**
- `Server::handleNewConnection` 绑定到 `Acceptor::newConnectionCallback` — accept 时调用
- `Server::handleOnMessage` 绑定到 `Connection::onMessageCallback` — 完整帧读取完毕后调用，**直接在 EventLoop 线程**执行 `conn->business(engine)`；gRPC 本身是异步的，这里不经过线程池（线程池只用于 CQ 回调之后的结果处理）
- `Server::handleDeleteConnection` 绑定到 `Connection::deleteConnectionCallback` — 从 `Server::conns` 映射中移除连接

**线协议：** 4 字节大端 body 长度 + body 数据。`Connection::business` 循环解析帧，直到缓冲区耗尽或遇到不完整帧。

**线程安全：** `AsyncAIEngine::active_calls_` 由 `mu_` 保护；`ThreadPool::tasks` 由其内部 mutex 保护。`Connection` 对象跨线程共享——`Channel::tie` 机制（weak_ptr）可防止 Connection 被销毁后 epoll 仍持有引用导致的悬空调用。

## 文档

公开文档在 `docs/`：`启动指南.md`（运行流程 + 四个动手实验）、`项目说明.md`（架构、线程与所有权、
设计取舍、协议、项目结构、实测数字、已知边界）、`演示.md`（占位）、`diagrams/`（架构图与导出约定）。
根目录只有本文件与 `README.md`（项目介绍 + 最短路径 + 分支说明）。

`notes/` 是本地笔记目录，已在 `.gitignore` 中排除，不进版本库；原先 `doc/` 下的类图、数据流、
协议、实测记录与学习笔记都在那里。

改代码后要回头改文档里这几类容易变质的事实：文件数与行数（README 的「分支说明」）、
测试套件与用例数（README 与 `启动指南`）、实测数字（必须标清来源与条件）、
已删除或未接入的模块（例如 H.264 没有调用点，不能写成已接入）、`docs/` 内的相对链接。

## 规则

> 注：`notes/learn lists.md` 在 `.gitignore` 里（本地文件，不入库），
> 所以下面这条规则产出的内容只在本地存在。

### 新增功能后同步学习清单

每次为本项目添加新功能或引入新技术后，必须在 `notes/learn lists.md` 中追加相关的学习条目。要求：

1. **条目格式** — 在对应分类下添加 `- [标题](链接) — 一句话说明`，若无合适分类则新建分类
2. **链接优先级** — 优先官方文档、cppreference、man 手册页等第一手资料；其次用技术博客（如 CSDN、知乎专栏）；视频仅推荐 B 站 (`bilibili.com/video/`)
3. **内容范围** — 聚焦本次新增功能涉及的技术点（如新增 H.264 解析，则添加 H.264 NALU 结构、Annex B 格式相关的学习链接）
4. **不要重复** — 添加前先检查是否已有相同或高度相似的条目
5. **时机** — 在功能实现完成后、代码 commit 前完成追加
