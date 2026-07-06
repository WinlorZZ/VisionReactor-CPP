# VisionReactor-CPP 实现路线与验收清单

> 最终目标：浏览器播放视频并按固定帧率抽取 JPEG，经 WebSocket → TCP → C++ Reactor → gRPC → YOLO 完成推理，再把检测框返回浏览器绘制。
>
> 当前原则：先跑通单张图片闭环，再接视频；第一版不直接传 H.264。

## 分工标记

- 👤 **你主写**：核心后端能力，建议先自己分析和写第一版，可以随时让 AI 解释、搭测试骨架和审查。
- 🤝 **结对完成**：你负责理解和关键决策，AI 可以直接协助实现较难边界。
- 🤖 **AI 主写**：前端、桥接、重复工程工作和文档；你负责运行、观察和理解接口。

## 推荐协作流程

每个“你主写”的任务按以下方式完成：

1. 你先阅读关联文件，写出实现思路或伪代码。
2. 可以让 AI 解释 API、生成测试骨架，但先不要直接索要最终实现。
3. 你提交第一版代码。
4. AI 做 code review，并运行单测、ASan/UBSan；并发部分再运行 TSan。
5. 你至少亲手修正一轮问题。
6. AI 完成最终兜底、回归测试和文档同步。

---

# 阶段 0：冻结稳定底座

## 0.1 阅读并确认当前稳定化修改 👤

- [ ] 能解释 `eventfd + queueInLoop` 的跨线程投递路径。
- [ ] 能解释为什么 Connection 的 Buffer、Channel 和状态必须归 EventLoop 线程管理。
- [ ] 完成 `doc/稳定化学习检查.md` 中至少前 8 题。

关联文件：

- `src/EventLoop.cpp`
- `src/Connection.cpp`
- `src/Channel.cpp`
- `src/Epoll.cpp`

验收：

- 不看代码，能够画出 `Worker → queueInLoop → eventfd → epoll_wait → pendingFunctors_`。
- 允许忘记成员名，但必须讲清线程和对象之间的关系。

## 0.2 建立干净基线 🤝

- [ ] 执行 `make test`。
- [ ] 执行 ASan/UBSan 测试。
- [ ] 审查当前 diff，确认稳定化代码和文档。
- [ ] 提交 `codex/stabilize` 第一阶段改动。

验收：

- CTest 全部通过。
- Sanitizer 无错误。
- 工作区不包含 PID、日志和构建产物。

---

# 阶段 1：单张 JPEG 的 TCP → AI → TCP 闭环

这是当前最高优先级。完成后项目才算拥有真实业务闭环。

## 1.1 固定 MVP 线协议 🤝

第一版先保持简单：

```text
请求：[4 字节大端 body_length][JPEG/PNG bytes]
响应：[4 字节大端 body_length][UTF-8 JSON]
```

建议响应 JSON：

