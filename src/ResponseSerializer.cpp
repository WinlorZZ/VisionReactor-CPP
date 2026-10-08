#include "ResponseSerializer.h"

#include <arpa/inet.h>

#include <cstring>
#include <sstream>

std::string ResponseSerializer::escapeJson(const std::string& value) {
    std::ostringstream out;
    for (unsigned char ch : value) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    const char* hex = "0123456789abcdef";
                    out << "\\u00" << hex[(ch >> 4) & 0x0f] << hex[ch & 0x0f];
                } else {
                    out << ch;
                }
                break;
        }
    }
    return out.str();
}

std::string ResponseSerializer::successJson(const FrameContextPtr& ctx,
                                            const vision::FrameResponse& response) {
    const int64_t parse_us = LatencyProfiler::microseconds_between(ctx->t_start, ctx->t_parsed);
    const int64_t gateway_to_grpc_us = LatencyProfiler::microseconds_between(ctx->t_parsed, ctx->t_grpc_sent);
    const int64_t grpc_round_trip_us = LatencyProfiler::microseconds_between(ctx->t_grpc_sent, ctx->t_grpc_recv);
    const int64_t postprocess_us = LatencyProfiler::microseconds_between(ctx->t_grpc_recv, ctx->t_finish);
    const int64_t infer_us = response.inference_latency_us();
    const int64_t grpc_transport_us = grpc_round_trip_us - infer_us;
    const int64_t total_us = LatencyProfiler::microseconds_between(ctx->t_start, ctx->t_finish);
    const int64_t gateway_us = parse_us + gateway_to_grpc_us + postprocess_us;

    std::ostringstream out;
    out << "{\"frame_id\":" << response.frame_id()
        << ",\"ok\":true"
        << ",\"error\":\"\""
        << ",\"timing\":{"
        << "\"total_us\":" << total_us
        << ",\"gateway_us\":" << gateway_us
        << ",\"parse_us\":" << parse_us
        << ",\"queue_to_grpc_us\":" << gateway_to_grpc_us
        << ",\"grpc_round_trip_us\":" << grpc_round_trip_us
        << ",\"grpc_transport_us\":" << grpc_transport_us
        << ",\"infer_us\":" << infer_us
        << ",\"postprocess_us\":" << postprocess_us
        << "},\"detections\":[";

    for (int i = 0; i < response.boxes_size(); ++i) {
        const auto& box = response.boxes(i);
        if (i > 0) {
            out << ',';
        }
        out << "{\"class\":\"" << escapeJson(box.class_name()) << "\""
            << ",\"confidence\":" << box.confidence()
            << ",\"x\":" << box.x()
            << ",\"y\":" << box.y()
            << ",\"w\":" << box.width()
            << ",\"h\":" << box.height()
            << "}";
    }

    out << "]}";
    return out.str();
}

std::string ResponseSerializer::errorJson(uint64_t frame_id,
                                          const std::string& code,
                                          const std::string& message,
                                          const FrameContextPtr& ctx) {
    int64_t total_us = 0;
    int64_t gateway_us = 0;
    int64_t infer_us = 0;
    if (ctx) {
        total_us = LatencyProfiler::microseconds_between(ctx->t_start, ctx->t_finish);
        gateway_us = total_us;
        infer_us = ctx->t3_python_cost_us;
    }

    std::ostringstream out;
    out << "{\"frame_id\":" << frame_id
        << ",\"ok\":false"
        << ",\"error\":\"" << escapeJson(message) << "\""
        << ",\"code\":\"" << escapeJson(code) << "\""
        << ",\"timing\":{"
        << "\"total_us\":" << total_us
        << ",\"gateway_us\":" << gateway_us
        << ",\"infer_us\":" << infer_us
        << "},\"detections\":[]}";
    return out.str();
}

std::string ResponseSerializer::frameTcpPayload(const std::string& json) {
    const uint32_t len = htonl(static_cast<uint32_t>(json.size()));
    std::string payload(sizeof(len), '\0');
    std::memcpy(&payload[0], &len, sizeof(len));
    payload.append(json);
    return payload;
}
