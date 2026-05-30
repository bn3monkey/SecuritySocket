// WsFrameCodec unit tests — RFC 6455 frame decode/encode.
//
// Pure in-process: build masked client->server frames by hand, decode them,
// and assert the in-place-unmasked view. Encode tests verify the server-side
// (unmasked) wire shape Phase 6 writes back. These pin the codec contract that
// the ReceivingWebSocketFrame / SendingWebSocketResponse states rely on.

#include <gtest/gtest.h>

#include "../src/implementation/protocol/WsFrameCodec.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using Bn3Monkey::WsFrameCodec;
using Bn3Monkey::WsFrameView;
using Bn3Monkey::WsOpcode;

namespace {

// Assemble a client->server (masked) frame. Mirrors what curl / a browser puts
// on the wire so decode() exercises the real unmask path.
std::vector<char> maskedFrame(WsOpcode opcode, const std::string& payload,
                              bool fin = true,
                              const unsigned char* mask = nullptr)
{
    static const unsigned char kDefaultMask[4] = { 0x12, 0x34, 0x56, 0x78 };
    const unsigned char* m = mask ? mask : kDefaultMask;

    std::vector<char> f;
    f.push_back(static_cast<char>((fin ? 0x80 : 0x00) |
                                  (static_cast<uint8_t>(opcode) & 0x0F)));

    const size_t len = payload.size();
    if (len < 126) {
        f.push_back(static_cast<char>(0x80 | len));
    } else if (len <= 0xFFFF) {
        f.push_back(static_cast<char>(0x80 | 126));
        f.push_back(static_cast<char>((len >> 8) & 0xFF));
        f.push_back(static_cast<char>(len & 0xFF));
    } else {
        f.push_back(static_cast<char>(0x80 | 127));
        const uint64_t n = static_cast<uint64_t>(len);
        for (int i = 0; i < 8; ++i)
            f.push_back(static_cast<char>((n >> (56 - 8 * i)) & 0xFF));
    }
    for (int i = 0; i < 4; ++i) f.push_back(static_cast<char>(m[i]));
    for (size_t i = 0; i < len; ++i)
        f.push_back(static_cast<char>(static_cast<unsigned char>(payload[i]) ^ m[i & 3]));
    return f;
}

} // namespace

TEST(WsFrameCodec, DecodesMaskedBinaryFrame) {
    auto f = maskedFrame(WsOpcode::BINARY, "hello");
    WsFrameView v;
    ASSERT_EQ(WsFrameCodec::DecodeResult::OK,
              WsFrameCodec::decode(f.data(), f.size(), v));
    EXPECT_TRUE(v.fin);
    EXPECT_EQ(WsOpcode::BINARY, v.opcode);
    ASSERT_EQ(5u, v.payload_len);
    EXPECT_EQ(0, std::memcmp(v.payload, "hello", 5));
    EXPECT_EQ(f.size(), v.frame_len);
}

TEST(WsFrameCodec, NeedMoreWhenHeaderIncomplete) {
    auto f = maskedFrame(WsOpcode::BINARY, "hello");
    WsFrameView v;
    EXPECT_EQ(WsFrameCodec::DecodeResult::NEED_MORE,
              WsFrameCodec::decode(f.data(), 1, v));
}

TEST(WsFrameCodec, NeedMoreWhenPayloadIncomplete) {
    auto f = maskedFrame(WsOpcode::BINARY, "hello world");
    WsFrameView v;
    EXPECT_EQ(WsFrameCodec::DecodeResult::NEED_MORE,
              WsFrameCodec::decode(f.data(), f.size() - 3, v));
}

TEST(WsFrameCodec, RejectsUnmaskedClientFrame) {
    std::vector<char> f;
    f.push_back(static_cast<char>(0x82)); // FIN + BINARY
    f.push_back(static_cast<char>(0x03)); // no mask bit, len 3
    f.push_back('a'); f.push_back('b'); f.push_back('c');
    WsFrameView v;
    EXPECT_EQ(WsFrameCodec::DecodeResult::INVALID,
              WsFrameCodec::decode(f.data(), f.size(), v));
}

TEST(WsFrameCodec, RejectsRsvBits) {
    auto f = maskedFrame(WsOpcode::BINARY, "x");
    f[0] = static_cast<char>(f[0] | 0x40); // set RSV1
    WsFrameView v;
    EXPECT_EQ(WsFrameCodec::DecodeResult::INVALID,
              WsFrameCodec::decode(f.data(), f.size(), v));
}

