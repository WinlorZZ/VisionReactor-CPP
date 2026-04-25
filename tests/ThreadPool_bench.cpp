#include <gtest/gtest.h>
#include <chrono>
#include <atomic>
#include <vector>
#include "ThreadPool.h"

TEST(ThreadPoolBenchmark, TaskSchedulingOverhead) {
    const int NUM_TASKS = 100000; // 瞬间抛入 10 万个微小任务
    ThreadPool pool(8);           // 开启 8 个工作线程
    std::atomic<int> counter{0};

    auto start = std::chrono::steady_clock::now();

    // 测重点：单线程疯狂塞入任务，8个线程疯狂抢夺任务
    for (int i = 0; i < NUM_TASKS; ++i) {
        pool.add([&counter]() {
            // 极轻量级任务，单纯为了测试锁调度开销
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    // 轮询等待所有任务执行完毕
    while (counter.load(std::memory_order_relaxed) < NUM_TASKS) {
        std::this_thread::yield(); 
    }

    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    double tps = (static_cast<double>(NUM_TASKS) / ms) * 1000.0;

    std::cout << "[ VISION REACTOR ] ThreadPool TPS (Tasks Per Second): " << tps << " tasks/s\n";
    std::cout << "[ VISION REACTOR ] Average Scheduling Latency: " << (ms * 1000.0 / NUM_TASKS) << " us/task\n";
}