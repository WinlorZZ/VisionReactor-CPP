#pragma once
#include <functional>
#include <string>
#include <memory>
#include <arpa/inet.h>
#include <cstddef>
#include "LatencyProfiler.h"
// #include <gtest/gtest_prod.h>

class EventLoop;
class Socket;
class Channel;
class Buffer;
class AsyncAIEngine;

class Connection : public std::enable_shared_from_this<Connection>{
                // 公有继承自 基类 std::enable_shared_from_this<Connection>
                // 为 Connection 提供了 shared_from_this() 成员函数
public:
    // 三个状态：连接保持，准备断开连接，连接已经断开
    enum StateE { kConnected, kDisconnecting, kDisconnected };
    
    // 初始化函数
    void connectEstablished(); 
    // 销毁函数
    void connectDestroyed();

    Connection(EventLoop *loop, Socket *sock);
    ~Connection();

    // using Callback = std::function<void(Connection*)>; // 回调类型：传自己回去

    void setDeleteConnectionCallback(std::function<void(Socket*)> cb);
    
    // 设置消息回调
    void setOnMessageCallback(std::function< void( std::shared_ptr<Connection> ) >cb);

    // 只负责读数据 (IO)
    void handleReadEvent();
    
    // 负责处理数据 (计算/业务)
    void business( AsyncAIEngine* engine_ptr );

    // H.264 Annex B 排水循环：从 inputBuffer 中持续拆解 NALU
    // 返回 false 表示触发了 OOM 熔断，调用方应释放连接
    bool processH264Nalus();

    // 发送接口，业务处理完后调用此接口发送数据
    void send(const std::string& msg);

    // AI 回调完成后调用：归还连接在途额度，并按当前协议写回结果
    void completeAnalysis(const std::string& json);

    // 写回调，由EventLoop调用
    void handleWriteEvent();

    // 释放连接
    void handleClose();

private:
    enum class ClientProtocol {
        TcpLengthPrefixed,
        WebSocket
    };

    void sendInLoop(const std::string& msg);
    void sendApplicationMessageInLoop(const std::string& json);
    void submitImageInLoop(AsyncAIEngine* engine_ptr,
                           std::string&& image_data,
                           FrameContextPtr ctx = nullptr);
    bool tryHandleWebSocketHandshake();
    bool processWebSocketFrames(AsyncAIEngine* engine_ptr);
    void sendWebSocketFrameInLoop(uint8_t opcode, const std::string& payload);
    void sendProtocolErrorInLoop(uint64_t frame_id,
                                 const std::string& code,
                                 const std::string& message);

    // 授权特定的测试套件访问私有成员
    // FRIEND_TEST(ConnectionTest, StickyPacketTest);
    // FRIEND_TEST(ConnectionTest, FatPacketHandling);
    // FRIEND_TEST(ConnectionTest, LifecycleSafety);

    StateE state_; // 当前连接状态

    EventLoop *loop;
    Socket *sock;
    Channel *channel;

    std::function<void(Socket*)> deleteConnectionCallback;
    
    std::function<void( std::shared_ptr< Connection >)> onMessageCallback; 
    
    // 储存读到的数据
    // std::string readBuffer;
    Buffer* inputBuffer;
    Buffer* outputBuffer;
    // 时间信息上下文
    FrameContextPtr current_frame_ctx_;

    ClientProtocol protocol_ = ClientProtocol::TcpLengthPrefixed;
    bool websocket_handshake_done_ = false;
    size_t in_flight_requests_ = 0;
    static constexpr size_t kMaxInFlightRequests = 2;
    static constexpr size_t kMaxFrameBytes = 10 * 1024 * 1024;
};
