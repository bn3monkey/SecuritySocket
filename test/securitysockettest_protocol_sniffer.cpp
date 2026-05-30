// ProtocolSniffer unit tests — HTTP vs Custom single-port detection.
//
// Pins the byte-0 classification contract handleSniff() depends on: confirmed
// "<METHOD> " is HTTP, a strict method prefix is NEED_MORE, and anything that
// can't be a request line is CUSTOM.

#include <gtest/gtest.h>

#include "../src/implementation/protocol/ProtocolSniffer.hpp"

#include <cstring>
#include <string>

using Bn3Monkey::Protocol;
using Bn3Monkey::ProtocolSniffer;

namespace {
Protocol sniff(const std::string& s) {
    return ProtocolSniffer::detect(s.data(), s.size());
}
}  // namespace

TEST(ProtocolSniffer, DetectsEachHttpMethod) {
    EXPECT_EQ(Protocol::HTTP, sniff("GET /index.html HTTP/1.1\r\n"));
    EXPECT_EQ(Protocol::HTTP, sniff("PUT /x HTTP/1.1\r\n"));
    EXPECT_EQ(Protocol::HTTP, sniff("HEAD /x HTTP/1.1\r\n"));
    EXPECT_EQ(Protocol::HTTP, sniff("POST /x HTTP/1.1\r\n"));
    EXPECT_EQ(Protocol::HTTP, sniff("PATCH /x HTTP/1.1\r\n"));
    EXPECT_EQ(Protocol::HTTP, sniff("DELETE /x HTTP/1.1\r\n"));
    EXPECT_EQ(Protocol::HTTP, sniff("OPTIONS /x HTTP/1.1\r\n"));
    EXPECT_EQ(Protocol::HTTP, sniff("CONNECT host:443 HTTP/1.1\r\n"));
    EXPECT_EQ(Protocol::HTTP, sniff("TRACE /x HTTP/1.1\r\n"));
}

TEST(ProtocolSniffer, MethodPrefixIsNeedMore) {
    // Strict prefixes of a method — can't decide until the separator arrives.
    EXPECT_EQ(Protocol::NEED_MORE, sniff("G"));
    EXPECT_EQ(Protocol::NEED_MORE, sniff("GE"));
    EXPECT_EQ(Protocol::NEED_MORE, sniff("GET"));      // no SP yet
    EXPECT_EQ(Protocol::NEED_MORE, sniff("P"));        // POST/PUT/PATCH
    EXPECT_EQ(Protocol::NEED_MORE, sniff("OPTION"));
    EXPECT_EQ(Protocol::NEED_MORE, sniff("OPTIONS"));  // no SP yet
}

TEST(ProtocolSniffer, EmptyBufferIsNeedMore) {
    EXPECT_EQ(Protocol::NEED_MORE, ProtocolSniffer::detect(nullptr, 0));
    EXPECT_EQ(Protocol::NEED_MORE, sniff(""));
}

TEST(ProtocolSniffer, MethodWithoutSpaceIsCustom) {
    // Full token present but not followed by SP — not a valid request line.
    EXPECT_EQ(Protocol::CUSTOM, sniff("GETX"));
    EXPECT_EQ(Protocol::CUSTOM, sniff("POSTING"));
    EXPECT_EQ(Protocol::CUSTOM, sniff("GET\t/x"));  // tab, not space
}

TEST(ProtocolSniffer, BinaryHeaderIsCustom) {
    // A Custom binary header: first byte isn't an uppercase method letter.
    const char hdr[] = { 0x00, 0x01, 0x02, 0x03, 0x10, 0x20 };
    EXPECT_EQ(Protocol::CUSTOM, ProtocolSniffer::detect(hdr, sizeof(hdr)));

    // High-bit / lowercase leads also resolve immediately to CUSTOM.
    EXPECT_EQ(Protocol::CUSTOM, sniff("\x80\x00 stuff"));
    EXPECT_EQ(Protocol::CUSTOM, sniff("get /x"));   // lowercase, not a token
}

TEST(ProtocolSniffer, ResolvesByMaxSniffBytes) {
    // Any 8-byte input must classify (never NEED_MORE past MAX_SNIFF_BYTES).
    EXPECT_NE(Protocol::NEED_MORE, sniff("OPTIONS "));   // HTTP
    EXPECT_NE(Protocol::NEED_MORE, sniff("ZZZZZZZZ"));   // CUSTOM
    EXPECT_EQ(8u, ProtocolSniffer::MAX_SNIFF_BYTES);
}

TEST(ProtocolSniffer, SingleByteBinaryDecidesCustomImmediately) {
    // One non-method byte is enough to rule out HTTP.
    const char b = 0x42 + 0;  // 'B' — no method begins with B
    EXPECT_EQ(Protocol::CUSTOM, ProtocolSniffer::detect("B", 1));
    (void)b;
}
