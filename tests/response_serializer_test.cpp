#include <gtest/gtest.h>

#include <arpa/inet.h>

#include <cstring>

#include "ResponseSerializer.h"

TEST(ResponseSerializerTest, EscapesClassNamesAndFramesTcpPayload) {
    auto ctx = std::make_shared<FrameContext>();
    ctx->t_parsed = ctx->t_start + std::chrono::microseconds(10);
    ctx->t_grpc_sent = ctx->t_parsed + std::chrono::microseconds(20);
    ctx->t_grpc_recv = ctx->t_grpc_sent + std::chrono::microseconds(100);
    ctx->t_finish = ctx->t_grpc_recv + std::chrono::microseconds(30);

    vision::FrameResponse response;
    response.set_frame_id(ctx->trace_id);
    response.set_inference_latency_us(70);
    auto* box = response.add_boxes();
    box->set_class_name("person \"fast\"\n");
    box->set_confidence(0.95f);
    box->set_x(100.0f);
    box->set_y(120.0f);
    box->set_width(80.0f);
    box->set_height(160.0f);

    const std::string json = ResponseSerializer::successJson(ctx, response);
    EXPECT_NE(json.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(json.find("person \\\"fast\\\"\\n"), std::string::npos);
    EXPECT_NE(json.find("\"infer_us\":70"), std::string::npos);

    const std::string framed = ResponseSerializer::frameTcpPayload(json);
    ASSERT_EQ(framed.size(), json.size() + 4);
    uint32_t len = 0;
    std::memcpy(&len, framed.data(), sizeof(len));
    EXPECT_EQ(ntohl(len), json.size());
    EXPECT_EQ(framed.substr(4), json);
}

TEST(ResponseSerializerTest, ErrorJsonContainsStableShape) {
    const std::string json = ResponseSerializer::errorJson(7, "OVERLOADED", "slow down");
    EXPECT_NE(json.find("\"frame_id\":7"), std::string::npos);
    EXPECT_NE(json.find("\"ok\":false"), std::string::npos);
    EXPECT_NE(json.find("\"code\":\"OVERLOADED\""), std::string::npos);
    EXPECT_NE(json.find("\"detections\":[]"), std::string::npos);
}
