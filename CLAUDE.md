# CLAUDE.md

此文件为 Claude Code (claude.ai/code) 在此仓库中工作时提供指导。

## 构建与测试命令

```bash
# 常用目标（Makefile 里都有）
make release        # Release，跑基准与端到端用它 -> build-release/
make build          # Debug -> build/
make asan           # Debug + ASan/UBSan -> build-asan/
make test           # ctest（5 套件 / 12 用例）
make test-asan      # 跑 ASan 构建的测试
make bench          # Buffer / ThreadPool 基准

# 单目标
cmake --build build-release --target buffer_test --parallel

# 运行单个测试
./build-release/buffer_test
./build-release/connection_test

# 启动服务（需先启动 Python AI 节点）
cd python_ai && python Pserver.py &
./build-release/server [ai地址:端口]   # 默认: 127.0.0.1:50051
```

依赖项：gRPC、Protobuf、pthread（OpenCV 与 OpenSSL 已在 slim 分支移除）。Ubuntu 22.04/24.04 直接 apt 安装即可。

**GTest 三级来源**：① `-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=<本地源码>`（对应 `make release GTEST_SRC=...`）；
② 系统 `libgtest-dev`；③ 从 GitHub 下载。CMakeLists 会**拒绝 `/mnt/<盘>/` 下的 GTest**——
WSL 继承 Windows PATH 后会找到 Miniconda 里的 MSVC 版，链进来必然失败。

## 架构

这是一个 C++17 异步网络网关，通过 TCP 接收视频帧，经异步 gRPC 分发给 Python YOLOv8 服务进行推理，再将结果返回客户端。

**数据流：** `Client → epoll (ET) → Connection::handleReadEvent → Connection::business → AsyncAIEngine::AnalyzeFrameAsync → gRPC CompletionQueue → Python YOLOv8 → CQ 回调 → ThreadPool → 结果处理/延迟日志`

**线程归属：** Connection 的读写 Buffer、Channel 和 epoll 状态只允许在 EventLoop 线程修改。其他线程通过 `EventLoop::queueInLoop` 投递闭包，并由 `eventfd` 唤醒 Reactor。

**所有权链：** `Server` 持有 `EventLoop`（心跳）、`Acceptor`（监听 Socket）、`ThreadPool`（工作线程）、`AsyncAIEngine`（gRPC 存根 + CQ 线程），以及一个以 fd 为键的 `map<int, shared_ptr<Connection>>`。`Connection` 持有自身的 `Socket`、`Channel` 和两个 `Buffer`（输入/输出）。`AsyncAIEngine` 持有 gRPC `CompletionQueue` 及其轮询线程，以及一个 `map<tag, shared_ptr<AsyncClientCall>>` 用于管理在途 RPC。

**关键类：**
- `EventLoop` / `Epoll` — epoll 封装；`EventLoop::loop()` 阻塞于 `epoll_wait` 并分发活跃的 `Channel`
- `Channel` — 将 fd 与读/写回调绑定；`tie()` 存的是 **`weak_ptr`**（不增引用计数），只在 `handleEvent` 期间提升成局部 guard 来保证回调期间对象不被析构
- `Connection` — 状态机（`kConnected → kDisconnecting → kDisconnected`）；处理 TCP 组帧（4 字节大端长度前缀）与每连接在途上限
- `Buffer` — `vector<char>` + 读写游标 + 8 字节预留头；ET 模式下使用 `readv` 配合 64KB 栈缓冲区读取；`peekInt32` 处理网络字节序
- `ThreadPool` — 固定数量工作线程，基于 `std::future` 的任务提交
- `AsyncAIEngine` — 封装 gRPC `CompletionQueue`；`AnalyzeFrameAsync` 发起异步 RPC，`AsyncCompleteRpc`（独立线程）处理完成事件并将结果重新投递至 ThreadPool
- `LatencyProfiler` / `FrameContext` — 逐帧计时，包含 6 个探针点（T1–T4）和一个全局 atomic trace-id 发号器

**回调绑定（Server → Connection）：**
- `Server::handleNewConnection` 绑定到 `Acceptor::newConnectionCallback` — accept 时调用
- `Server::handleOnMessage` 绑定到 `Connection::onMessageCallback` — 完整帧读取完毕后**在 EventLoop 线程直接**调用 `conn->business(engine)`（gRPC 本身已是异步，无需再投线程池）
- `Server::handleDeleteConnection` 绑定到 `Connection::deleteConnectionCallback` — 从 `Server::conns` 映射中移除连接

**线协议：** 4 字节大端 body 长度 + body 数据。`Connection::business` 循环解析帧，直到缓冲区耗尽或遇到不完整帧。

**线程安全：** `AsyncAIEngine::active_calls_` 由 `mu_` 保护；`ThreadPool::tasks` 由其内部 mutex 保护。`Connection` 对象跨线程共享——`Channel::tie` 机制（weak_ptr）可防止 Connection 被销毁后 epoll 仍持有引用导致的悬空调用。

## 文档

项目文档全部在 `docs/`（根目录只留本文件与 `README.md`）。**阅读顺序：00（跑通）→ 06（动手验证）→ 05（系统理解），01–04 是参考手册。**

- `docs/00-开始项目.md` — 从 clone 到拿到第一个 JSON 的完整流程（每步附「应该看到什么」与排错表）
- `docs/06-动手验证.md` — 四个实验：半包、去掉 tie、让推理挂住、持续发帧；每个命令标了属于哪个终端
- `docs/05-带你过一遍.md` — 线性教程：全景 → 流程 → 数据链路 → 内存管理 → 取舍 → 20 道自测
- `docs/01-架构与数据流.md` — 组件关系、线程归属表、四条不变式、一帧的完整流转
- `docs/02-设计决策.md` — 每个决策的「选了什么 / 为什么 / 不这么做会出什么问题」
- `docs/03-实测与边界.md` — 实测数字（含跨主机真实推理）、延迟口径、复现命令、未验证边界
- `docs/04-代码导览.md` — 逐文件行数与职责、建议阅读顺序、六个易错点
- `docs/diagrams/` — 手绘 `.drawio`（不是旧文档：主体仍与当前代码一致，过时处已在索引里标注）

**注意**：`docs/00` 里的示例输出都是实际捕获的。改代码后若输出格式变了（例如探针格式、JSON 字段、
启动日志），要同步更新 00 与 03。

## 规则

### 改动代码后同步 `docs/`

文档必须描述代码里**真实存在**的东西。改了实现就要回头改文档，尤其是这几类容易变质的数字与事实：

1. **行数** — `docs/04` 有逐文件行数表；增删代码后要重算
2. **用例数** — `README.md` 与 `docs/04` 都写了「5 套件 / 12 用例」
3. **已删除的模块** — 文档里不得再出现被移除的模块（历史上出现过 H.264 / WebSocket / OpenCV 的残留描述）
4. **实测数字** — 必须标清来源（本次实测 / 完整版时代记录）与条件（单连接？模拟节点还是真实模型？）
5. **链接** — `docs/` 内相对链接在文件移动后要跟着改

代码里不能有已删模块的残留引用；`grep -rniE 'h264|opencv|openssl|websocket|bzero' src include main.cpp` 应为 0 处。
