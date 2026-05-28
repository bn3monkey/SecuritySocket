// HttpResponseImpl unit tests.
//
// Build a response with the fluent builder API and serialize it into a
// scratch buffer. Each test verifies the resulting bytes — the canonical
// shape Phase 6 dispatch will write back to the socket.

#include <gtest/gtest.h>

#include "../src/implementation/http/HttpResponse.hpp"

#include <cstring>
#include <string>

using Bn3Monkey::HttpResponse;
using Bn3Monkey::HttpResponseImpl;

namespace {

// Convenience — serialize into a stack buffer and return as std::string for
// easy substring matching. Big enough for everything these tests exercise.
std::string serialize(const HttpResponseImpl& res) {
    char buf[4096];
    const size_t n = res.serialize(buf, sizeof(buf));
    return std::string(buf, n);
}

// True if 'whole' contains 'sub'. Header order is fixed (declaration order),
// but we use substring rather than exact equality so we don't lock the test
// to a specific Content-Length placement.
bool contains(const std::string& whole, const char* sub) {
    return whole.find(sub) != std::string::npos;
}

}  // namespace

TEST(HttpResponse, MinimalGetReturnsStatusLineAndEmptyBody) {
    HttpResponseImpl res;
    res.status(200);

    const std::string out = serialize(res);
    EXPECT_TRUE(contains(out, "HTTP/1.1 200 OK\r\n"));
    // Empty body — Content-Length must still be 0 (RFC 7230 §3.3.2 cases
    // where 0 is the explicit length for an absent body).
    EXPECT_TRUE(contains(out, "Content-Length: 0\r\n"));
    // The bare headers/body separator must be present.
    EXPECT_TRUE(contains(out, "\r\n\r\n"));
}

TEST(HttpResponse, FluentChainingReturnsSelf) {
    // The builder returns a reference to the abstract base — each chained
    // call must compile and end up writing to the same instance.
    HttpResponseImpl res;
    res.status(201)
       .header("X-Trace", "abc")
       .body("hi", 2);

    const std::string out = serialize(res);
    EXPECT_TRUE(contains(out, "HTTP/1.1 201 Created\r\n"));
    EXPECT_TRUE(contains(out, "X-Trace: abc\r\n"));
    EXPECT_TRUE(contains(out, "Content-Length: 2\r\n"));
    EXPECT_TRUE(contains(out, "\r\n\r\nhi"));
}

TEST(HttpResponse, JsonSetsBodyAndContentType) {
    HttpResponseImpl res;
    const char body[] = "{\"ok\":true}";
    res.status(200).json(body);

    const std::string out = serialize(res);
    EXPECT_TRUE(contains(out, "Content-Type: application/json\r\n"));
    EXPECT_TRUE(contains(out, "Content-Length: 11\r\n"));
    EXPECT_TRUE(contains(out, "\r\n\r\n{\"ok\":true}"));
}

TEST(HttpResponse, BodyBytesPreservedExactly) {
    // Non-text body (raw bytes incl. high-bit) must serialize byte-for-byte.
    HttpResponseImpl res;
    const unsigned char raw[] = { 0x00, 0xFF, 0x10, 0x80, 0x7F };
    res.status(200).body(raw, sizeof(raw));

    char buf[256];
    const size_t n = res.serialize(buf, sizeof(buf));
    ASSERT_GT(n, sizeof(raw));
    // The last 5 bytes of the serialized message are the body.
    EXPECT_EQ(0, std::memcmp(buf + n - sizeof(raw), raw, sizeof(raw)));
}

