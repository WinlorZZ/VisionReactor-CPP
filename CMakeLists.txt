cmake_minimum_required(VERSION 3.10)
project(EpollServer)

# 1. 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 包含头文件目录
include_directories(include)

# 收集源文件
# 语法: file(GLOB 变量名 "路径/匹配模式")
file(GLOB SRC_FILES "src/*.cpp" "main.cpp")

# 生成可执行文件
# 语法: add_executable(可执行文件名 源文件列表)
add_executable(epoll_server ${SRC_FILES})

