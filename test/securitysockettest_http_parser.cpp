// HttpParser unit tests — incremental parse, header inspection, and the
// RFC 6455 Sec-WebSocket-Accept computation (validates the local SHA1+base64).

#include <gtest/gtest.h>

#include "../src/implementation/protocol/HttpParser.hpp"

#include <cstring>
#include <string>

using Bn3Monkey::HttpParser;

namespace {
using PR = HttpParser::ParsedRequest;

// NOTE: ParsedRequest holds *pointers into the source buffer* (picohttpparser
// returns views, not copies). This helper is only safe when the result is
// consumed within the same full-expression (e.g. contentLength(parse("..."))),
// because the string temporary lives until the end of that statement. A test
// that STORES the PR and reads its pointer fields (method/path/header values)
// in later statements must instead keep its own named source buffer alive —
// see the tests below that parse into a local `raw`.
PR parse(const std::string& s) {
    return HttpParser::parse(s.data(), s.size(), 0);
}

std::string headerValue(const PR& r, const char* name) {
    const auto* h = HttpParser::findHeader(r, name);
    return h ? std::string(h->value, h->value_len) : std::string("<absent>");
}
}  // namespace

TEST(HttpParser, ParsesCompleteRequestLineAndHeaders) {
    // `raw` must outlive every access to `r` — r.method/r.path/header values
    // are pointers into it.
    std::string raw = "GET /path/to?x=1 HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "Content-Length: 0\r\n"
                      "\r\n";
    PR r = HttpParser::parse(raw.data(), raw.size(), 0);
    ASSERT_EQ(HttpParser::ParseStatus::OK, r.status);
    EXPECT_EQ(std::string("GET"),        std::string(r.method, r.method_len));
    EXPECT_EQ(std::string("/path/to?x=1"), std::string(r.path, r.path_len));
    EXPECT_EQ(1, r.minor_version);
    EXPECT_EQ(std::string("example.com"), headerValue(r, "Host"));
    // header_end points just past the blank line — the body would start there
    // (here the request has no body, so it equals the whole buffer size).
    EXPECT_EQ(r.header_end, raw.size());
}

TEST(HttpParser, IncompleteHeaderBlockIsIncomplete) {
    PR r = parse("GET / HTTP/1.1\r\nHost: example.com\r\n"); // no final CRLF
    EXPECT_EQ(HttpParser::ParseStatus::INCOMPLETE, r.status);
}

TEST(HttpParser, MalformedRequestLineIsMalformed) {
    // A bare control byte in the request line — picohttpparser rejects it.
    std::string raw = "GET / HTTP/1.1\r\n";
    raw.push_back('\x01');           // illegal header field start
    raw += "X: y\r\n\r\n";
    PR r = HttpParser::parse(raw.data(), raw.size(), 0);
    EXPECT_EQ(HttpParser::ParseStatus::MALFORMED, r.status);
}

TEST(HttpParser, FindHeaderIsCaseInsensitive) {
    std::string raw = "GET / HTTP/1.1\r\nContent-Type: text/plain\r\n\r\n";
    PR r = HttpParser::parse(raw.data(), raw.size(), 0);
    EXPECT_EQ(std::string("text/plain"), headerValue(r, "content-type"));
    EXPECT_EQ(std::string("text/plain"), headerValue(r, "CONTENT-TYPE"));
    EXPECT_EQ(nullptr, HttpParser::findHeader(r, "Authorization"));
}

TEST(HttpParser, ContentLengthParsedOrMinusOne) {
    EXPECT_EQ(42, HttpParser::contentLength(
        parse("POST / HTTP/1.1\r\nContent-Length: 42\r\n\r\n")));
    EXPECT_EQ(0, HttpParser::contentLength(
        parse("POST / HTTP/1.1\r\nContent-Length: 0\r\n\r\n")));
    EXPECT_EQ(-1, HttpParser::contentLength(
        parse("GET / HTTP/1.1\r\nHost: x\r\n\r\n")));   // absent
    EXPECT_EQ(-1, HttpParser::contentLength(
        parse("POST / HTTP/1.1\r\nContent-Length: abc\r\n\r\n")));  // non-numeric
}

