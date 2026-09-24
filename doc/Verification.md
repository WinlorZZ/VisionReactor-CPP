# 实测记录（Verification）

本文记录一次完整、可复现的验证过程：干净重建、单元测试、Sanitizer、基准与端到端链路。
用途是回答面试里最容易被追问的三件事——**怎么测的、数字代表什么、哪些结论不能从这些数字推出来**。

- 验证日期：2026-09-24
- 被测版本：分支 `codex/resume-verify`（含热路径日志优化，见第 6 节）
- 原始日志：本次验证的脚本与原始输出保存在本地 `.verify/`（已在 `.gitignore` 中排除，不入库）

## 1. 环境

| 项 | 值 |
| --- | --- |
| CPU | AMD Ryzen 7 5700X（8 核 16 线程） |
| 内存 | 15 GiB |
| 系统 | Ubuntu 24.04.4 LTS on WSL2，内核 6.18.33.2-microsoft-standard-WSL2 |
| 编译器 | g++ 13.3.0 |
| CMake | 3.28.3 |
| gRPC | 1.51.1（apt `libgrpc++-dev`） |
| Protobuf | 3.21.12（apt） |
| OpenCV | 4.6.0（apt `libopencv-dev`） |
| 基准构建 | Release（`-O3 -DNDEBUG`） |
| Sanitizer 构建 | Debug + `-fsanitize=address,undefined` |

## 2. 干净重建

依赖只需 apt 一条命令（见 README）。CMake 配置与编译结果：

- 配置成功；编译**0 error、0 warning**
- `find_package(Protobuf)` 使用默认查找方式，同时兼容 apt 的模块模式与源码/vcpkg 的 config 模式
  （原先写死的 `CONFIG` 模式在 Ubuntu 24.04 上会直接配置失败）

## 3. 单元测试与 Sanitizer

`ctest` 共 6 个测试套件、15 个 GTest 用例：

| 套件 | 用例数 | 覆盖内容 |
| --- | --- | --- |
| `buffer_test` | 4 | 初始状态、追加/取出、动态扩容、搬移（Tighten） |
| `thread_pool_test` | 2 | 线程池初始状态、并发计数正确性 |
| `connection_test` | 3 | `shared_from_this`/`tie` 生命周期、高频创建销毁、跨线程发送回注 EventLoop |
| `h264_demuxer_test` | 3 | 4 字节起始码、多 NALU、前导垃圾数据 |
| `event_loop_test` | 1 | 跨线程投递并唤醒 poller（eventfd 路径） |
| `response_serializer_test` | 2 | 类别名转义 + TCP 长度前缀封包、错误 JSON 结构 |

结果：

- Release：`100% tests passed, 0 tests failed out of 6`
- ASan + UBSan：`100% tests passed, 0 tests failed out of 6`；
  日志中 `AddressSanitizer` / `runtime error:` / `LeakSanitizer` 关键字命中数为 **0**

## 4. 基准（Release，各跑 3 次）

### 4.1 Buffer 连续读写吞吐（`Buffer_bench`）

口径：单线程反复 `append(64 KiB) × 8` → `peek()` → `retrieve(512 KiB)`，共 10 万轮（约 50 GiB 数据量）；
同时断言运行后 Buffer 容量未膨胀（无持续重新分配）。

| 轮次 | Sustained Bandwidth |
| --- | --- |
| 1 | 50.08 GiB/s |
| 2 | 50.92 GiB/s |
| 3 | 47.45 GiB/s |
| 中位数 | **约 50 GiB/s** |

**这个数字能说明什么**：Buffer 的双游标 + 预留头 + 搬移策略在长时间大数据量下吞吐稳定、容量不膨胀。
**不能说明什么**：这是内存内拷贝吞吐，**不等于网卡吞吐，也不等于端到端帧处理能力**。

### 4.2 线程池吞吐（`ThreadPool_bench`）

口径：单线程连续提交 10 万个极轻量任务到 8 工作线程的池，统计全部完成所需墙钟时间；
TPS = 任务数 / 墙钟时间，`us/task` 是**均摊值**（墙钟时间 / 任务数），不是单任务排队延迟。

| 轮次 | TPS | 均摊每任务 |
| --- | --- | --- |
| 1 | 54 288.8 tasks/s | 18.42 µs |
| 2 | 55 741.4 tasks/s | 17.94 µs |
| 3 | 55 126.8 tasks/s | 18.14 µs |
| 中位数 | **约 5.5 万 tasks/s** | **约 18.1 µs** |

