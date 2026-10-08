#include <gtest/gtest.h>
#include "h264_demuxer.h"

// 测试用例 1：最标准的 4 字节 Start Code 解析 (模拟 SPS 帧)
TEST(H264DemuxerTest, ParseSingleNaluWith4ByteStartCode) {
    // 构造码流: [00 00 00 01] + [67 (SPS头)] + [42 00 1E (Payload)]
    std::vector<uint8_t> mockData = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E};
    H264Demuxer demuxer(mockData);

    NaluUnit nalu = demuxer.getNextNalu();
    
    EXPECT_FALSE(nalu.isEmpty());
    EXPECT_EQ(nalu.type, 7); // 0x67 & 0x1F = 7 (SPS)
    EXPECT_EQ(nalu.payload.size(), 4); // 包含 67 42 00 1E
    EXPECT_EQ(nalu.payload[0], 0x67);

    // 文件读完了，第二次获取应该返回空
    NaluUnit nalu2 = demuxer.getNextNalu();
    EXPECT_TRUE(nalu2.isEmpty());
}

// 测试用例 2：连续切包解析 (SPS + PPS)
TEST(H264DemuxerTest, ParseMultipleNalus) {
    // 构造码流: [00 00 00 01] + [67 42] + [00 00 01] + [68 CE]
    // 注意：第二个是 3 字节的 Start Code
    std::vector<uint8_t> mockData = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 
        0x00, 0x00, 0x01, 0x68, 0xCE
    };
    H264Demuxer demuxer(mockData);

    // 1. 读出第一个 NALU (SPS)
    NaluUnit nalu1 = demuxer.getNextNalu();
    EXPECT_EQ(nalu1.type, 7);
    EXPECT_EQ(nalu1.payload.size(), 2); // 67 42

    // 2. 读出第二个 NALU (PPS)
    NaluUnit nalu2 = demuxer.getNextNalu();
    EXPECT_EQ(nalu2.type, 8); // 0x68 & 0x1F = 8 (PPS)
    EXPECT_EQ(nalu2.payload.size(), 2); // 68 CE
}

// 测试用例 3：容错能力测试（跳过开头的垃圾数据）
TEST(H264DemuxerTest, SkipLeadingGarbageData) {
    // 构造码流: [FF EE] (垃圾数据) + [00 00 00 01] + [65 11] (IDR关键帧)
    std::vector<uint8_t> mockData = {0xFF, 0xEE, 0x00, 0x00, 0x00, 0x01, 0x65, 0x11};
    H264Demuxer demuxer(mockData);

    NaluUnit nalu = demuxer.getNextNalu();
    EXPECT_FALSE(nalu.isEmpty());
    EXPECT_EQ(nalu.type, 5); // 0x65 & 0x1F = 5 (IDR)
    EXPECT_EQ(nalu.payload.size(), 2);
}