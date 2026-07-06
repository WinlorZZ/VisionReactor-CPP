# VisionReactor-CPP: 高性能 C++ 异步 AI 视觉网关

![C++](https://img.shields.io/badge/C++-17-blue.svg) ![gRPC](https://img.shields.io/badge/gRPC-Async-green.svg) ![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey.svg) ![Build](https://img.shields.io/badge/Build-CMake-orange.svg) ![YOLO](https://img.shields.io/badge/AI-YOLOv8-red.svg)

基于 **C++17** 的高性能异步网络服务器引擎，采用 **Reactor 模型**与**线程池**架构，深度集成 **gRPC 异步非阻塞通信**，为游戏或高并发网关提供毫秒级 AI 视觉分析能力。

## 核心特性

- **单 Reactor 事件驱动模型** — 基于 `epoll` 边缘触发 (ET) + 非阻塞 I/O，连接读写与协议拆包保持 EventLoop 线程归属
- **跨线程安全投递** — `eventfd + queueInLoop` 唤醒 Reactor，Worker 不直接修改 Channel、epoll 或连接 Buffer
- **异步 gRPC 微服务架构** — 采用 `CompletionQueue` 纯异步模型，请求发起后不阻塞等待推理，回调结果投递至线程池处理
- **连接生命周期管理** — `shared_ptr` / `weak_ptr` 管理 Connection，`Channel::tie` 在事件回调期间维持对象存活
- **H.264 实验模块** — 内置 Annex B 格式 NALU 解复用器；当前尚未接入 AI 推理主链路
- **网关链路延迟追踪** — `FrameContext` 携带唯一 TraceID，覆盖 TCP 到达、拆包、gRPC、AI 推理与回执处理
- **双层 Buffer 设计** — 内部 `vector<char>` + 栈上 64KB `extrabuf`，结合 `readv` 分散读，在 ET 模式下高效读取变长数据

## 架构概览

```
Client ──TCP──> Main Reactor (epoll_wait)
                    │
                    ▼
              Acceptor (新连接) ──> Server::handleNewConnection
                    │
                    ▼
              Connection (读事件) ──> Buffer::readFd (readv 分散读)
                    │
                    ▼
              Loop 线程协议拆包 ──> AsyncAIEngine::AnalyzeFrameAsync
                    │
                    ▼ (gRPC CompletionQueue)
              Python AI Node (YOLOv8) ──> FrameResponse
                    │
                    ▼
              CQ 回调线程 ──> ThreadPool ──> 结果处理/延迟日志

Worker 线程需要操作连接时，通过 `queueInLoop` 回投 EventLoop。
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
│   ├── Server.cpp
│   └── Socket.cpp
├── tests/                   # 测试与基准
│   ├── buffer_test.cpp      # Buffer 单元测试
│   ├── thread_test.cpp      # ThreadPool 单元测试
│   ├── connection_test.cpp  # Connection 生命周期与拆包测试
│   ├── event_loop_test.cpp  # EventLoop 跨线程唤醒测试
│   ├── h264_demuxer_test.cpp
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
│   ├── 全流程.drawio         # 流程图
│   └── h264.cpp             # H.264 解复用器独立调试代码
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
| pthread | 系统自带 |

### 编译

```bash
git clone https://github.com/WinlorZZ/VisionReactor-CPP.git
cd VisionReactor-CPP

# 安装系统依赖 (以 Ubuntu 为例)
sudo apt install build-essential cmake libopencv-dev
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

### 运行测试

```bash
make test
make bench
```

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
        -Socket *sock
        -Channel *channel
        -Buffer *inputBuffer
        -Buffer *outputBuffer
        +handleReadEvent()
        +business(AsyncAIEngine*)
        +send(string)
    }

    class AsyncAIEngine {
        -CompletionQueue cq_
        -thread cq_thread
        -ThreadPool* threadPool_
        +AnalyzeFrameAsync(FrameContextPtr, string&&)
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
| `t_parsed` | Connection::business | 粘包拆分完成 |
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
