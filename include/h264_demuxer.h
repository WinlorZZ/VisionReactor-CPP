#pragma once
#include <vector>
#include <cstdint>
#include <utility>

// ==================== 共享工具：零拷贝 H.264 起始码搜索 ====================
//
// 在 [start, end) 区间内搜索 Annex B 起始码 00 00 01 (3字节) 或 00 00 00 01 (4字节)。
// 快路径：起始码首字节必须为 0x00，非零直接跳过。
// 边界安全：始终检查 ptr+N < end，尾部残留的半截起始码自然返回 nullptr。
//
// 返回指向起始码首字节的指针，未找到返回 nullptr。
inline const uint8_t* findH264StartCode(const uint8_t* start, const uint8_t* end) {
    const uint8_t* p = start;
    while (p < end) {
        if (*p != 0) { ++p; continue; }                    // 快路径
        if (p + 3 < end && p[1] == 0 && p[2] == 0 && p[3] == 1) return p; // 4字节
        if (p + 2 < end && p[1] == 0 && p[2] == 1)          return p; // 3字节
        ++p;
    }
    return nullptr;
}

// 从 NALU 首字节提取类型（低 5 位）
inline int getNaluType(uint8_t firstByte) { return firstByte & 0x1F; }

// ==================== NALU 零拷贝视图 ====================
// 用于在不拷贝数据的情况下描述一个 NALU 单元（流式解析场景）
struct NaluView {
    const uint8_t* data;  // 指向 NALU 负载首字节（已剥离起始码）
    size_t size;          // 负载长度
    int type;             // NALU 类型

    bool valid() const { return data != nullptr && size > 0; }
};

// ==================== NALU 数据单元载体（拷贝版本，离线解析场景） ====================
struct NaluUnit {
    std::vector<uint8_t> payload; // 纯 NALU 数据（剥离了起始码）
    int type{0};                  // NALU 类型 (7=SPS, 8=PPS, 5=IDR)

    bool isEmpty() const { return payload.empty(); }
};

// ==================== H.264 解复用器 ====================
// 支持两种使用模式：
//   - 离线模式: 构造时注入完整流数据，getNextNalu() 返回拷贝的 NaluUnit
//   - 流式模式: 使用共享的 findH264StartCode / getNaluType 配合 Buffer 裸指针
class H264Demuxer {
public:
    explicit H264Demuxer(const std::vector<uint8_t>& streamData)
        : streamData_(streamData), readIndex_(0) {}

    // 离线模式：每次返回一个完整的 NALU（拷贝语义）
    NaluUnit getNextNalu();

private:
    // 内部使用共享的 free function，封装索引 → 指针的转换
    const uint8_t* dataBegin() const { return streamData_.data(); }
    const uint8_t* dataEnd()   const { return streamData_.data() + streamData_.size(); }

    std::vector<uint8_t> streamData_;
    int readIndex_; // 当前解析位置（streamData_ 中的索引）
};
