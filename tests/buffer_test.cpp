#include <gtest/gtest.h>
#include "Buffer.h" // 确保 CMakeLists 中的 include_directories 能找到它
#include <string>

// ==========================================
// Test Suite: BufferCoreTests
// 目标：测试网络缓冲区的核心内存逻辑
// ==========================================

// 测试 1：初始化状态（边界测试）
// 语法：TEST(TestSuiteName, TestName)自定义测试套件的名字和具体的测试场景，不要使用_
TEST(BufferCoreTests, InitialState) {
    Buffer buf(1024); // 假设你的类构造函数支持传入初始大小，或者使用默认值
    
    // 常用比较的简写
    // EQ: equal
    // GE：great or equal
    // LE
    // STREQ： 专门用来判断c风格字符串是否相等
    // 刚初始化的 Buffer，可读数据应该为 0
    EXPECT_EQ(buf.readableBytes(), 0);// 断言宏EXPECT_EQ(a, b)：期望a EQ b 
    // 可写空间应该不小于初始设定值
    EXPECT_GE(buf.writableBytes(), 1024); 
}

// 测试 2：基本的读写偏移逻辑（功能测试）
// 目的：验证追加数据后，读写指针 (readIndex / writeIndex) 是否同步正确后移
TEST(BufferCoreTests, AppendAndRetrieve) {
    Buffer buf;
    std::string data = "Hello, VisionReactor!";
    buf.append(data.c_str(), data.size());

    // 写入后，可读字节数应完全等于写入长度
    EXPECT_EQ(buf.readableBytes(), data.size());
    
    // 模拟应用层读取了前 5 个字节 (Hello)
    // 注意：请将 peek() 和 retrieve() 替换为你 Buffer 类中实际的函数名
    std::string peek_data(buf.peek(), 5);
    EXPECT_EQ(peek_data, "Hello");
    
    // 移动读指针
    buf.retrieve(5);
    
    // 验证剩余可读字节数是否精准减少
    EXPECT_EQ(buf.readableBytes(), data.size() - 5);
}

// 测试 3：动态扩容与大包容载（压力/核心业务测试）
// 目的：验证在 AI 视觉场景下，突发 200KB 图像大包时，Buffer 是否能正确扩容且数据不乱码
TEST(BufferCoreTests, DynamicExpansion) {
    Buffer buf(1024); // 初始只有 1KB
    
    // 构造一个 200KB 的“巨型伪造帧”，模拟 YOLO 需要接收的高清图片流
    std::string huge_frame(200 * 1024, 'A'); 
    
    // 强行塞入远超当前容量的数据，触发内部的扩容逻辑 (通常是 std::vector 的 resize)
    buf.append(huge_frame.c_str(), huge_frame.size());

    // 1. 验证数据一字节不少
    EXPECT_EQ(buf.readableBytes(), 200 * 1024);
    
    // 2. 验证首尾数据没有因为扩容内存搬运而发生错乱 (内存踩踏)
    EXPECT_EQ(buf.peek()[0], 'A');
    EXPECT_EQ(buf.peek()[200 * 1024 - 1], 'A');
}

// 测试 4：内存碎片整理/复用回收（进阶边界测试）
// 目的：模拟长期运行下，读写指针不断后移导致前端出现空闲碎片，测试内部的挪动合并逻辑
TEST(BufferCoreTests, BufferCompaction) {
    Buffer buf(1024);
    std::string data = "1234567890";
    
    // 写入 10 字节，读走 5 字节，此时头部应该留出了 5 字节的碎片空间
    buf.append(data.c_str(), data.size());
    buf.retrieve(5);
    EXPECT_EQ(buf.readableBytes(), 5); // 剩下 "67890"

    // 此时往里面塞入一个恰好能触发碎片整理、但不触发扩容的大数据
    std::string chunk(1000, 'B');
    buf.append(chunk.c_str(), chunk.size());

    // 验证数据长度与内容是否依然严丝合缝
    EXPECT_EQ(buf.readableBytes(), 1005);
    EXPECT_EQ(std::string(buf.peek(), 5), "67890");
}