**不能说明什么**：该场景是「单生产者 + 8 消费者争抢同一把队列锁」的极端形态，
数字受锁竞争和 `packaged_task`/`future` 封装开销主导，不代表真实业务里每帧的处理延迟。

## 5. 端到端链路

### 5.1 实验设置

- 拓扑：`socket 客户端 → TCP(4 字节大端长度前缀 + JPEG) → C++ 网关(epoll ET) → 异步 gRPC → Python 节点 → JSON 回传`
- 输入：单张真实 JPEG，44 194 字节
- AI 节点：本节用**模拟节点**（`python_ai/dummy_server.py`，可控 `time.sleep` 模拟推理耗时，不依赖 torch/OpenCV），
  目的是把「网关与 gRPC 链路开销」从推理耗时里分离出来观察
- 连接数：单连接；两种模式各 30 帧
  - `seq`：发一帧等一帧
  - `burst`：一次性连发 30 帧，不等待

### 5.2 结果

`seq` 30 帧全部成功（墙钟 674 ms，约 44 帧/秒，受 20 ms 模拟推理限制）：

| 分段（微秒） | min | 中位数 | p95 |
| --- | --- | --- | --- |
| `total_us` 端到端 | 21 145 | 21 517 | 22 053 |
| `gateway_us` 网关侧合计 | 56 | **76** | 103 |
| ├ `parse_us` 到达→拆包 | 3 | 4 | 7 |
| ├ `queue_to_grpc_us` 拆包→发出 RPC | 2 | 3 | 5 |
| └ `postprocess_us` 回执→结果处理 | 50 | 68 | 87 |
| `grpc_round_trip_us` gRPC 往返 | 21 083 | 21 430 | 21 957 |
| ├ `infer_us` AI 侧自报推理 | 20 120 | 20 160 | 20 236 |
| └ `grpc_transport_us` 往返减推理 | 942 | 1 295 | 1 744 |

`burst` 30 帧：**2 帧受理、28 帧返回 `OVERLOADED`** —— 与「每连接最多 2 个在途请求，超限拒绝」的设计一致，
证明背压策略在真实负载下按预期生效（客户端可据此降帧率）。

### 5.3 结论与边界

- 网关自身处理在**百微秒级**（中位数 76 µs），端到端耗时由推理与 gRPC 往返主导。
- 明确不宣称：这不是高并发压测（单连接、无多客户端竞争）；没有 p99、没有长时间稳定性与内存曲线；
  `grpc_transport_us` 是「往返减推理」的差值估计，包含排队与序列化，**不等于纯网络传输耗时**。

## 6. 一次可量化的优化：热路径同步日志

第一次端到端测量时，`gateway_us` 中位数是 **929 µs**，其中 `parse_us` 占 **842 µs**。
44 KB 的拷贝不可能花掉 842 µs，逐段排查后定位到原因：`Connection::business()` 在 T1 计时区间内
有多条 `std::cout << ... << std::endl`，每条都触发一次 flush 写日志文件。

改动：热路径上不再做同步日志输出（保留探针与延迟报告），结果日志改用 `'\n'` 避免 flush，
并把日志移到探针之后，避免输出开销被计入 T4。改动后重测（同样 30 帧）：

| 指标（中位数） | 优化前 | 优化后 |
| --- | --- | --- |
| `gateway_us` | 929 µs | **76 µs** |
| `gateway_us` p95 | 1 084 µs | 103 µs |
| `parse_us` | 842 µs | **4 µs** |

这类问题单看代码看不出来，是分段探针直接指出来的——也是本项目保留 6 段延迟探针的实际用途。

## 7. 复现步骤

```bash
# 1. 依赖与构建
sudo apt install -y build-essential cmake libgrpc++-dev protobuf-compiler-grpc \
  libprotobuf-dev protobuf-compiler libopencv-dev libssl-dev
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel

# 2. 测试与基准
ctest --test-dir build-release --output-on-failure
./build-release/Buffer_bench
./build-release/ThreadPool_bench

# 3. 端到端（模拟 AI 节点，无需 GPU）
python3 -m venv .venv && . .venv/bin/activate
pip install grpcio grpcio-tools protobuf
(cd python_ai && python dummy_server.py --latency-ms 20) &
./build-release/server 127.0.0.1:50051 &
python3 tools/image_client.py --image test.jpg --host 127.0.0.1 --port 8888
```

真实推理只需把第 3 步的模拟节点换成 `python Pserver.py`（需要 torch + ultralytics，见 README）。
