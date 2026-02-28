cmake_minimum_required(VERSION 3.10)
project(EpollServer)

# 1. 设置 C++ 标准 (C++17 是为了 std::shared_ptr, lambda 等特性)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall") # 开启调试信息和警告

# 2.告诉编译器去哪里找 .h 头文件
# 这样你在代码里写 #include "Server.h" 时，编译器知道去 include/ 目录下找
include_directories(include)

# 3.自动查找 src 目录下的所有 .cpp 文件
file(GLOB SRC_FILES "src/*.cpp")

# 4. 查找系统线程库 (Linux 下必须链接 pthread)
find_package(Threads REQUIRED)

# 5. 生成可执行文件
# 注意：要把 main.cpp (在根目录) 和 SRC_FILES (在 src 目录) 一起编译
add_executable(epoll_server main.cpp ${SRC_FILES})

# 6. 链接线程库
# 如果不加这一行，ThreadPool 里的 std::thread 会报错 "undefined reference to pthread_create"
target_link_libraries(epoll_server Threads::Threads)