#include "Connection.h"
#include "Socket.h"
#include "Channel.h"
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <memory> // shared_from_this
#include "Buffer.h"
#include "AsyncAIEngine.h"
#include <opencv2/opencv.hpp>// cv::Mat

#include <thread> // 对应 std::this_thread
#include <chrono> // 对应 std::chrono
#include <atomic> // std::atomic

Connection::Connection(EventLoop *loop, Socket *sock) : state_(kConnected), loop(loop), sock(sock) {
    //初始化channel
    channel = new Channel(loop, sock->fd());
    //初始化Buffer
    inputBuffer = new Buffer();
    outputBuffer = new Buffer();

    //绑定读事件
    std::function<void()> readCb = std::bind(&Connection::handleReadEvent, this);
    channel->setReadCallback(readCb);
    //绑定写事件
    std::function<void()> writeCb = std::bind( &Connection::handleWriteEvent,this );
    channel->setWriteCallback(writeCb);
    // channel->enableReading(); //将注册epoll这一步从构造函数中剔除，以便使用shared_ptr
    // 执行构造函数的时候，外层的 std::make_shared 还没有执行完，
    // 当前对象还处于‘半成品’状态，根本没有被 shared_ptr 管理
}

void Connection::connectEstablished(){
    // 此时shared_ptr可以使用了
    channel->tie( shared_from_this() );
    channel->enableReading();
}

Connection::~Connection() {
    delete channel;
    delete sock;
    delete inputBuffer;
    delete outputBuffer;
}

void Connection::setDeleteConnectionCallback(std::function<void(Socket*)> cb) { 
    deleteConnectionCallback = cb; 
}

void Connection::setOnMessageCallback(std::function<void( std::shared_ptr<Connection> )> cb) { 
    onMessageCallback = cb; 
}

// IO 读取 
// 将该函数设置给Connection管理的对应的channel，channel在被调用handleEvent时会使用该函数
void Connection::handleReadEvent() {
    int savedErrno = 0;
    // bool read_something = false;

    // ET 模式：必须用 while 循环读到 EAGAIN 为止
    while (true) {
        ssize_t n = inputBuffer->readFd(sock->fd(), &savedErrno);
        
        if (n > 0) {// 读到数据
            // read_something = true; 
            continue;// 继续下一次调用readFd，调用的时候已经存进inputBuffer了
        } else if (n == -1 && savedErrno == EINTR) {// 被系统中断打断，继续读
            continue; 
        } else if (n == -1 && (savedErrno == EAGAIN || savedErrno == EWOULDBLOCK)) {
            break; // 
        } else if (n == 0) {
            // 对端正常关闭 (FIN包)
            std::cout << "[Connection] 收到 FIN，准备断开..." << std::endl;
            handleClose();
            break; // 已经要断开了，直接跳出循环
        } else {
            // 发生了其他严重错误
            std::cout << "[Connection] 读取异常，强行断开..." << std::endl;
            handleClose();
            break;
        }
    }
    // 将数据处理放到循环外进行（之前在n > 0就处理了）
    if (inputBuffer->readableBytes() > 0 && onMessageCallback) {
        onMessageCallback(shared_from_this());
    }
}

void Connection::handleClose() {
    state_ = kDisconnecting; // 状态切换：准备断开
    channel->disableReading(); // 不再接收新数据

    // 检查是否有残留的未读数据
    if (outputBuffer->readableBytes() == 0) {
        // 没有则直接通知server回收
        if (deleteConnectionCallback) deleteConnectionCallback(sock);
    } else {
        std::cout << "[Connection] 触发优雅挥手，发现 Buffer 仍有积压，延迟销毁！" << std::endl;
        // 留着 EPOLLOUT，让 handleWriteEvent 把剩下的数据发完
    }
}