TEST(HttpParser, KeepAliveDefaultsByVersion) {
    // HTTP/1.1 default keep-alive
    EXPECT_TRUE(HttpParser::keepAlive(
        parse("GET / HTTP/1.1\r\nHost: x\r\n\r\n")));
    // HTTP/1.1 + Connection: close
    EXPECT_FALSE(HttpParser::keepAlive(
        parse("GET / HTTP/1.1\r\nConnection: close\r\n\r\n")));
    // HTTP/1.0 default close
    EXPECT_FALSE(HttpParser::keepAlive(
        parse("GET / HTTP/1.0\r\nHost: x\r\n\r\n")));
    // HTTP/1.0 + Connection: keep-alive
    EXPECT_TRUE(HttpParser::keepAlive(
        parse("GET / HTTP/1.0\r\nConnection: keep-alive\r\n\r\n")));
}

TEST(HttpParser, KeepAliveTokenWiseInMultiValueConnection) {
    // Connection: keep-alive, Upgrade  — "close" not present → still alive.
    EXPECT_TRUE(HttpParser::keepAlive(
        parse("GET / HTTP/1.1\r\nConnection: keep-alive, Upgrade\r\n\r\n")));
}

TEST(HttpParser, DetectsWebSocketUpgrade) {
    std::string raw = "GET /ws HTTP/1.1\r\n"
                      "Host: x\r\n"
                      "Upgrade: websocket\r\n"
                      "Connection: Upgrade\r\n"
                      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                      "Sec-WebSocket-Version: 13\r\n\r\n";
    PR r = HttpParser::parse(raw.data(), raw.size(), 0);
    EXPECT_TRUE(HttpParser::isWebSocketUpgrade(r));
}

TEST(HttpParser, NonUpgradeRequestIsNotWebSocket) {
    EXPECT_FALSE(HttpParser::isWebSocketUpgrade(
        parse("GET / HTTP/1.1\r\nHost: x\r\n\r\n")));
    // Upgrade header present but Connection lacks the upgrade token.
    EXPECT_FALSE(HttpParser::isWebSocketUpgrade(
        parse("GET / HTTP/1.1\r\nUpgrade: websocket\r\nConnection: keep-alive\r\n\r\n")));
}

TEST(HttpParser, ComputeAcceptMatchesRfc6455Vector) {
    // RFC 6455 §1.3 canonical example: this exact key must produce this accept.
    char out[HttpParser::ACCEPT_BUF_SIZE];
    ASSERT_TRUE(HttpParser::computeAccept(
        "dGhlIHNhbXBsZSBub25jZQ==", 24, out, sizeof(out)));
    EXPECT_STREQ("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", out);
}

TEST(HttpParser, ComputeAcceptFromParsedRequest) {
    std::string raw = "GET /ws HTTP/1.1\r\n"
                      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
    PR r = HttpParser::parse(raw.data(), raw.size(), 0);
    char out[HttpParser::ACCEPT_BUF_SIZE];
    ASSERT_TRUE(HttpParser::computeAccept(r, out, sizeof(out)));
    EXPECT_STREQ("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", out);
}

TEST(HttpParser, ComputeAcceptFailsWithoutKey) {
    std::string raw = "GET /ws HTTP/1.1\r\nHost: x\r\n\r\n";
    PR r = HttpParser::parse(raw.data(), raw.size(), 0);
    char out[HttpParser::ACCEPT_BUF_SIZE];
    EXPECT_FALSE(HttpParser::computeAccept(r, out, sizeof(out)));
}

TEST(HttpParser, SerializeHandshakeShape) {
    char out[256];
    const size_t n = HttpParser::serializeHandshake(
        "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", out, sizeof(out));
    ASSERT_GT(n, 0u);
    std::string s(out, n);
    EXPECT_NE(std::string::npos, s.find("HTTP/1.1 101 Switching Protocols\r\n"));
    EXPECT_NE(std::string::npos, s.find("Upgrade: websocket\r\n"));
    EXPECT_NE(std::string::npos, s.find("Connection: Upgrade\r\n"));
    EXPECT_NE(std::string::npos,
              s.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"));
    // Ends with the blank-line terminator.
    EXPECT_EQ("\r\n\r\n", s.substr(s.size() - 4));
}

TEST(HttpParser, SerializeHandshakeReturnsZeroOnOverflow) {
    char tiny[16];
    EXPECT_EQ(0u, HttpParser::serializeHandshake(
        "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", tiny, sizeof(tiny)));
}