TEST(HttpResponse, KnownStatusCodesHaveReasonPhrases) {
    EXPECT_STREQ("OK",                     HttpResponseImpl::reasonPhrase(200));
    EXPECT_STREQ("Created",                HttpResponseImpl::reasonPhrase(201));
    EXPECT_STREQ("No Content",             HttpResponseImpl::reasonPhrase(204));
    EXPECT_STREQ("Bad Request",            HttpResponseImpl::reasonPhrase(400));
    EXPECT_STREQ("Not Found",              HttpResponseImpl::reasonPhrase(404));
    EXPECT_STREQ("Method Not Allowed",     HttpResponseImpl::reasonPhrase(405));
    EXPECT_STREQ("Payload Too Large",      HttpResponseImpl::reasonPhrase(413));
    EXPECT_STREQ("Internal Server Error",  HttpResponseImpl::reasonPhrase(500));
}

TEST(HttpResponse, UnknownStatusCodeStillSerializes) {
    // 999 isn't a known code — emit status line without a reason phrase.
    // RFC 7230 §3.1.2 makes the phrase optional.
    HttpResponseImpl res;
    res.status(999);
    const std::string out = serialize(res);
    EXPECT_TRUE(contains(out, "HTTP/1.1 999\r\n"));
}

TEST(HttpResponse, MultipleHeadersAppearInOrder) {
    HttpResponseImpl res;
    res.status(200)
       .header("A", "1")
       .header("B", "2")
       .header("C", "3");

    const std::string out = serialize(res);
    // Each header present, and B appears between A and C.
    const auto a = out.find("A: 1\r\n");
    const auto b = out.find("B: 2\r\n");
    const auto c = out.find("C: 3\r\n");
    ASSERT_NE(std::string::npos, a);
    ASSERT_NE(std::string::npos, b);
    ASSERT_NE(std::string::npos, c);
    EXPECT_LT(a, b);
    EXPECT_LT(b, c);
}

TEST(HttpResponse, OverlongHeaderRejectedSilently) {
    // Header storage caps at HEADER_NAME_CAP / HEADER_VALUE_CAP. Overlong
    // entries are dropped — better than truncating (which would forge a
    // different header from what the caller asked for).
    HttpResponseImpl res;
    res.status(200);
    std::string long_value(HttpResponseImpl::HEADER_VALUE_CAP, 'x');
    res.header("X-Big", long_value.c_str());

    EXPECT_EQ(0u, res.headerCount());
    const std::string out = serialize(res);
    EXPECT_FALSE(contains(out, "X-Big"));
}

TEST(HttpResponse, HeaderCountCapEnforced) {
    HttpResponseImpl res;
    res.status(200);
    for (size_t i = 0; i < HttpResponseImpl::MAX_HEADERS; ++i) {
        res.header("X-Pad", "v");
    }
    EXPECT_EQ(HttpResponseImpl::MAX_HEADERS, res.headerCount());
    res.header("overflow", "v");
    EXPECT_EQ(HttpResponseImpl::MAX_HEADERS, res.headerCount());
}

TEST(HttpResponse, SerializeReturnsZeroOnUndersizedBuffer) {
    // Capacity-overflow signal: 0 written, caller knows to bump the buffer.
    HttpResponseImpl res;
    res.status(200).body("hello world", 11);

    char tiny[16];  // far less than the full serialized response needs
    EXPECT_EQ(0u, res.serialize(tiny, sizeof(tiny)));
}

TEST(HttpResponse, ErrorStatusCodesProduceUsableMessages) {
    // Phase 6 will emit these as framework-generated responses — sanity-check
    // a couple shapes that need to round-trip cleanly to common clients.
    {
        HttpResponseImpl res;
        res.status(404).json("{\"error\":\"not found\"}");
        const std::string out = serialize(res);
        EXPECT_TRUE(contains(out, "HTTP/1.1 404 Not Found\r\n"));
        EXPECT_TRUE(contains(out, "Content-Type: application/json\r\n"));
    }
    {
        HttpResponseImpl res;
        res.status(413);  // Payload Too Large — body-size enforcement path
        const std::string out = serialize(res);
        EXPECT_TRUE(contains(out, "HTTP/1.1 413 Payload Too Large\r\n"));
        EXPECT_TRUE(contains(out, "Content-Length: 0\r\n"));
    }
}
