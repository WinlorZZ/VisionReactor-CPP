#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include "EventLoop.h"

TEST(EventLoopTest, RunsCrossThreadFunctorAndWakesPoller) {
    EventLoop loop;
    std::atomic<bool> executed{false};

    std::thread producer([&loop, &executed]() {
        loop.queueInLoop([&loop, &executed]() {
            executed.store(true);
            loop.quit();
        });
    });

    loop.loop();
    producer.join();

    EXPECT_TRUE(executed.load());
}
