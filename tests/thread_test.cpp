#include <gtest/gtest.h>
#include "ThreadPool.h" // 确保 CMakeLists 中的 include_directories 能找到它
#include <string>

//
TEST(ThreadPoolTest, InitialState){
    ThreadPool ths(6);
    EXPECT_EQ(ths.getWorkersCount(), 6);
    EXPECT_EQ(ths.getPendingTaskCount() , 0);
}

TEST(ThreadPoolTest, CES){
    ThreadPool pool(6);
    std::atomic<int> counter{0};
    for(int i = 0; i< 1000; ++i){
        pool.add(
            [&counter](){
                counter++;
            }
        );
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(counter.load(), 1000);
}