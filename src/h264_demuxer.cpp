#include "h264_demuxer.h"

// ==================== H.264 码流解析 ====================
// findNextStartCode 和 getNextNalu 均复用 h264_demuxer.h 中的共享自由函数

NaluUnit H264Demuxer::getNextNalu() {
    NaluUnit nalu;

    // 1. 从当前 readIndex_ 开始寻找下一个起始码（委托共享 free function）
    const uint8_t* searchStart = dataBegin() + readIndex_;
    const uint8_t* startCode = findH264StartCode(searchStart, dataEnd());

    if (!startCode) {
        return nalu; // 流已耗尽，返回空 NaluUnit
    }

    // 2. 判定起始码长度
    int scLen = 3;
    if (startCode + 3 < dataEnd() && startCode[2] == 0x00 && startCode[3] == 0x01) {
        scLen = 4;
    }

    // 3. 搜索下一个起始码（确定当前 NALU 边界）
    const uint8_t* nextStartCode = findH264StartCode(startCode + scLen, dataEnd());

    // 4. 确定 NALU 数据区间
    const uint8_t* payloadStart = startCode + scLen;
    const uint8_t* payloadEnd   = nextStartCode ? nextStartCode : dataEnd();

    if (payloadStart < payloadEnd) {
        nalu.payload.insert(nalu.payload.end(), payloadStart, payloadEnd);
        nalu.type = getNaluType(nalu.payload[0]);
    }

    // 5. 更新 readIndex_（使用指针差值还原索引）
    readIndex_ = (nextStartCode ? nextStartCode : dataEnd()) - dataBegin();

    return nalu;
}
