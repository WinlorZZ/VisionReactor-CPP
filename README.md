# VisionReactor-CPP

![C++](https://img.shields.io/badge/C++-17-blue.svg) ![gRPC](https://img.shields.io/badge/gRPC-Async-green.svg) ![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey.svg) ![Build](https://img.shields.io/badge/Build-CMake-orange.svg) ![YOLO](https://img.shields.io/badge/AI-YOLOv8-red.svg)

基于 C++17 的异步视觉推理网关。收一帧图，转发给 Python 侧的 YOLOv8，把检测框和分段延迟写成 JSON 发回去。
支持原始 TCP（4 字节大端长度前缀）与 WebSocket 二进制帧；推理走异步 gRPC，网关线程不被 RPC 阻塞。

具体见[项目说明](docs/项目说明.md)。

实测端到端 16.7 ms，推理 14.3 ms，网关侧 63 µs。口径见[项目说明](docs/项目说明.md)。

## 快速开始

```bash
# 1. 克隆
git clone https://github.com/WinlorZZ/VisionReactor-CPP.git && cd VisionReactor-CPP

# 2. 安装依赖（Ubuntu 22.04/24.04，联网即可）
sudo apt install -y build-essential cmake libgrpc++-dev protobuf-compiler-grpc \
  libprotobuf-dev protobuf-compiler libssl-dev libopencv-dev

# 3. 编译（googletest 会联网自动拉取）
make build

# 4. 准备推理节点（新终端，模型权重首次运行自动下载）
cd python_ai && pip install grpcio grpcio-tools protobuf numpy opencv-python torch ultralytics
python -u Pserver.py

# 5. 准备网关（新终端）
cd build && ./server 127.0.0.1:50051

# 6. 发送图片（新终端，可以使用仓库自带的测试图）
python3 tools/image_client.py --image testdata/test.jpg
```

第 6 步会打印一段 JSON。

离线构建、代理配置、排错表，以及动手实验等其他内容见[启动指南](docs/启动指南.md)。

## 文档

| 板块 | 内容 |
| --- | --- |
| **项目介绍** | 本文件：定位、启动路径、分支差异 |
| [启动指南](docs/启动指南.md) | 完整运行流程、排错表、四个动手实验 |
| [项目说明](docs/项目说明.md) | 架构、线程与所有权、设计取舍、协议、项目结构、实测数字、已知边界 |
| [演示](docs/演示.md) | 浏览器实时演示的截图与录屏（待补） |
| [图的源文件](docs/diagrams/说明.md) | 架构图的来源，以及 draw.io 导出与引用约定 |

## 分支说明

本仓库有两个分支，代码与文档都不同：

| | `main`（你在这里） | `slim` |
| --- | --- | --- |
| 内容 | 完整版：WebSocket 直连、H.264 解复用模块、浏览器实时演示 | 精简版：只保留 TCP 核心链路 |
| 规模 | 60 个文件 / 生产代码 2467 行 | 56 个文件 / 生产代码 1945 行 |
| 文档 | `docs/`：启动指南 / 项目说明 / 演示 | `docs/` 00–06：上手流程、逐段教程、架构与取舍、实测与边界、代码导览、动手实验 |
| 构建 | `make build`（Debug，产物 `build/server`） | `make release`（Release，产物 `build-release/server`） |
| 依赖 | 还要 OpenCV + OpenSSL | 只要 gRPC + Protobuf + pthread |
| GTest | 只能从 GitHub 下载（没有 `find_package`） | 有三级来源，装 `libgtest-dev` 即可离线 |
| 测试套件 | 6 个 | 5 个 |

两个分支共用同一套机制：边缘触发拆包、跨线程投递、弱引用生命周期、异步 gRPC、背压。
想先把机制读透，切到 `slim` 看它的 `docs/01`–`docs/05`。

```bash
git clone -b slim https://github.com/WinlorZZ/VisionReactor-CPP.git
cd VisionReactor-CPP && ls docs/
```
