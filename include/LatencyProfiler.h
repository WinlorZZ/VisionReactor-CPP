#pragma once

#include <chrono>
#include <cstdint>
#include <atomic>
#include <memory>

// ==========================================
// 1. 高精度计时器 (Header-only)
// ==========================================
class LatencyProfiler {
public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;

    static inline TimePoint now() {
        return Clock::now();
    }

    static inline int64_t microseconds_between(TimePoint start, TimePoint end) {
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    }
};

// ==========================================
// 2. 线程安全的全局 ID 发号器 (解决 C++11/14 ODR 问题)
// ==========================================
inline std::atomic<uint64_t>& get_global_frame_counter() {
    static std::atomic<uint64_t> counter{0}; // 局部静态变量，保证全局唯一且线程安全
    return counter;
}

// ==========================================
// 3. 帧生命周期上下文
// ==========================================
struct FrameContext {
    uint64_t trace_id;               
    
    LatencyProfiler::TimePoint t_start;       // TCP 刚到达 (T1 开始)
    LatencyProfiler::TimePoint t_parsed;      // 拆包完成 (T1 结束)
    LatencyProfiler::TimePoint t_grpc_sent;   // 已送入 gRPC (T2 结束)
    
    int64_t t3_python_cost_us = 0;            // Python 端纯推理耗时 (由 gRPC 返回)
    
    LatencyProfiler::TimePoint t_grpc_recv;   // C++ 收到 gRPC 回执 (T4 开始)
    LatencyProfiler::TimePoint t_finish;      // 准备发回客户端 (T4 结束)

    // 构造函数：自动分配 TraceID 并记录起点时间
    FrameContext() : trace_id(++get_global_frame_counter()) {
        t_start = LatencyProfiler::now();
    }
};

// 使用 shared_ptr 方便在多个队列和线程间传递
using FrameContextPtr = std::shared_ptr<FrameContext>;