```json
{
  "frame_id": 1,
  "ok": true,
  "error": "",
  "timing": {
    "total_us": 0,
    "infer_us": 0
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

- [ ] 在 `doc/Protocol V1.md` 中写明字节序、最大包长和错误响应。
- [ ] 明确坐标使用 YOLO 返回的原图像素坐标。
- [ ] 明确一条 TCP 连接允许连续发送多帧。

你需要理解：

- TCP 没有消息边界。
- 为什么请求与响应都要带长度前缀。
- 为什么异步返回必须包含 `frame_id`。

验收：

- 根据协议文档，可以独立写出 Python 客户端。
- 半包和粘包情况下仍能解析。

## 1.2 将异步 RPC 与原 Connection 安全关联 👤

修改范围：

- `include/AsyncAIEngine.h`
- `src/AsyncAIEngine.cpp`
- `src/Connection.cpp`

实现任务：

- [ ] `AnalyzeFrameAsync()` 增加 `weak_ptr<Connection>` 参数。
- [ ] `AsyncClientCall` 保存 Connection 弱引用。
- [ ] CQ 返回后使用 `weak_ptr::lock()` 判断客户端是否仍存活。
- [ ] 客户端已经断开时丢弃结果，不访问 Connection。
- [ ] 不允许 AsyncAIEngine 通过裸指针延长或猜测 Connection 生命周期。

你先写的部分：

- `AsyncClientCall` 字段。
- `AnalyzeFrameAsync()` 参数传递。
- CQ 回调中的 `weak_ptr::lock()` 分支。

AI 可以协助：

- 补测试替身。
- 检查 CQ、ThreadPool 和 EventLoop 之间的生命周期。
- 审查停机竞态。

验收：

- AI 返回前客户端断开，程序不崩溃。
- ASan 无 use-after-free。
- 能解释为什么这里使用 `weak_ptr` 而不是 `shared_ptr`。

## 1.3 序列化检测结果并回传 🤝

- [ ] 将 `FrameResponse` 转为 JSON。
- [ ] 成功响应包含 `frame_id`、检测框和延迟。
- [ ] RPC 失败也生成统一错误 JSON。
- [ ] JSON 外层增加 4 字节大端长度。
- [ ] 调用 `Connection::send()`，让实际写操作回投 EventLoop。

建议新增：

```text
include/ResponseSerializer.h
src/ResponseSerializer.cpp
tests/response_serializer_test.cpp
```

你主写：

- JSON 字段设计。
- 长度前缀封包过程。

AI 协助：

- JSON 字符串转义。
- 浮点值、错误字段和测试用例。

验收：

- TCP 客户端收到完整 JSON。
- 多个检测框可正确解析。
- 类别名含引号或特殊字符时 JSON 仍合法。

## 1.4 修复 ET 写排空 👤

当前 `handleWriteEvent()` 每次只调用一次 `write()`，需要改成排水循环。

- [ ] 当 `outputBuffer` 非空时持续 `write()`。
- [ ] `EINTR`：立即重试。
- [ ] `EAGAIN/EWOULDBLOCK`：保留 Buffer，等待下一次 EPOLLOUT。
- [ ] Buffer 清空：注销 EPOLLOUT。
- [ ] 其他错误：关闭连接。
- [ ] `kDisconnecting` 状态下，排空后再释放 Connection。

建议伪代码：

```text
while outputBuffer 非空:
    n = write(...)
    if n > 0: retrieve(n)
    else if EINTR: continue
    else if EAGAIN: break
    else: 关闭连接

if outputBuffer 已空:
    disableWriting()
