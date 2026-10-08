cmake_minimum_required(VERSION 3.10)
project(EpollServer)

# 1. 设置 C++ 标准 (C++17 是为了 std::shared_ptr, lambda 等特性)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall") # 开启调试信息和警告

# 2.告诉编译器去哪里找 .h 头文件
include_directories(include)
# GTest：按"可控性"排序的三级来源，避免"没有 GitHub 网络就完全无法配置"
#
#   A) 配置时传了 -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=<本地源码目录> -> 用它（最明确，离线可用）
#   B) 系统装了 Linux 原生 libgtest-dev -> 用它（离线可用）
#   C) 都没有 -> 从 GitHub 下载（需要网络）
#
# ⚠️ WSL 特有陷阱：WSL 会把 Windows 的 PATH 带进来，于是 CMake 可能找到
#    /mnt/<盘>/... 下面 Windows 版（MSVC 编译）的 GTest，例如 Miniconda 自带的
#    /mnt/d/miniconda3/Library/lib/cmake/GTest。把那里的 gtest.lib 当 Linux 静态库链
#    会在链接期爆出一堆 MSVC 名字修饰符号（??_R4...）的未定义引用。
#    因此这里显式拒绝挂载盘下的 GTest。
set(_GTEST_OK FALSE)

if(FETCHCONTENT_SOURCE_DIR_GOOGLETEST)
  message(STATUS "GTest: 使用指定的本地源码 ${FETCHCONTENT_SOURCE_DIR_GOOGLETEST}（离线）")
else()
  find_package(GTest QUIET)
  if(GTest_FOUND AND TARGET GTest::gtest_main)
    if(GTest_DIR MATCHES "^/mnt/[A-Za-z]/")
      message(STATUS "GTest: 忽略 ${GTest_DIR}")
      message(STATUS "       该路径在 Windows 挂载盘下，多半是 MSVC 版 GTest，链进来会失败。")
      message(STATUS "       WSL 下请改用：sudo apt install libgtest-dev")
    else()
      set(_GTEST_OK TRUE)
      message(STATUS "GTest: 使用系统安装的 googletest ${GTest_DIR}（离线）")
    endif()
  endif()
endif()

if(NOT _GTEST_OK)
  if(NOT FETCHCONTENT_SOURCE_DIR_GOOGLETEST)
    message(STATUS "GTest: 改用 FetchContent（需要网络）")
    message(STATUS "       离线环境请传 -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=<本地 googletest 源码>")
  endif()
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/heads/main.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(googletest)
endif()

# 3.自动查找 src 目录下的所有 .cpp 文件
file(GLOB SRC_FILES "src/*.cpp")

# 4. 查找系统依赖包 (Threads, Protobuf, gRPC,Linux 下必须链接 pthread)
find_package(Threads REQUIRED)
# 查找系统中安装的 gRPC 和 Protobuf
# Protobuf 用默认查找方式：源码编译/vcpkg 提供 config 模式，apt 的 libprotobuf-dev
# 只提供 CMake 模块模式，写死 CONFIG 会在 Ubuntu 24.04 上直接配置失败。
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)
# 设置 protoc 编译器和 grpc 插件的路径
set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)
set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)

# 实时生成 gRPC 代码
set(HW_PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/proto/game_ai.proto")
set(HW_PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/game_ai.pb.cc")
set(HW_PROTO_HDRS "${CMAKE_CURRENT_BINARY_DIR}/game_ai.pb.h")
set(HW_GRPC_SRCS "${CMAKE_CURRENT_BINARY_DIR}/game_ai.grpc.pb.cc")
set(HW_GRPC_HDRS "${CMAKE_CURRENT_BINARY_DIR}/game_ai.grpc.pb.h")

include_directories("${CMAKE_CURRENT_BINARY_DIR}")
add_custom_command(
    OUTPUT "${HW_PROTO_SRCS}" "${HW_PROTO_HDRS}" "${HW_GRPC_SRCS}" "${HW_GRPC_HDRS}"
    COMMAND ${_PROTOBUF_PROTOC}
    ARGS --grpc_out="${CMAKE_CURRENT_BINARY_DIR}"
         --cpp_out="${CMAKE_CURRENT_BINARY_DIR}"
         --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
         -I "${CMAKE_CURRENT_SOURCE_DIR}/proto"
         "${HW_PROTO_FILE}"
    DEPENDS "${HW_PROTO_FILE}"
)

# 5. 核心逻辑静态库
# 把除了 main.cpp 之外的所有底层逻辑，打包成一个静态库 server_core
add_library(server_core ${SRC_FILES} ${HW_PROTO_SRCS} ${HW_GRPC_SRCS})

# 把第三方依赖直接挂载到 server_core 上（使用 PUBLIC 属性，谁链接了它，谁就拥有这些依赖）
target_link_libraries(server_core
    PUBLIC
    Threads::Threads          # ThreadPool 需要的 pthread
    gRPC::grpc++              # gRPC 核心库
    protobuf::libprotobuf     # Protobuf 核心库
)

# 6. 生成主程序 (线上运行入口)
add_executable(server main.cpp)
# 主程序只需要链接 server_core 即可
target_link_libraries(server server_core)

# 7. 生成自动化测试程序 (测试入口)
add_executable(buffer_test tests/buffer_test.cpp)
add_executable(ThreadPool_test tests/thread_test.cpp)
add_executable(Buffer_bench tests/Buffer_bench.cpp)
add_executable(ThreadPool_bench tests/ThreadPool_bench.cpp)
add_executable(connection_test tests/connection_test.cpp)
add_executable(event_loop_test tests/event_loop_test.cpp)
add_executable(response_serializer_test tests/response_serializer_test.cpp)
# 测试程序同样链接 server_core，并额外链接 GTest 的 main 函数入口
target_link_libraries(buffer_test server_core GTest::gtest_main)
target_link_libraries(ThreadPool_test server_core GTest::gtest_main)
target_link_libraries(Buffer_bench server_core GTest::gtest_main)
target_link_libraries(ThreadPool_bench server_core GTest::gtest_main)
target_link_libraries(connection_test server_core GTest::gtest_main)
target_link_libraries(event_loop_test server_core GTest::gtest_main)
target_link_libraries(response_serializer_test server_core GTest::gtest_main)

enable_testing()
add_test(NAME buffer_test COMMAND buffer_test)
add_test(NAME thread_pool_test COMMAND ThreadPool_test)
add_test(NAME connection_test COMMAND connection_test)
add_test(NAME event_loop_test COMMAND event_loop_test)
add_test(NAME response_serializer_test COMMAND response_serializer_test)
