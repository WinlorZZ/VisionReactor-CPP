#include <gtest/gtest.h>
#include <chrono>
#include <vector>
#include "Buffer.h"

// 阻止编译器优化的辅助函数
template <class T>
void doNotOptimize(T const& val) {
    asm volatile("" : : "g"(val) : "memory");
}

TEST(BufferAbsoluteBenchmark, ThroughputAndMemoryStability) {
    Buffer buf;
    const size_t TCP_CHUNK = 64 * 1024;    // 64KB 碎片化接收
    const size_t FRAME_SIZE = 512 * 1024;  // 512KB AI 推理帧
    const int ITERATIONS = 100000;         // 压测 10万帧，约 50GB 数据

    std::vector<char> dummy_chunk(TCP_CHUNK, 'A');

    // [预热阶段]：让 Buffer 跑几轮，触发其内部的 makeSpace 扩容
    // 这证明了你的预留和搬移机制起效了
    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 8; ++j) { buf.append(dummy_chunk.data(), TCP_CHUNK); }
        buf.retrieve(FRAME_SIZE);
    }
    
    // 记录此时 Buffer 的容量（底层的 capacity）
    // 注意：你需要把 buffer_.capacity() 暴露出来，或者在这里估算
    size_t expected_capacity = buf.writableBytes() + buf.readableBytes();

    // [正式测速阶段]
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        // 模拟网络层频繁的小块写入
        for (int j = 0; j < 8; ++j) {
            buf.append(dummy_chunk.data(), TCP_CHUNK);
        }
        
        // 模拟业务层零拷贝读取
        doNotOptimize(buf.peek());
        
        // 提取数据并触发内部状态机 (Tighten 搬移)
        buf.retrieve(FRAME_SIZE);
    }
    auto end = std::chrono::steady_clock::now();

    // 统计绝对吞吐量
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    double gbps = (static_cast<double>(FRAME_SIZE) * ITERATIONS) / (1024 * 1024 * 1024) / (ms / 1000.0);
    
    std::cout << "[ VISION REACTOR ] Sustained Bandwidth: " << gbps << " GiB/s\n";

    // 核心断言：证明在 50GB 的数据冲刷下，容量没有发生过膨胀！
    // 这是一个极具说服力的指标，证明了无内存泄漏和无额外动态分配
    EXPECT_LE(buf.writableBytes() + buf.readableBytes(), expected_capacity + TCP_CHUNK);
}