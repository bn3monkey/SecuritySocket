// picohttpparser smoke tests — exercise the vendored HTTP request parser
// directly against literal request strings. These tests do not touch any
// SecuritySocket code; they prove the parser was vendored, compiled, and
// linked correctly so later phases can rely on it for the HTTP server.

#include <gtest/gtest.h>

#include <cstring>

#include "picohttpparser.h"

namespace {

constexpr size_t kMaxHeaders = 32;

struct ParseResult {
    int          ret;
    const char*  method;
    size_t       method_len;
    const char*  path;
    size_t       path_len;
    int          minor_version;
    phr_header   headers[kMaxHeaders];
    size_t       num_headers;
};

ParseResult parseRequest(const char* buf, size_t len) {
    ParseResult r{};
    r.num_headers = kMaxHeaders;
    r.ret = phr_parse_request(buf, len,
                              &r.method, &r.method_len,
                              &r.path,   &r.path_len,
                              &r.minor_version,
                              r.headers, &r.num_headers,
                              /*last_len=*/0);
    return r;
}

}  // namespace

TEST(PicoHttpParser, ParsesMinimalGetRequest) {
    const char buf[] = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    const auto r = parseRequest(buf, sizeof(buf) - 1);

    ASSERT_GT(r.ret, 0) << "parse should consume the full header block";
    EXPECT_EQ(static_cast<int>(sizeof(buf) - 1), r.ret);
    EXPECT_EQ(3u, r.method_len);
    EXPECT_EQ(0, std::strncmp(r.method, "GET", r.method_len));
    EXPECT_EQ(1u, r.path_len);
    EXPECT_EQ('/', r.path[0]);
    EXPECT_EQ(1, r.minor_version);
    EXPECT_EQ(1u, r.num_headers);
    EXPECT_EQ(0, std::strncmp(r.headers[0].name, "Host", r.headers[0].name_len));
    EXPECT_EQ(0, std::strncmp(r.headers[0].value, "example.com",
                              r.headers[0].value_len));
}

TEST(PicoHttpParser, ParsesPostWithMultipleHeaders) {
    const char buf[] =
        "POST /api/v1/echo HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 17\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    const auto r = parseRequest(buf, sizeof(buf) - 1);

    ASSERT_GT(r.ret, 0);
    EXPECT_EQ(4u, r.method_len);
    EXPECT_EQ(0, std::strncmp(r.method, "POST", r.method_len));
    EXPECT_EQ(12u, r.path_len);
    EXPECT_EQ(0, std::strncmp(r.path, "/api/v1/echo", r.path_len));
    EXPECT_EQ(1, r.minor_version);
    EXPECT_EQ(4u, r.num_headers);
}

TEST(PicoHttpParser, ReportsIncompleteOnPartialHeader) {
    // No terminating CRLF CRLF — the parser must signal "need more bytes".
    const char buf[] = "GET / HTTP/1.1\r\nHost: example.com\r\n";
    const auto r = parseRequest(buf, sizeof(buf) - 1);

    EXPECT_EQ(-2, r.ret) << "expected -2 (incomplete), got " << r.ret;
}

TEST(PicoHttpParser, RejectsMalformedRequestLine) {
    // Two embedded NULs are an invalid token character; phr_parse_request
    // must reject with -1.
    const char buf[] = "GE\0T / HTTP/1.1\r\n\r\n";
    const auto r = parseRequest(buf, sizeof(buf) - 1);

    EXPECT_EQ(-1, r.ret) << "expected -1 (parse error), got " << r.ret;
}
