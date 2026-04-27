cmake_minimum_required(VERSION 3.10)
project(EpollServer)

# 1. 设置 C++ 标准 (C++17 是为了 std::shared_ptr, lambda 等特性)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall") # 开启调试信息和警告

# 2.告诉编译器去哪里找 .h 头文件
include_directories(include)
# 自动下载并集成GTest
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/heads/main.zip
)
FetchContent_MakeAvailable(googletest)

# 3.自动查找 src 目录下的所有 .cpp 文件
file(GLOB SRC_FILES "src/*.cpp")

# 4. 查找系统依赖包 (Threads, Protobuf, gRPC,Linux 下必须链接 pthread)
find_package(Threads REQUIRED)
# 查找系统中安装的 gRPC 和 Protobuf
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
# 查找OpenCV
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

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
    ${OpenCV_LIBS}            # OpenCV 库
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
# 测试程序同样链接 server_core，并额外链接 GTest 的 main 函数入口
target_link_libraries(buffer_test server_core GTest::gtest_main)
target_link_libraries(ThreadPool_test server_core GTest::gtest_main)
target_link_libraries(Buffer_bench server_core GTest::gtest_main)
target_link_libraries(ThreadPool_bench server_core GTest::gtest_main)
target_link_libraries(connection_test server_core GTest::gtest_main)
