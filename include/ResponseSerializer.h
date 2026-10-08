#pragma once

#include <cstdint>
#include <string>

#include "LatencyProfiler.h"
#include "game_ai.pb.h"

class ResponseSerializer {
public:
    static std::string successJson(const FrameContextPtr& ctx,
                                   const vision::FrameResponse& response);
    static std::string errorJson(uint64_t frame_id,
                                 const std::string& code,
                                 const std::string& message,
                                 const FrameContextPtr& ctx = nullptr);
    static std::string frameTcpPayload(const std::string& json);

private:
    static std::string escapeJson(const std::string& value);
};