// ==================== H.264 Annex B 排水循环（粘包/半包/熔断） ====================
//
// 设计要点:
//   - 零拷贝: 全程操作裸指针，不产生任何 std::string 拷贝
//   - 粘包处理: while(true) 循环连续切分，直到缓冲区耗尽或不完整帧
//   - 半包等待: 找到起始码但找不到下一个起始码时，break 等待下次 Epoll 触发
//   - TCP 截断起始码: findH264StartCode 内建边界检查，末尾 00/00 00 自然返回
//     nullptr; 下次 readv 补齐后重新搜索即可拼接完整起始码
//   - OOM 熔断: 超过 10MB 且无有效起始码 → 清空 Buffer → 关闭连接

static void ProcessNalu(const char* data, size_t len) {
    // 占位函数: 后续替换为实际的 NALU 处理逻辑（编码/推流/写入文件等）
    if (len > 0) {
        int naluType = getNaluType(static_cast<uint8_t>(data[0])); // 复用 h264_demuxer.h
        std::cout << "[H264] 切出 NALU type=" << naluType
                  << " size=" << len << "B" << std::endl;
    }
}

bool Connection::processH264Nalus() {
    // ===================== 阶段 0: OOM 防火墙 =====================
    // 触发条件: 缓冲区超过 10MB 却连一个合法的 H.264 起始码都找不到
    // 此时大概率是恶意垃圾数据或协议错乱，继续积累只会撑爆内存
    if (inputBuffer->readableBytes() > Buffer::kH264MaxBufferBytes &&
        inputBuffer->findH264StartCode() == nullptr) {
        std::cerr << "[OOM Firewall] 缓冲区已达 "
                  << (inputBuffer->readableBytes() / (1024.0 * 1024.0))
                  << "MB 且未发现 H.264 起始码，触发熔断！清空并断开连接。"
                  << std::endl;
        inputBuffer->retrieveAll();
        handleClose();
        return false; // 通知调用方：连接已进入销毁流程
    }

    // ===================== 阶段 1: 排水循环 =====================
    while (true) {
        // ----- 1a. 定位当前 NALU 的起始码 -----
        //
        // 从 readerIndex_ 开始搜索第一个 00 00 01 / 00 00 00 01。
        // 返回值是指向底层 vector<char> 的裸指针，不产生内存拷贝。
        const char* startCode = inputBuffer->findH264StartCode();

        if (startCode == nullptr) {
            // 场景 A: 缓冲区里连一个起始码都没有
            //   - 正常情况: 数据还没到齐，等待下次 Epoll 触发
            //   - 异常情况: 垃圾数据在积累，由阶段 0 的 OOM 防火墙兜底
            break;
        }

        // ----- 1b. 跳过起始码前的垃圾数据 -----
        //
        // 场景: 流开头可能包含非 H.264 数据（如 HTTP 头残留、错位字节等），
        // 如果起始码不在 readerIndex_ 位置，说明前面有垃圾，全部丢弃。
        if (startCode > inputBuffer->peek()) {
            size_t garbageLen = startCode - inputBuffer->peek();
            std::cout << "[H264] 跳过 " << garbageLen
                      << "B 垃圾数据（起始码前非 H.264 内容）" << std::endl;
            inputBuffer->retrieve(garbageLen);
        }
        // 此时 inputBuffer->peek() 正好指向起始码首字节

        // ----- 1c. 判定起始码长度（3 字节还是 4 字节） -----
        const char* p = inputBuffer->peek();
        size_t readable = inputBuffer->readableBytes();
        int scLen = 3; // 默认 3 字节: 00 00 01
        if (readable >= 4 && p[2] == 0x00 && p[3] == 0x01) {
            scLen = 4; // 4 字节: 00 00 00 01
        }

        // ----- 1d. 搜索下一个起始码（标记当前 NALU 的结束位置） -----
        //
        // 从当前起始码之后开始搜索。下一个起始码的位置就是当前 NALU 的
        // 数据边界。H.264 Annex B 标准中，NALU 之间以起始码分隔:
        //
        //   ... [00 00 00 01] [NALU_DATA] [00 00 00 01] [NEXT_NALU] ...
        //                    |----------- 当前 NALU -----------|
        //
        const char* nextStartCode = inputBuffer->findH264StartCode(p + scLen);

        // ----- 1e. 半包判断: 找不到下一个起始码 -----
        if (nextStartCode == nullptr) {
            // 场景 B: 当前 NALU 的尾部数据尚未到达
            //
            // 此时绝不能移动 readerIndex_！一旦移动，下次搜索会从错误位置
            // 开始，导致数据错乱。直接 break，等待下次 readv 补齐数据后，
            // 下一轮 onMessage 会重新进入本循环，从同一位置继续搜索。
            //
            // TCP 截断起始码的边界情况:
            //   包尾残留 00 00，下个包首字节是 01。
            //   findH264StartCode 在 p+2 >= end 时自然跳过，
            //   下次循环时数据已补齐，00 00 01 被完整匹配。
            //
            // 单 NALU 超大保护: 若当前 NALU 数据已超过 5MB 仍无结束标记，
            // 视为异常（正常 H.264 NALU 远小于此值），触发保护
            size_t currentNaluDataSize = inputBuffer->beginWrite() - (p + scLen);
            if (currentNaluDataSize > Buffer::kH264MaxNaluBytes) {
                std::cerr << "[OOM Firewall] 单 NALU 数据已超 "
                          << (currentNaluDataSize / (1024.0 * 1024.0))
                          << "MB 且无结束起始码，异常！清空并断开。" << std::endl;
                inputBuffer->retrieveAll();
                handleClose();
                return false;
            }

            break; // 半包等待: 不回退、不消费，原地等数据
        }

        // ----- 1f. 提取并处理完整 NALU -----
        //
        // NALU 数据区域 = [当前起始码末尾, 下一个起始码开头)
        const char* naluData = p + scLen;
        size_t naluSize = nextStartCode - naluData;

        if (naluSize > 0) {
            // 调用 NALU 处理函数（当前为占位，后续替换为实际业务逻辑）
            ProcessNalu(naluData, naluSize);
        }
        // 注意: 长度为 0 的 NALU 理论上不会出现（起始码不会连续紧挨），
        // 但即使出现也安全跳过

        // ----- 1g. 移动读游标，前进到下一个起始码 -----
        //
        // retrieve(len) 的效果: readerIndex_ += len
        // 下一轮循环的 peek() 将正好指向 nextStartCode 位置（即下一帧的
        // 起始码），实现连续切包。
        inputBuffer->retrieve(nextStartCode - p);
        // 循环回到 1a，继续解析下一个 NALU
    }

    return true; // 本轮排水完成（可能因半包等待或缓冲区耗尽而退出）
}

