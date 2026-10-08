# VisionReactor-CPP：单 Reactor 异步 AI 视觉网关

> **这是 `slim` 分支：只保留 TCP 路径的核心链路**（完整版含 WebSocket / H.264 / 浏览器演示，在 `main` 分支）。
> 精简的动机很直接：**一个 1900 行的项目要能一口气读完，才讲得清。**

![C++](https://img.shields.io/badge/C++-17-blue.svg) ![gRPC](https://img.shields.io/badge/gRPC-Async-green.svg) ![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey.svg)

## 定位

**实验性项目。** 目标方向是「实时画面检查」——屏幕或相机画面实时检测并叠加标记。
本仓库是那条路的第一步：先把 **「帧进来 → 异步送推理 → 结果回来」** 这条管道和它的延迟搞明白。

所以这里**只有管道**：捕获与叠加显示都还没做。管道的部分有实测数据（见[实测](docs/03-实测与边界.md)），
它不是产品，也没有上线计划。

## 这是什么

C++17 编写的**单 Reactor 异步网关**：把图像帧从客户端送到 Python 推理服务，再把结果送回来。
它自己不解码、不识别、不存储任何图像。

```text
客户端 ──TCP──> [4 字节大端长度 + JPEG]
                      │
              epoll ET 收帧 → 双游标 Buffer 拆包
                      │
              异步 gRPC（CompletionQueue，不阻塞）
                      ▼
              Python YOLOv8 ──> FrameResponse
                      │
              CQ 线程 → ThreadPool → JSON
                      │
              weak_ptr 回投 EventLoop ──> [4 字节长度 + JSON]
```

处理一帧时，**网关自身只花约 70 微秒**；端到端耗时由推理主导（推理约 20 ms，占 99% 以上）。

## 核心特性

每一项都能在代码里指到具体位置：

- **单 Reactor + epoll ET + 非阻塞 I/O** — 连接的读、写、状态、Buffer 只在 EventLoop 线程修改
- **`readv` 双缓冲** — 内部 Buffer 可写区 + 栈上 64 KB `extrabuf`，一次系统调用尽量读空内核缓冲
- **双游标 Buffer** — 8 字节预留头；扩容前先做内部搬移（Tighten），避免频繁向 OS 申请
- **4 字节大端长度前缀** — 循环解析直到缓冲区耗尽或遇到不完整帧；半包时不移动读游标
- **写侧排空至 `EAGAIN`** — 并维持不变式「`outputBuffer` 非空 ⇔ `EPOLLOUT` 已注册」
- **`eventfd` + `queueInLoop`** — 任何非 Loop 线程想动连接，都必须回投 EventLoop 执行
- **异步 gRPC** — `CompletionQueue` + tag 生命周期表；**先登记 tag、后 `Finish()`**
- **每个 RPC 带 2 秒 deadline**（`AsyncAIEngine::kRpcDeadlineMs`）— 超时返回 `TIMEOUT` 并归还在途额度
- **`Channel::tie`（`weak_ptr`）** — 保证 epoll 事件回调期间 Connection 不被析构
- **每连接 2 个在途请求** — 超限立刻回 `OVERLOADED`，让发送端降帧（宁可丢帧，不排队）
- **6 段延迟探针** — T1 到达→拆包、T2 拆包→发出、T3 推理、T4 回执→结果处理

## 快速开始

环境：Ubuntu 22.04/24.04 或 WSL2；g++ 11+、CMake 3.16+。

```bash
sudo apt install -y build-essential cmake libgrpc++-dev protobuf-compiler-grpc \
  libprotobuf-dev protobuf-compiler
```

```bash
make release        # Release，跑基准与端到端用它
make build          # Debug
make asan           # Debug + ASan/UBSan（查越界、use-after-free、未定义行为）
make test-asan      # 跑 ASan 构建的测试
make test           # ctest：5 个套件 / 12 个用例
```

**GTest 来源按可控性取三级**：① 显式传 `-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=<本地源码>`；
② 系统装了 `libgtest-dev`；③ 都没有才从 GitHub 下载。
离线环境推荐 `sudo apt install libgtest-dev`，或 `make release GTEST_SRC=<本地 googletest 源码>`。

> ⚠️ **网络需要代理时，CMake 要单独设环境变量。**
> CMake 下载 GTest 用的是 libcurl，**它只读 `https_proxy`/`http_proxy` 环境变量，完全不读 git 的代理配置**。
> 实测：git 能 clone 而 CMake 不设代理时，配置阶段会卡约 86 秒后报 `Could not resolve host: github.com`；
> 设上之后 10 秒通过。
>
> ```bash
> export https_proxy=http://<你的代理>:<端口>
> export http_proxy=$https_proxy
> make release
> ```
>
> 最省事的办法是 `sudo apt install libgtest-dev`——之后完全不联网也能构建。

> ⚠️ **WSL 用户注意**：WSL 会继承 Windows 的 PATH，CMake 可能因此找到 `/mnt/d/...` 下
> **MSVC 编译的 GTest**（例如 Miniconda 自带的 `/mnt/d/miniconda3/Library/lib/cmake/GTest`），
> 链进来会爆出大量 `??_R4...` 未定义引用。本仓库已显式拒绝 `/mnt/<盘>/` 下的 GTest 并给出提示。

## 跑一遍端到端

```bash
# 终端 1：模拟 AI 节点（不需要 torch/GPU）
python3 -m venv .venv && .venv/bin/pip install -q grpcio grpcio-tools protobuf
cd python_ai && ../.venv/bin/python dummy_server.py --port 50051 --latency-ms 20 --boxes 2

# 终端 2：网关（默认监听 127.0.0.1:8888，AI 地址可传参）
./build-release/server 127.0.0.1:50051

# 终端 3：发一张图
.venv/bin/python tools/image_client.py --image <任意 JPEG>
```

换成真实推理只需把终端 1 换成 `python_ai/Pserver.py`（需要 torch + ultralytics）。

## 实测摘要

| 项 | 结果 |
| --- | --- |
| 单元测试 | 5 套件 / 12 用例全过；**ASan+UBSan 零报告** |
| 干净构建 | **0 error**（Release 约 45 s，16 核） |
| **跨主机真实推理**（Windows + RTX 4070 Ti SUPER） | `total_us` 中位 **16.8 ms**、`infer_us` 15.1 ms、**`gateway_us` 55 µs（0.33%）**，12/12 ok |
| 同链路（模拟节点，sleep 20 ms） | `gateway_us` 中位 **约 70 µs** |
| 背压 | 连发 30 帧 → **2 帧受理、28 帧 `OVERLOADED`**（真实推理下同样成立） |
| 超时与额度归还 | AI 推理 5 秒时 seq 3 帧**全部** `TIMEOUT`，且**无一帧** `OVERLOADED` |
| Buffer / 线程池基准 | 36.0 GiB/s（内存内拷贝） / 约 5.2 万 tasks/s |

**关键结论**：`gateway_us` 在**模拟节点和真实 YOLOv8 上处于同一水平**（55–99 µs）
——**网关自身开销与下游做什么无关**，端到端耗时几乎完全由推理决定。

**条件是单机或跨主机、单连接、WSL2。** 完整数字、口径、**机器状态敏感性**与**明确未验证的边界**见
[`docs/03-实测与边界.md`](docs/03-实测与边界.md)——引用任何数字之前请先读它。

## 文档

| 文档 | 内容 |
| --- | --- |
| **[docs/00-开始项目.md](docs/00-开始项目.md)** | **从 `clone` 到拿到第一个 JSON**（15 分钟，每步写了「应该看到什么」+ 排错表） |
| **[docs/05-带你过一遍.md](docs/05-带你过一遍.md)** | **教程：从全景到取舍，带你走完整个项目**（含 3 个实验与 20 道自测） |
| [docs/README.md](docs/README.md) | 文档索引，以及两张手绘设计图的说明（标清了哪处已过时） |
| [docs/01-架构与数据流](docs/01-架构与数据流.md) | 组件关系、线程归属表、四条不变式、一帧的完整流转 |
| [docs/02-设计决策](docs/02-设计决策.md) | 8 个决策：选了什么 / 为什么 / 不这么做会出什么问题 |
| [docs/03-实测与边界](docs/03-实测与边界.md) | 实测数字（含跨主机真实推理）、延迟口径、复现命令、边界 |
| [docs/04-代码导览](docs/04-代码导览.md) | 逐文件行数与职责、建议阅读顺序、六个容易看错的地方 |
| [docs/diagrams/](docs/diagrams/) | 手绘 `.drawio`：启动流程 / 一次连接的完整处理 / 类关系 |

## 已知边界（摘要）

- **无优雅停机**：没有 SIGINT/SIGTERM 处理，`loop()` 是死循环 → `Server::~Server()` 的顺序运行时跑不到
- **OutputBuffer 无上限**：只发不读的慢客户端能把它撑大
- **只有单连接验证**：没有并发压测、没有 p99、没有长稳曲线
- **`Epoll` / `Channel` / `AsyncAIEngine` / `Server` / `Acceptor` 无单测**
- **捕获与叠加未实现**（本仓库只到管道为止）

完整列表见 [`docs/03-实测与边界.md`](docs/03-实测与边界.md)。
