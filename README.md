# 🚀 VisionReactor-CPP: 高性能 C++ 异步 AI 视觉网关服务器

![C++](https://img.shields.io/badge/C++-17-blue.svg) ![gRPC](https://img.shields.io/badge/gRPC-Async-green.svg) ![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey.svg) ![Build](https://img.shields.io/badge/Build-CMake-orange.svg)

本项目是一个基于 **C++17** 研发的高性能、高并发网络服务器引擎。它采用 **Reactor 模型**与**线程池**架构，并深度集成了 **gRPC 异步非阻塞通信**，旨在为服务器或高并发网关提供毫秒级的 AI 视觉分析能力。

## ✨ 核心架构亮点

* **⚡ 主从 Reactor 高并发模型**
  * 基于 `epoll` 边缘触发 (ET) 模式，结合非阻塞 IO，实现高效的网络事件分发。
  * 主线程专职监听新连接（Acceptor），拒绝任何业务阻塞。
* **🧵 动态工作线程池 (Thread Pool)**
  * 采用 `std::mutex` 与 `std::condition_variable` 实现高效任务队列。
  * 将网络 I/O 读写与具体业务逻辑解耦，最大化利用多核 CPU 性能。
* **🤖 跨语言 AI 微服务架构 (C++ -> Python)**
  * 摒弃传统的同步 RPC 阻塞调用，采用 **gRPC 纯异步模型 (CompletionQueue)**。
  * C++ Worker 线程发起 AI 图像推理请求后 **0 阻塞立即返回**，回调结果由守护线程重新投递至线程池，实现极致的吞吐量。
* **🛡️ 内存安全与对象生命周期管理**
  * 全面拥抱现代 C++，使用 `std::shared_ptr` 和 `std::weak_ptr` 管理 TCP Connection 对象，完美解决多线程环境下的“野指针”和连接意外断开导致的内存泄漏问题。

## ⚙️ 架构拓扑图

1. `Client` 发送画面帧。
2. `Main Reactor` 通过 `epoll` 监听到读事件。
3. 任务下发至 `Worker Thread`，进行数据反序列化。
4. `Worker Thread` 通过 `gRPC Async` 将画面零拷贝发送至 AI 节点。
5. AI 节点返回推理结果，触发 `CompletionQueue` 回调，由线程池接管并返回给 `Client`。

## 🛠️ 编译与运行指南

### 环境依赖
* OS: Ubuntu 20.04+ / WSL2
* 编译器: GCC 9.0+ (支持 C++17)
* 构建工具: CMake 3.10+
* 核心库: gRPC, Protobuf, pthread

### 快速使用
```bash
# 1. 克隆项目
git clone [https://github.com/YourName/VisionReactor-CPP.git](https://github.com/YourName/VisionReactor-CPP.git)
cd VisionReactor-CPP

# 2. 编译 C++ 主引擎
mkdir build && cd build
cmake ..
make -j4

# 3. 启动 Python AI 模拟节点
cd ../python_ai
pip install grpcio grpcio-tools protobuf
python dummy_server.py

# 4. 启动 C++ 网关 (另开终端)
cd build
./vision_server