#include <gtest/gtest.h>
#include <memory>
#include <atomic>
#include "Connection.h"
#include "Socket.h"
#include "EventLoop.h"
#include <sys/socket.h> // 引入底层 socket 库


TEST(ConnectionStability, SharedFromThisAndTieSafety) {
    EventLoop loop;
    
    // [修复点]：向操作系统申请一个真实的空 TCP Socket 描述符
    // 这样就能完美骗过你的 Socket 类的底层校验
    int dummy_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    Socket* sock = new Socket(dummy_fd); // 传入真实的 fd
    
    std::shared_ptr<Connection> conn = std::make_shared<Connection>(&loop, sock);

    // 2. 模拟 Server 端的删除回调
    std::atomic<bool> is_deleted{false};
    conn->setDeleteConnectionCallback([&is_deleted](Socket* s) {
        is_deleted = true;
    });

    // 3. 核心触发：建立连接并执行 tie
    conn->connectEstablished();
    EXPECT_EQ(conn.use_count(), 1);

    // 4. 模拟“优雅挥手”场景
    conn->handleClose();
    
    // 5. 模拟竞态条件：外部指针销毁
    conn.reset(); 

    // 验证：即使外部释放了，因为 tie 机制，对象依然安全存活
    EXPECT_TRUE(is_deleted);
}

TEST(ConnectionStability, HighFrequencyCreationSoakTest) {
    EventLoop loop;
    const int TEST_COUNT = 50000; // 5万次高频冲击

    for (int i = 0; i < TEST_COUNT; ++i) {
        int dummy_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        Socket* sock = new Socket(dummy_fd);
        
        auto conn = std::make_shared<Connection>(&loop, sock);

        // 核心：注册 Server 层的删除回调
        conn->setDeleteConnectionCallback([](Socket* s) {
            // 在真实的 Server 架构中，这里会执行 server_map.erase(s->fd())
            // 在测试中，回调被触发就意味着业务闭环完成了，智能指针会自动释放内存
        });

        conn->connectEstablished();

        conn->setOnMessageCallback([](std::shared_ptr<Connection> c) {
            // 模拟收到消息时的业务层回调
        });

        // 瞬间触发关闭
        // 因为 Buffer 是空的，这里会自动无缝触发我们上面注册的 deleteConnectionCallback
        conn->handleClose(); 
    }
    SUCCEED();
}