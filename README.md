# VisionReactor-CPP: 高性能 C++ 异步 AI 视觉网关

![C++](https://img.shields.io/badge/C++-17-blue.svg) ![gRPC](https://img.shields.io/badge/gRPC-Async-green.svg) ![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey.svg) ![Build](https://img.shields.io/badge/Build-CMake-orange.svg) ![YOLO](https://img.shields.io/badge/AI-YOLOv8-red.svg)

基于 **C++17** 的高性能异步网络服务器引擎，采用 **Reactor 模型**与**线程池**架构，深度集成 **gRPC 异步非阻塞通信**，为视频流或高并发网关提供毫秒级 AI 视觉分析能力。当前版本已经支持浏览器上传本地视频、按帧抽取 JPEG、经 C++ 网关转发到 Python YOLOv8 推理服务，并将检测框与分段延迟实时回传到前端 Canvas 渲染。

## 核心特性

- **单 Reactor 事件驱动模型** — 基于 `epoll` 边缘触发 (ET) + 非阻塞 I/O，连接读写与协议拆包保持 EventLoop 线程归属
- **跨线程安全投递** — `eventfd + queueInLoop` 唤醒 Reactor，Worker 不直接修改 Channel、epoll 或连接 Buffer
- **异步 gRPC 微服务架构** — 采用 `CompletionQueue` 纯异步模型，请求发起后不阻塞等待推理，回调结果投递至线程池处理
- **连接生命周期管理** — `shared_ptr` / `weak_ptr` 管理 Connection，`Channel::tie` 在事件回调期间维持对象存活
- **浏览器实时演示** — C++ 网关直接支持 WebSocket Upgrade，浏览器以二进制帧发送 JPEG，网关以文本 JSON 帧回传检测结果
- **双协议入口** — 支持原始 TCP `4 字节大端长度 + JPEG/PNG`，也支持浏览器 WebSocket 二进制帧
- **低延迟背压** — 每连接默认最多 2 个在途 AI 请求，过载时返回 `OVERLOADED`，前端可自动降帧率
- **H.264 实验模块** — 内置 Annex B 格式 NALU 解复用器；当前尚未接入 AI 推理主链路
- **网关链路延迟追踪** — `FrameContext` 携带唯一 TraceID，覆盖 TCP/WebSocket 到达、拆包、gRPC、AI 推理与回执处理
- **双层 Buffer 设计** — 内部 `vector<char>` + 栈上 64KB `extrabuf`，结合 `readv` 分散读，在 ET 模式下高效读取变长数据

## 架构概览

```
Browser
  ├─ upload video
  ├─ canvas 抽帧为 JPEG
  └─ WebSocket binary frame
             │
             ▼
Client ──TCP/WebSocket──> Main Reactor (epoll_wait)
                            │
                            ▼
                      Acceptor (新连接) ──> Server::handleNewConnection
                            │
                            ▼
                      Connection (读事件) ──> Buffer::readFd (readv 分散读)
                            │
                            ▼
                      Loop 线程协议拆包/背压控制
                            │
                            ▼
                      AsyncAIEngine::AnalyzeFrameAsync
                            │
                            ▼ (gRPC CompletionQueue)
                      Python AI Node (YOLOv8) ──> FrameResponse
                            │
                            ▼
                      CQ 回调线程 ──> ThreadPool ──> JSON 序列化
                            │
                            ▼
                      weak_ptr<Connection> 安全回投 EventLoop
                            │
                            ▼
                      TCP 长度前缀响应 / WebSocket text frame

Worker 线程需要操作连接时，通过 `queueInLoop` 回投 EventLoop。

> 说明：视频帧本体始终走二进制 JPEG/PNG，不使用 JSON/base64。JSON 只用于前端结果层，承载检测框、类别、置信度和延迟数字，便于浏览器直接解析和调试。若后续追求更极限的结果编码开销，可将 `ResponseSerializer` 替换为 protobuf binary。
```

## 项目结构

```
.
├── include/                 # 头文件
│   ├── Acceptor.h           # TCP 连接接受器
│   ├── AsyncAIEngine.h      # gRPC 异步 AI 调用引擎
│   ├── Buffer.h             # 高性能可变长缓冲区 (readv + 协议解析)
│   ├── Channel.h            # epoll 事件通道抽象
│   ├── Connection.h         # TCP 连接生命周期管理
│   ├── Epoll.h              # epoll 封装
│   ├── EventLoop.h          # 事件循环
│   ├── InetAddress.h        # 网络地址封装
│   ├── LatencyProfiler.h    # 高精度计时 + TraceID + FrameContext
│   ├── ResponseSerializer.h # 检测结果 JSON 与 TCP 长度前缀封包
│   ├── Server.h             # 服务器主控 (组装所有组件)
│   ├── Socket.h             # Socket RAII 封装
│   ├── ThreadPool.h         # 动态线程池
│   ├── h264_demuxer.h       # H.264 Annex B NALU 解复用器
│   └── util.h               # 工具函数
├── src/                     # 源文件
│   ├── Acceptor.cpp
│   ├── AsyncAIEngine.cpp
│   ├── Channel.cpp
│   ├── Connection.cpp
│   ├── Epoll.cpp
│   ├── EventLoop.cpp
│   ├── InetAddress.cpp
│   ├── ResponseSerializer.cpp
│   ├── Server.cpp
│   └── Socket.cpp
├── tests/                   # 测试与基准
│   ├── buffer_test.cpp      # Buffer 单元测试
│   ├── thread_test.cpp      # ThreadPool 单元测试
│   ├── connection_test.cpp  # Connection 生命周期与拆包测试
│   ├── event_loop_test.cpp  # EventLoop 跨线程唤醒测试
│   ├── h264_demuxer_test.cpp
│   ├── response_serializer_test.cpp
│   ├── Buffer_bench.cpp     # Buffer 吞吐量基准
│   └── ThreadPool_bench.cpp # ThreadPool 调度延迟基准
├── proto/
│   └── game_ai.proto        # gRPC 契约定义 (VisionAI 服务)
├── python_ai/
│   ├── Pserver.py           # YOLOv8 推理服务 (生产)
│   ├── dummy_server.py      # 模拟 AI 节点 (开发调试)
│   └── yolov8n.pt           # YOLOv8 nano 权重
├── doc/                     # 设计文档
│   ├── Class Diagram.md     # 类图
│   ├── Data Flow.md         # 数据流
│   ├── Output Buffer 发送状态机.md
│   ├── Protocol V1.md       # TCP/WebSocket 传输协议与 JSON 响应结构
│   ├── 全流程.drawio         # 流程图
│   └── h264.cpp             # H.264 解复用器独立调试代码
├── tools/
│   └── image_client.py      # 单图 TCP 端到端测试客户端
├── web_demo/
│   ├── index.html           # 浏览器视频实时推理演示
│   ├── app.js               # WebSocket 抽帧、发送、渲染与延迟面板
│   └── styles.css
├── main.cpp                 # 程序入口
├── Makefile                 # 构建、测试与基准入口
└── CMakeLists.txt
```

## 编译与运行

### 环境依赖

| 组件 | 版本要求 |
|------|---------|
| OS | Ubuntu 20.04+ / WSL2 |
| GCC | 9.0+ (支持 C++17) |
| CMake | 3.10+ |
| gRPC | 1.x |
| Protobuf | 3.x |
| OpenCV | 4.x |
| OpenSSL | 1.1+ / 3.x |
| pthread | 系统自带 |

### 编译

```bash
git clone https://github.com/WinlorZZ/VisionReactor-CPP.git
cd VisionReactor-CPP

# 安装系统依赖 (以 Ubuntu 为例)
sudo apt install build-essential cmake libopencv-dev libssl-dev
# gRPC/Protobuf 需从源码编译或通过 vcpkg 安装，参考 grpc.io 文档

make build
```

编译产物：
- `server` — 主程序
- `buffer_test`、`ThreadPool_test`、`connection_test` 等 — 单元测试
- `Buffer_bench`、`ThreadPool_bench` — 性能基准

### 运行

```bash
# 1. 启动 Python AI 推理节点
cd python_ai
pip install grpcio grpcio-tools protobuf opencv-python torch ultralytics
python Pserver.py

# 2. 启动 C++ 网关 (另开终端)
cd build
./server [AI节点地址:端口]   # 默认 127.0.0.1:50051
```

默认监听地址为 `127.0.0.1:8888`。浏览器演示和 TCP 客户端都连接这个端口。

### 浏览器视频演示

直接打开 `web_demo/index.html`，或用任意静态文件服务器打开：

```bash
cd web_demo
python3 -m http.server 8080
```

页面默认连接：

```text
ws://127.0.0.1:8888
```

使用流程：

1. 点击“连接”，完成浏览器到 C++ 网关的 WebSocket 握手。
2. 选择本地视频文件。
3. 点击“播放”，前端会按设定 FPS 抽帧为 JPEG 并发送。
4. 页面实时绘制检测框，并显示 `Total`、`Gateway`、`gRPC`、`Infer`、`In flight`。

### 单图 TCP 验证

不经过浏览器时，可以用脚本验证原始 TCP 协议闭环：

```bash
python tools/image_client.py --image test.jpg --host 127.0.0.1 --port 8888
```

成功时会打印网关返回的 JSON，包括检测框和延迟字段。

### 运行测试

```bash
make test
make bench
```

如果受限沙箱不允许测试创建真实 socket，`connection_test` 可能出现 `Operation not permitted`；在正常本机环境或提升权限后应通过。

## 前端/网关协议

当前协议见 `doc/Protocol V1.md`。

### Raw TCP

请求：

```text
[4 字节大端 body_length][JPEG/PNG bytes]
```

响应：

```text
[4 字节大端 body_length][UTF-8 JSON]
```

一条连接允许连续发送多帧，网关负责处理半包和粘包。

### WebSocket

- 浏览器发给网关：binary WebSocket frame，payload 是 JPEG/PNG bytes。
- 网关发给浏览器：text WebSocket frame，payload 是 UTF-8 JSON。
- 第一版支持完整非分片 WebSocket frame。

### JSON 响应字段

成功响应示例：

```json
{
  "frame_id": 1,
  "ok": true,
  "error": "",
  "timing": {
    "total_us": 0,
    "gateway_us": 0,
    "parse_us": 0,
    "queue_to_grpc_us": 0,
    "grpc_round_trip_us": 0,
    "grpc_transport_us": 0,
    "infer_us": 0,
    "postprocess_us": 0
  },
  "detections": [
    {
      "class": "person",
      "confidence": 0.95,
      "x": 100,
      "y": 120,
      "w": 80,
      "h": 160
    }
  ]
}
```

`x/y/w/h` 使用 YOLO 返回的原图像素坐标，其中 `x/y` 是检测框中心点。

## 类关系图

```mermaid
classDiagram
    Server *-- Acceptor
    Server *-- ThreadPool
    Server *-- AsyncAIEngine
    Server o-- EventLoop
    Server o-- Connection

    Acceptor *-- Socket
    Acceptor *-- Channel
    Acceptor o-- EventLoop

    Connection *-- Socket
    Connection *-- Channel
    Connection *-- Buffer
    Connection ..> AsyncAIEngine : 使用

    AsyncAIEngine o-- ThreadPool : 回调投递

    EventLoop *-- Epoll
    Channel o-- EventLoop
    Epoll o-- Channel : epoll_event

    class Server {
        -EventLoop *loop
        -Acceptor *acceptor
        -ThreadPool *threadPool
        -AsyncAIEngine aiengine
        -map~int,shared_ptr~ conns
        +handleNewConnection(Socket*)
        +handleOnMessage(shared_ptr~Connection~)
    }

    class Connection {
        -State {kConnected, kDisconnecting, kDisconnected}
        -ClientProtocol {TcpLengthPrefixed, WebSocket}
        -Socket *sock
        -Channel *channel
        -Buffer *inputBuffer
        -Buffer *outputBuffer
        +handleReadEvent()
        +business(AsyncAIEngine*)
        +send(string)
        +completeAnalysis(string)
    }

    class AsyncAIEngine {
        -CompletionQueue cq_
        -thread cq_thread
        -ThreadPool* threadPool_
        +AnalyzeFrameAsync(FrameContextPtr, string&&, weak_ptr~Connection~)
    }

    class Epoll {
        -int epfd
        +poll() vector~Channel*~
        +updateChannel(Channel*)
    }

    class Channel {
        -int fd
        -uint32_t events
        +enableReading()
        +handleEvent()
    }

    class ThreadPool {
        -vector~thread~ workers
        -queue~function~ tasks
        +add(F&&) future
    }

    class Buffer {
        -vector~char~ buffer_
        -size_t readerIndex_
        -size_t writerIndex_
        +readFd(int, int*)
        +append(char*, size_t)
        +peekInt32()
    }
```

## gRPC 契约

```protobuf
service VisionAI {
  rpc AnalyzeFrame(FrameRequest) returns (FrameResponse) {}
}

message FrameRequest {
  uint64 frame_id = 1;       // 帧序号 (异步回调对齐)
  bytes  image_data = 2;      // 图像数据 (JPEG/PNG 编码)
  uint64 timestamp_ms = 3;    // 客户端时间戳
}

message FrameResponse {
  uint64 frame_id = 1;              // 回传帧序号
  repeated BBox boxes = 2;          // 检测框列表
  int64  inference_latency_us = 3;  // Python 推理耗时 (微秒)
}
```

## 网关链路延迟追踪

每帧携带 `FrameContext`，记录以下时间戳：

| 探针 | 位置 | 含义 |
|------|------|------|
| `t_start` | Connection::handleReadEvent | TCP 数据到达 |
| `t_parsed` | Connection::business | TCP/WebSocket 拆包完成 |
| `t_grpc_sent` | AsyncAIEngine | gRPC 请求已发出 |
| `t3_python_cost_us` | Python 返回 | AI 纯推理耗时 |
| `t_grpc_recv` | CompletionQueue 回调 | gRPC 回执到达 |
| `t_finish` | CQ 结果任务 | 结果处理完成 |

## 技术要点

- **Buffer 内存策略**：8 字节预留头 (TLV 封包) + 1KB 初始容量，扩容时优先内部搬移 (Tighten) 而非直接向 OS 申请
- **ET 模式读**：`readv` 双缓冲 (内部 buffer + 栈上 64KB)，单次系统调用尽可能读空内核缓冲区
- **Channel 安全提升**：读/写回调执行前通过 `weak_ptr` 检测上层对象存活，防止连接销毁后的悬空调用
- **连接状态机**：`kConnected → kDisconnecting → kDisconnected`，保证异步回调期间状态一致性
- **gRPC Tag 管理**：`unordered_map<void*, shared_ptr<AsyncClientCall>>` 保持 Call 对象生命周期，直到 CompletionQueue 返回
- **Connection 弱引用回传**：AI 回调只保存 `weak_ptr<Connection>`，客户端断开后结果会自然丢弃，不延长连接生命周期
- **ET 写排空**：`handleWriteEvent()` 循环写到 `EAGAIN/EWOULDBLOCK`，避免边缘触发模式下残留数据不再触发写事件
- **结果序列化隔离**：`ResponseSerializer` 独立负责 JSON、错误响应和 TCP 长度前缀封包，便于后续替换为 protobuf binary
