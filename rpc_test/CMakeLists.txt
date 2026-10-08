cmake_minimum_required(VERSION 3.10)
project(RpcTest)

# 使用 C++17 (你的服务器应该也是 11/14/17 以上)
set(CMAKE_CXX_STANDARD 17) 

# 寻找系统安装的 Protobuf 库
find_package(Protobuf REQUIRED)

# 寻找 gRPC 库
find_package(gRPC REQUIRED)

# 告诉 CMake 生成的头文件在哪里
include_directories(${CMAKE_SOURCE_DIR}/../proto)

# 将 .proto 生成的两个 C++ 源文件一起拉进来编译
set(PROTO_SRC 
    ../proto/game_ai.pb.cc
    ../proto/game_ai.grpc.pb.cc
)

# 生成 dummy_client 可执行文件
add_executable(dummy_client_async dummy_client_async.cpp ${PROTO_SRC})

# 将 gRPC 和 Protobuf 的系统库链接到我们的程序上
target_link_libraries(dummy_client_async
    gRPC::grpc++
    protobuf::libprotobuf
)