TEST(WsFrameCodec, RejectsFragmentedControlFrame) {
    auto f = maskedFrame(WsOpcode::PING, "x", /*fin=*/false);
    WsFrameView v;
    EXPECT_EQ(WsFrameCodec::DecodeResult::INVALID,
              WsFrameCodec::decode(f.data(), f.size(), v));
}

TEST(WsFrameCodec, RejectsReservedOpcode) {
    auto f = maskedFrame(static_cast<WsOpcode>(0x3), "x");
    WsFrameView v;
    EXPECT_EQ(WsFrameCodec::DecodeResult::INVALID,
              WsFrameCodec::decode(f.data(), f.size(), v));
}

TEST(WsFrameCodec, Decodes16BitExtendedLength) {
    std::string payload(200, 'z');
    auto f = maskedFrame(WsOpcode::BINARY, payload);
    WsFrameView v;
    ASSERT_EQ(WsFrameCodec::DecodeResult::OK,
              WsFrameCodec::decode(f.data(), f.size(), v));
    EXPECT_EQ(200u, v.payload_len);
    EXPECT_EQ(0, std::memcmp(v.payload, payload.data(), 200));
}

TEST(WsFrameCodec, DecodesPingAndExposesPayload) {
    auto f = maskedFrame(WsOpcode::PING, "ka");
    WsFrameView v;
    ASSERT_EQ(WsFrameCodec::DecodeResult::OK,
              WsFrameCodec::decode(f.data(), f.size(), v));
    EXPECT_EQ(WsOpcode::PING, v.opcode);
    ASSERT_EQ(2u, v.payload_len);
    EXPECT_EQ(0, std::memcmp(v.payload, "ka", 2));
}

TEST(WsFrameCodec, EncodeServerFrameHasNoMaskBit) {
    char out[64];
    const size_t n = WsFrameCodec::encode(WsOpcode::BINARY, "hi", 2, true,
                                          out, sizeof(out));
    ASSERT_EQ(4u, n); // 2 header + 2 payload
    EXPECT_EQ(0x82u, static_cast<unsigned char>(out[0])); // FIN + BINARY
    EXPECT_EQ(0x02u, static_cast<unsigned char>(out[1])); // no mask bit, len 2
    EXPECT_EQ('h', out[2]);
    EXPECT_EQ('i', out[3]);
}

TEST(WsFrameCodec, EncodeUses16BitLengthOver125) {
    std::string payload(300, 'q');
    std::vector<char> out(512);
    const size_t n = WsFrameCodec::encode(WsOpcode::BINARY, payload.data(),
                                          payload.size(), true,
                                          out.data(), out.size());
    ASSERT_EQ(4u + 300u, n); // 2 + 2 ext-len + payload
    EXPECT_EQ(0x82u, static_cast<unsigned char>(out[0]));
    EXPECT_EQ(126u,  static_cast<unsigned char>(out[1]));
    EXPECT_EQ(0x01u, static_cast<unsigned char>(out[2])); // 300 >> 8
    EXPECT_EQ(0x2Cu, static_cast<unsigned char>(out[3])); // 300 & 0xFF
}

TEST(WsFrameCodec, EncodeClosePacksBigEndianCode) {
    char out[16];
    const size_t n = WsFrameCodec::encodeClose(1002, out, sizeof(out));
    ASSERT_EQ(4u, n);
    EXPECT_EQ(0x88u, static_cast<unsigned char>(out[0])); // FIN + CLOSE
    EXPECT_EQ(2,     out[1]);
    EXPECT_EQ(0x03u, static_cast<unsigned char>(out[2])); // 1002 >> 8 == 3
    EXPECT_EQ(0xEAu, static_cast<unsigned char>(out[3])); // 1002 & 0xFF == 234
}

TEST(WsFrameCodec, EncodePongReflectsPayloadUnmasked) {
    char out[64];
    const size_t n = WsFrameCodec::encodePong("ping-data", 9, out, sizeof(out));
    ASSERT_EQ(11u, n);
    EXPECT_EQ(0x8Au, static_cast<unsigned char>(out[0])); // FIN + PONG
    EXPECT_EQ(9u,    static_cast<unsigned char>(out[1])); // no mask, len 9
    EXPECT_EQ(0, std::memcmp(out + 2, "ping-data", 9));
}

TEST(WsFrameCodec, EncodeReturnsZeroOnUndersizedBuffer) {
    char tiny[2];
    EXPECT_EQ(0u, WsFrameCodec::encode(WsOpcode::BINARY, "hello", 5, true,
                                       tiny, sizeof(tiny)));
}
