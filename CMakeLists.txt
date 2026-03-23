cmake_minimum_required(VERSION 3.10)
project(EpollServer)

# 1. 设置 C++ 标准 (C++17 是为了 std::shared_ptr, lambda 等特性)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall") # 开启调试信息和警告

# 2.告诉编译器去哪里找 .h 头文件
include_directories(include)

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
# 5. 生成可执行文件
# 注意：要把 main.cpp (在根目录) 和 SRC_FILES (在 src 目录) 一起编译
add_executable(server main.cpp ${SRC_FILES} ${HW_PROTO_SRCS} ${HW_GRPC_SRCS})

# 6. 链接所有底层库
# 如果不加这一行，ThreadPool 里的 std::thread 会报错 "undefined reference to pthread_create"
target_link_libraries(server
    Threads::Threads          # ThreadPool 需要的 pthread
    gRPC::grpc++              # gRPC 核心库
    protobuf::libprotobuf     # Protobuf 核心库
    ${OpenCV_LIBS}
)