// 原有的 business 方法保留不动，供旧协议路径（4字节长度前缀）使用
void Connection::business(AsyncAIEngine* engine_ptr) {
    // std::cerr << "[Critical Debug] 进入 business 函数成功！" << std::endl;
    if ( inputBuffer->readableBytes() == 0 ) return;// 当读缓冲区为空时返回
    // // 之前的版本，提取消息
    // std::string message(inputBuffer->peek(), inputBuffer->readableBytes());
    // inputBuffer->retrieveAll(); // 取出后立刻移动读游标
    if (!engine_ptr) {
                std::cerr << "[-] 致命错误：engine_ptr 是空指针！" << std::endl;
                return;
    }
    
    while (inputBuffer->readableBytes() >= 4) {
        // 包头解析
        uint32_t body_len = inputBuffer->peekInt32();
        std::cout << "[Debug] 收到 Header，解析出的 Body 长度为: " << body_len << std::endl;
        if (body_len <= 0 || body_len > 10 * 1024 * 1024) {
            // std::cerr << "[-] 致命错误：非法的数据包长度 " << body_len << "，强制断开连接！\n";
            handleClose();
            break;
        }
        if (!current_frame_ctx_) {
            current_frame_ctx_ = std::make_shared<FrameContext>();
            std::cout << "[Trace] 新帧开始接收 -> TraceID: " << current_frame_ctx_->trace_id << std::endl;
        }

        if (inputBuffer->readableBytes() >= 4 + body_len) {
            inputBuffer->retrieve(4);// 丢弃包头
            std::string message = inputBuffer->retrieveAsString(body_len);
            // 计时器：T1结束
            current_frame_ctx_->t_parsed = LatencyProfiler::now();
            std::cout << "[Debug] 数据已齐，准备调用 AI 引擎..." << std::endl;
            
            /* 处理业务 */ 
            uint64_t current_frame_id = current_frame_ctx_->trace_id;
            std::cout << "[协议层] 成功切包！提取到完整图像载荷，大小: " 
                        << message.size() << " bytes -> FrameID: " << current_frame_id << "\n";
            // 发送图片数据
            engine_ptr->AnalyzeFrameAsync(current_frame_ctx_, std::move(message));
            // 发送结束后重置上下文
            current_frame_ctx_.reset();
        }else{// 有包头但数据未传完，退出循环并等待下一次 Epoll 触发可读事件
            std::cout << "[Debug] 数据未齐，当前缓冲区: " << inputBuffer->readableBytes() 
                      << " 字节，等待下一波..." << std::endl;
            break;
        }
    }
}

