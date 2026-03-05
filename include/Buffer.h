#pragma once
#include <string>

class Buffer {
public:
    Buffer() : readerIndex_(0) {}
    ~Buffer() = default;

    // 追加数据到可写区
    void append(const char* data, size_t len) {
        buffer_.append(data, len);
    }

    // 还有多少有效数据可读
    size_t readableBytes() const {
        return buffer_.size() - readerIndex_;
    }

    // 返回可读数据的起始指针
    const char* peek() const {
        return buffer_.data() + readerIndex_;
    }

    // 读走 len 字节后，移动读游标
    void retrieve(size_t len) {
        readerIndex_ += len;
        // 【极简精髓】：只要被读空了，立刻清零复位，防止 string 无限膨胀
        if (readerIndex_ == buffer_.size()) {
            retrieveAll();
        }
    }

    void retrieveAll() {
        buffer_.clear();
        readerIndex_ = 0;
    }

    // 提取所有数据为 string 并清空
    std::string retrieveAllAsString() {
        std::string str(peek(), readableBytes());
        retrieveAll();
        return str;
    }

private:
    std::string buffer_;
    size_t readerIndex_;
};