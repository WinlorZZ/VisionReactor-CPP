#pragma once
#include <string>
#include <sys/uio.h> // readv 的核心头文件
#include <errno.h>   // 错误码
#include <arpa/inet.h> // ntohl , htonl 

class Buffer{
public:
    static const size_t kCheapPrepend = 8;
    static const size_t kInitialSize = 1024;//未扩容下整个buffer长度(不包含预留头)，字节数

    Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + initialSize),
          readerIndex_(kCheapPrepend),
          writerIndex_(kCheapPrepend) {}
    ~Buffer() = default;

    //核心区计算
    size_t readableBytes() const { return writerIndex_ - readerIndex_ ;}// 已写入空间
    size_t writableBytes() const { return buffer_.size() - writerIndex_ ;}// 尾部可写空间
    size_t prependableBytes() const { return readerIndex_; }// 死区末尾位置（读指针位置）

    //获取数据区指针，使用起始地址加相对位置计算
    const char* peek() const { return begin() + readerIndex_; }
    char* beginWrite()  { return begin() + writerIndex_; }// 可修改的版本
    const char* beginWrite() const { return begin() + writerIndex_; }// 只读的版本

    //功能区

    // 协议解析
    int32_t peekInt32() const{
        if (readableBytes() >= 4) {
            int32_t be32 = 0;
            ::memcpy(&be32, peek(), sizeof(be32));
            return ntohl(be32);
        }
        return 0;
    }

    std::string retrieveAsString(size_t len) {
    if (len > readableBytes()) len = readableBytes(); 
    std::string result(peek(), len); 
    retrieve(len); //（代码复用）
    return result; 
}
    //写入数据
    void append(const char* data, size_t len){
        ensureWritableBytes(len);// 确保写入时有足够空间
        std::copy(data, data + len,beginWrite() );//参数： 首元素地址，末尾元素地址+1，复制位置的起点地址
        writerIndex_ += len;
    }
    //写入数据后write指针移动
    void hasWritten(size_t len) { writerIndex_ += len; }
    //取出数据的指针移动
    void retrieve(size_t len){
        if(len < readableBytes() ){
            readerIndex_ += len;
        }else {
            retrieveAll();
        }
    }
    void retrieveAll(){
        readerIndex_ = kCheapPrepend;
        writerIndex_ = kCheapPrepend;
    }
    // 结合ET模式的分散读
    ssize_t readFd(int fd, int* savedErrno) ;

    // 发送端
    void prepend(const void* data, size_t len){
        // 确保前面的预留空间还够用 (通常初始化时预留了 8 字节)
        if (readerIndex_ < len) {
            return; 
        }
        // 读游标倒退
        readerIndex_ -= len;
        const char* d = static_cast<const char*>(data);
        // 把数据拷贝到倒退后的位置
        std::copy(d, d + len, begin() + readerIndex_);
    }

    void prependInt32(int32_t x){//写入 4 字节整型的便捷函数
        int32_t be32 = htonl(x); // 主机字节序转网络字节序
        prepend(&be32, sizeof(be32));
    }
private:
    std::vector<char> buffer_;// 存储数据的容器，包含头部预留
    // 获取容器位置
    // 给非 const 函数用的（返回可修改的指针）
    char* begin() { return &*buffer_.begin(); }
    // // 给 const 函数用的（返回只读指针）
    const char* begin() const { return &*buffer_.begin(); }
    void ensureWritableBytes(size_t len) {// 检测写入数据时是否需要扩容
        if (writableBytes() < len) {
            makeSpace(len);
        }
    }

    // 两个游标
    size_t readerIndex_;
    size_t writerIndex_;

    // 扩容和搬移
    void makeSpace(size_t len) {
        // 尾部可写空间 + 头部死区空间 < 需求长度 + 预留的 8 字节
        if ( writableBytes() + prependableBytes() < len + kCheapPrepend ) {
            // 真实扩容 (向 OS 申请内存)
            buffer_.resize(writerIndex_ + len);
        } else {
            // 内部搬移 (Tighten)：将状态 2 的 Readable 数据平移到 prepend 之后
            size_t readable = readableBytes();//已写入数据数量
            std::copy(begin() + readerIndex_,
                    begin() + writerIndex_,
                    begin() + kCheapPrepend );
            // 重置游标
            readerIndex_ = kCheapPrepend;
            writerIndex_ = kCheapPrepend + readable;
        }
    }
};

inline ssize_t Buffer::readFd(int fd, int* savedErrno){
    
    // 准备两块独立的内存区域 (分散读)
    struct iovec vec[2];
    // 分别为buffer本身的空间和栈空间
    const size_t writable = writableBytes();
    vec[0].iov_base = begin() + writerIndex_;
    vec[0].iov_len = writable;

    // 栈分配空间64KB
    char extrabuf[65536];
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);

    // 动态决定使用几块内存
    // 如果 Buffer 本身的空间极大（比如 >= 64KB），那就没必要用栈上的 extrabuf 了
    // 否则，两块内存一起上
    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
    
    // 使用分散读
    // 一次系统调用，内核会自动先把数据填满 vec[0]，如果还有剩的，再继续填入 vec[1]
    const ssize_t n = ::readv(fd, vec, iovcnt);
    
    if (n < 0) {
        // 读取发生错误，将错误码传给外层处理（比如 EAGAIN）
        *savedErrno = errno;
    } else if (static_cast<size_t>(n) <= writable) {
        // 第一块内存（Buffer 内部空间）足够装下所有读到的数据
        // 我们只需要把游标向后移动 n 个字节即可
        writerIndex_ += n;
    } else {
        // 读到的数据太多，第一块装满了，溢出到了栈上的 extrabuf 里
        // 此时 Buffer 内部的 Writable 空间已经被彻底榨干
        writerIndex_ = buffer_.size();
        
        // 将溢出在栈上的数据，追加到 Buffer 中
        // 内部会自动触发 makeSpace() ，进行扩容或者内存搬移
        append(extrabuf, n - writable);
    }
    
    return n;
}

// class Buffer {
// public:
//     Buffer() : readerIndex_(0) {}
//     ~Buffer() = default;

//     // 追加数据到可写区
//     void append(const char* data, size_t len) {
//         buffer_.append(data, len);
//     }

//     // 还有多少有效数据可读
//     size_t readableBytes() const {
//         return buffer_.size() - readerIndex_;
//     }

//     // 返回可读数据的起始指针
//     const char* peek() const {
//         return buffer_.data() + readerIndex_;
//     }

//     // 读走 len 字节后，移动读游标
//     void retrieve(size_t len) {
//         readerIndex_ += len;
//         // 【极简精髓】：只要被读空了，立刻清零复位，防止 string 无限膨胀
//         if (readerIndex_ == buffer_.size()) {
//             retrieveAll();
//         }
//     }

//     void retrieveAll() {
//         buffer_.clear();
//         readerIndex_ = 0;
//     }

//     // 提取所有数据为 string 并清空
//     std::string retrieveAllAsString() {
//         std::string str(peek(), readableBytes());
//         retrieveAll();
//         return str;
//     }

// private:
//     std::string buffer_;
//     size_t readerIndex_;
// };