cmake_minimum_required(VERSION 3.10)
project(EpollServer)

# 1. 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 开启调试信息 (-g) 和 所有警告 (-Wall)
# 在网络编程中，调试符号和警告非常重要
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall")

# 收集源文件
# 语法: file(GLOB 变量名 "路径/匹配模式")
file(GLOB SRC_FILES "src/*.cpp")

# 生成可执行文件
# 语法: add_executable(可执行文件名 源文件列表)
add_executable(epoll_server ${SRC_FILES})