```

验收测试：

- [ ] 小消息一次写完。
- [ ] 大消息发生部分写。
- [ ] 慢客户端使发送缓冲区写满。
- [ ] 排空后不再持续触发 EPOLLOUT。

## 1.5 单张图片端到端客户端 🤖

建议新增：

```text
tools/image_client.py
```

功能：

- [ ] 读取 JPEG。
- [ ] 按 V1 协议发送。
- [ ] 正确读取 4 字节响应长度和完整 JSON。
- [ ] 打印检测框与延迟。
- [ ] 支持 `--host`、`--port`、`--image`。

验收命令示例：

```bash
python tools/image_client.py --image test.jpg
```

成功标准：

```text
JPEG → C++ TCP → gRPC → Python YOLO → C++ → TCP JSON
```

---

# 阶段 2：可靠性、背压与容量控制

## 2.1 限制在途 RPC 数量 👤

问题：视频产生帧的速度可能高于 YOLO 推理速度。

- [ ] 统计每条 Connection 的在途请求数。
- [ ] 设置上限，例如每连接 2 帧。
- [ ] RPC 完成或失败后正确归还额度。
- [ ] 客户端过快时执行明确策略。

第一版推荐策略：

```text
最多 2 帧在途；
达到上限后拒绝新帧并返回 OVERLOADED；
视频前端收到后主动降帧率。
```

后续可升级为“丢旧帧、保留最新帧”。

验收：

- 连续发送 100 帧且 AI 很慢时，内存不会持续增长。
- 在途计数不会因为错误、取消或断连而永久泄漏。

## 2.2 OutputBuffer 上限 👤

- [ ] 为单连接输出缓冲区设置最大值，例如 4 MiB。
- [ ] 超限时记录原因并断开慢客户端，或返回过载错误。
- [ ] 不能允许客户端长期不读响应导致服务器 OOM。

验收：

- 模拟客户端只发送不读取。
- 服务器内存保持有界。

## 2.3 gRPC deadline 和错误映射 🤝

- [ ] 每个 RPC 设置 deadline，例如 2 秒。
- [ ] 区分超时、AI 不可达、请求非法、服务取消。
- [ ] 将 gRPC 状态映射为稳定的客户端错误码。
- [ ] 错误路径也必须释放在途额度。

验收：

- AI 服务未启动时，客户端不会无限等待。
- 模拟 AI 睡眠超过 deadline，收到明确超时响应。

## 2.4 异步结果乱序策略 🤝

- [ ] 每帧携带唯一 `frame_id`。
- [ ] 前端仅绘制不早于当前显示结果的检测框。
- [ ] 旧结果到达时丢弃，不覆盖新结果。

验收：

- 模拟第 2 帧比第 1 帧先返回。
- 浏览器最终显示第 2 帧结果。

---

# 阶段 3：视频 Web Demo

这一阶段主要由 AI 实现，你负责看懂协议和运行链路。

## 3.1 WebSocket → TCP Bridge 🤖

建议目录：

```text
web_demo/
├── bridge.py
└── requirements.txt
```

- [ ] 浏览器 WebSocket 二进制帧转为 TCP V1 请求。
- [ ] 正确处理 TCP 半包响应。
- [ ] 每个浏览器连接对应独立 TCP 连接。
- [ ] TCP/WS 任一端断开时清理另一端。
- [ ] 设置消息大小与连接超时。

验收：

- 浏览器发一张图片，可收到 JSON。
- 多浏览器连接互不串帧。

## 3.2 浏览器视频抽帧 🤖

建议目录：

```text
web_demo/frontend/
├── index.html
├── app.js
└── style.css
```

- [ ] 上传并播放本地视频。
- [ ] Canvas 或离屏 Canvas 抽取 JPEG。
- [ ] 支持调整发送 FPS，例如 1～10。
- [ ] 同时只允许有限数量帧在途。
- [ ] WebSocket 未连接或过载时暂停发送。

验收：

- 5 FPS 连续运行 5 分钟。
- 浏览器、Bridge 和 C++ 内存不持续增长。

## 3.3 检测框与延迟面板 🤖

- [ ] 根据原始视频尺寸缩放检测框。
- [ ] 显示类别、置信度和 frame_id。
- [ ] 显示发送 FPS、响应 FPS、推理延迟和端到端延迟。
- [ ] 丢弃乱序旧结果。
- [ ] 显示断线、超时和过载状态。

验收：

- 视频缩放后检测框位置仍正确。
- AI 暂停或断开时页面给出清晰提示。

## 3.4 一键启停 🤖

- [ ] `make start` 启动 C++、Bridge 和前端静态服务器。
- [ ] `make stop` 清理进程。
- [ ] `.pids/` 和日志不进入 Git。
- [ ] `make status` 显示各组件状态。

---

# 阶段 4：测试与交付质量

## 4.1 协议单元测试 👤

- [ ] 单个完整包。
- [ ] 包头拆成多个 TCP 分片。
- [ ] body 拆分。
- [ ] 多包粘连。
- [ ] 长度为 0。
- [ ] 超过最大长度。
- [ ] 非法 magic/version（协议 V2 再启用）。

## 4.2 Connection 并发与生命周期测试 🤝

- [ ] AI 返回前客户端断开。
- [ ] Worker/CQ 同时向多个 Connection 返回。
- [ ] 慢客户端触发 OutputBuffer。
- [ ] Connection 析构后队列里仍有发送闭包。
- [ ] 服务停机时仍存在 RPC。

## 4.3 端到端集成测试 🤖

- [ ] 启动 dummy AI。
- [ ] 启动 C++ 网关。
- [ ] 发送固定测试图片。
- [ ] 验证 frame_id、框数据和错误码。
- [ ] 测试 AI 不可用和超时。

## 4.4 动态检查 🤖

- [ ] ASan：越界、UAF、泄漏。
- [ ] UBSan：未定义行为。
- [ ] TSan：跨线程数据竞争。
- [ ] 所有检查加入文档，CI 条件允许时自动执行。

---

# 阶段 5：优雅停机与可观测性

## 5.1 信号与停机顺序 🤝

- [ ] 处理 `SIGINT/SIGTERM`。
- [ ] 信号处理只设置标志或写唤醒 fd，不执行复杂析构。
- [ ] 停止 Acceptor 接入新连接。
- [ ] 取消或排空在途 RPC。
- [ ] 停止 CompletionQueue。
- [ ] 停止 ThreadPool。
- [ ] 释放 Connection、Channel、Socket 和 EventLoop。

验收：

- 推理过程中按 Ctrl+C，进程能在有限时间内正常退出。
- Sanitizer 不报告退出阶段 UAF。

## 5.2 结构化日志与指标 🤖

- [ ] 日志包含 connection_id、frame_id 和错误码。
- [ ] 记录当前连接数、在途 RPC、拒绝帧数、超时数。
- [ ] 区分 TCP 解析、gRPC 往返、AI 推理和响应发送延迟。

---

# 阶段 6：协议 V2（MVP 完成后）

不要在单张图片闭环前实现。

可能字段：

- magic
- version
- message_type
- flags
- client_frame_id
- timestamp
- width / height
- encoding
- payload_length

- [ ] 独立 `ProtocolCodec`，避免协议解析继续堆在 `Connection::business()`。
- [ ] 请求与响应共享统一包头。
- [ ] 支持协议版本校验和明确错误码。
- [ ] 对 64 位整数实现正确的网络字节序转换。

这一阶段推荐你主写设计，AI 实现部分机械编码与测试。

---

# 阶段 7：H.264/FFmpeg（可选增强）

当前 `H264Demuxer` 只能识别和切分 NALU，不能直接产生 YOLO 可推理的图像。

如果决定支持 H.264，还需要：

- [ ] 明确 TCP 中如何传递 H.264 字节流和时间戳。
- [ ] 识别 Access Unit，而不只是单个 NALU。
- [ ] 缓存 SPS/PPS。
- [ ] 集成 FFmpeg/libavcodec 解码。
- [ ] 将解码帧转换为 BGR/RGB。
- [ ] 按目标 FPS 抽帧。
- [ ] 处理解码错误、分辨率变化和关键帧恢复。

建议定位：

```text
JPEG 帧协议：求职演示和 MVP 主线
H.264 + FFmpeg：后续多媒体增强项
```

---

# 近期执行顺序

每次只做一项：

1. [ ] 完成稳定化学习检查。
2. [ ] 编写 `doc/Protocol V1.md`。
3. [ ] 实现 RPC 与 Connection 的 weak_ptr 关联。
4. [ ] 实现响应 JSON 与 TCP 回传。
5. [ ] 重写 ET 写排空。
6. [ ] 运行单图端到端测试。
7. [ ] 增加背压和 deadline。
8. [ ] 由 AI 接入 Web Demo。

## 第一个可验收里程碑

```text
给定一张本地 JPEG：
客户端通过 TCP 发送 →
C++ 正确拆包 →
Python YOLO 推理 →
C++ 收到 FrameResponse →
同一客户端收到带长度前缀的合法 JSON。
```

只有达到这个结果后，才进入视频前端阶段。