// 发送接口，提供给business调用
void Connection::send(const std::string& msg){
    //先发送已有的数据
    if(outputBuffer->readableBytes() > 0){
        outputBuffer->append(msg.c_str(),msg.size() );
        return;
    }
    //write(sock->fd(), readBuffer.c_str(), readBuffer.size());
    //
    ssize_t nwrote = 0;// 记录
    size_t remaining = msg.size();
    bool faultError = false;
    nwrote = write(sock->fd() , msg.c_str() , msg.size() );
    if(nwrote >= 0){
        remaining = msg.size() - nwrote;
        if(remaining == 0) return;//没有剩余，直接返回
    }else{
        //nwrote == 0;
        if(errno != EWOULDBLOCK && errno != EAGAIN){
            faultError = true;//发生意外错误，排除读取完全部数据的错误码
        }
    }
    // 如果没写完，追加到 outBuffer 并注册 EPOLLOUT
    if (!faultError && remaining > 0) {
        std::cout << "[Send] 内核缓冲区已满，剩余 " << remaining << " 字节转入 OutputBuffer" << std::endl;
        outputBuffer->append(msg.c_str() + nwrote, remaining);
        
        // 通过channel类对象，将写事件添加到epoll
        if (!channel->isWriting()) {
            channel->enableWriting(); 
        }
    }
}

// 写处理
void Connection::handleWriteEvent(){
    if (channel->isWriting()) {
        std::cout << "[HandleWrite] Epoll 触发可写，准备搬运 Buffer 数据..." << std::endl;
        // 取出 outputBuffer_ 中的积压数据继续写
        ssize_t n = write(sock->fd(), outputBuffer->peek(), outputBuffer->readableBytes());
        
        if (n > 0) {
            // 发送成功 n 字节，向后移动读游标readerIndex_
            outputBuffer->retrieve(n);
            std::cout << "[HandleWrite] 成功发送 " << n << " 字节，剩余积压 " << outputBuffer->readableBytes() << std::endl;
            // 如果发完了，立刻注销 EPOLLOUT，防止死循环
            if (outputBuffer->readableBytes() == 0) {
                std::cout << "[HandleWrite] 数据发送完毕，注销 EPOLLOUT" << std::endl;
                channel->disableWriting(); 
                //如果此时连接准备结束但尚未结束，调用回调函数通知释放connection
                if(state_ == kDisconnecting ){
                    std::cout << "[Connection] 残留数据发送完毕，释放Connection" << std::endl;
                    if (deleteConnectionCallback) deleteConnectionCallback(sock);
                }
            }
        }
    }
}