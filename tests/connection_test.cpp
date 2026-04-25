#include <gtest/gtest.h>
#include "Connection.h"
#include "AsyncAIEngine.h"
#include "Buffer.h"
// class AsyncAIEngine;

class MockAIEngine : public AsyncAIEngine{
public:
    int received_frame_count = 0;
    size_t last_frame_size = 0;

    void AnalyzeFrameAsync(FrameContextPtr ctx, std::string&& message) override {
        received_frame_count++;// 调用次数
        last_frame_size = message.size();// 信息大小
        
        std::cout << "[Mock] 假装分析了一帧数据，大小：" << message.size() << std::endl;
    }
private:

};

TEST(ConnectionTest, StickyPacketTest) {
    auto engine = std::make_shared<MockAIEngine>(); // 使用 Mock 或 Dummy 引擎
    auto conn = std::make_shared<Connection>(nullptr, nullptr);
    auto buffer = conn->inputBuffer;

    // 构造一个“半包”：只有包头的前 2 字节
    int32_t len = 100;
    char header[4];
    memcpy(header, &len, 4);
    buffer->append(header, 2);
    
    conn->business(engine.get());
    EXPECT_EQ(buffer->readableBytes(), 2); // 应该留在缓冲区，不解析

    // 补全剩下的包并多带一个包的开头（粘包）
    buffer->append(header + 2, 2);
    buffer->append(std::string(100, 'a').c_str(), 100);
    buffer->append(header, 4); // 下一个包的包头

    conn->business(engine.get());
    EXPECT_EQ(buffer->readableBytes(), 4); // 应该只剩下一个孤立的包头
}

TEST(ConnectionTest, FatPacketHandling) {
    auto engine = std::make_shared<MockAIEngine>();
    auto conn = std::make_shared<Connection>(nullptr, nullptr);
    auto buffer = conn->inputBuffer;
    // 构造一个 5MB 的真实包
    int32_t fat_len = 5 * 1024 * 1024;
    std::string payload(fat_len, 'X');
    // 把主机字节序的长度，转换成网络字节序
    int32_t net_len = htonl(fat_len);
    // 把转换后的大端序整数拷进 header
    char header[4];
    memcpy(header, &net_len, 4);
    buffer->append(header, 4);
    buffer->append(payload.c_str(), fat_len);

    // 验证 business 函数在处理 5MB 负载时是否会发生 SIGSEGV 或内存拷贝异常
    EXPECT_NO_THROW(conn->business(engine.get()));
}

TEST(ConnectionTest, LifecycleSafety) {
    auto engine = std::make_shared<MockAIEngine>();
    std::weak_ptr<Connection> wk_conn;

    {
        auto conn = std::make_shared<Connection>(nullptr, nullptr);
        wk_conn = conn;

        // 模拟一个异步任务正在处理
        std::thread worker([conn, engine]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            // 即使外部 conn 析构了，这里也应该能安全持有引用
            EXPECT_GE(conn.use_count(), 1); 
            conn->send("Check alive");
        });
        worker.detach();
    } // 此时外部作用域的 conn 析构

    // 等待异步线程执行
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    // 验证连接是否已完全释放
    EXPECT_TRUE(wk_conn.expired());
}