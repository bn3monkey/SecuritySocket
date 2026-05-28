// HttpRequestImpl + HeaderIndex unit tests.
//
// Pure in-process — constructs an HttpRequestImpl directly with synthesised
// header/body slices. The view contract that Phase 6's dispatch flow will
// exercise is the one under test here: method/path/body accessors, query
// auto-decode, header case-insensitive lookup, and the setPathParam binding
// that the router invokes after a match.

#include <gtest/gtest.h>

#include "../src/implementation/http/HttpRequest.hpp"
#include "../src/implementation/http/Url.hpp"

#include <string>

using Bn3Monkey::HeaderEntry;
using Bn3Monkey::HeaderIndex;
using Bn3Monkey::HttpRequest;
using Bn3Monkey::HttpRequestImpl;

namespace {

// HttpRequestImpl borrows the header storage by reference and assumes the
// header values are already NUL-terminated. Phase 6 will guarantee that by
// writing '\0' over the CR after each value in the request buffer. Here we
// just hand it pre-prepared C strings.
HeaderIndex makeHeaders(std::initializer_list<std::pair<const char*, const char*>> kvs) {
    HeaderIndex idx;
    for (const auto& kv : kvs) {
        idx.add(kv.first, kv.second);
    }
    return idx;
}

}  // namespace

TEST(HeaderIndex, AddAndFindIsCaseInsensitive) {
    HeaderIndex idx;
    ASSERT_TRUE(idx.add("Content-Type", "application/json"));
    ASSERT_TRUE(idx.add("X-Trace-Id",   "abc123"));
    EXPECT_EQ(2u, idx.count());

    // RFC 7230 §3.2 — field name compare is case-insensitive on the server
    // side. Production headers come in arbitrary case (e.g. "content-type"
    // from curl, "Content-Type" from browsers).
    EXPECT_STREQ("application/json", idx.find("Content-Type"));
    EXPECT_STREQ("application/json", idx.find("content-type"));
    EXPECT_STREQ("application/json", idx.find("CONTENT-TYPE"));
    EXPECT_STREQ("abc123",           idx.find("x-trace-id"));
    EXPECT_EQ(nullptr,               idx.find("missing"));
}

TEST(HeaderIndex, CapacityCapEnforced) {
    HeaderIndex idx;
    for (size_t i = 0; i < HeaderIndex::MAX_HEADERS; ++i) {
        ASSERT_TRUE(idx.add("X-Pad", "v"));
    }
    EXPECT_EQ(HeaderIndex::MAX_HEADERS, idx.count());
    EXPECT_FALSE(idx.add("overflow", "v"));
    EXPECT_EQ(HeaderIndex::MAX_HEADERS, idx.count());
}

TEST(HttpRequest, MethodAndPathAccessors) {
    HeaderIndex h;
    HttpRequestImpl req("GET", "/health", h, nullptr, 0);
    EXPECT_STREQ("GET",     req.method());
    EXPECT_STREQ("/health", req.path());
}

TEST(HttpRequest, BodyAccessorsReflectInput) {
    HeaderIndex h;
    const char body[] = "{\"x\":1}";
    HttpRequestImpl req("POST", "/api", h, body, sizeof(body) - 1);
    EXPECT_EQ(body,            req.body());
    EXPECT_EQ(sizeof(body) - 1, req.bodySize());
}

TEST(HttpRequest, HeaderLookupCaseInsensitive) {
    HeaderIndex h = makeHeaders({{"Content-Type", "text/plain"},
                                 {"Host",         "example.com"}});
    HttpRequestImpl req("GET", "/", h, nullptr, 0);
    EXPECT_STREQ("text/plain",  req.header("Content-Type"));
    EXPECT_STREQ("text/plain",  req.header("content-type"));
    EXPECT_STREQ("example.com", req.header("Host"));
    EXPECT_EQ(nullptr,          req.header("Authorization"));
}

TEST(HttpRequest, QueryStringStrippedFromPath) {
    HeaderIndex h;
    HttpRequestImpl req("GET", "/items?limit=10", h, nullptr, 0);
    EXPECT_STREQ("/items", req.path());
}

TEST(HttpRequest, QueryParamsParsedAndAutoDecoded) {
    HeaderIndex h;
    // "name=Alice%20Doe&filter=k%3Dv" — value side of 'filter' contains an
    // encoded '=', which must decode to "k=v" (not split into a nested pair).
    HttpRequestImpl req("GET",
                        "/search?name=Alice%20Doe&filter=k%3Dv&empty=",
                        h, nullptr, 0);
    EXPECT_STREQ("Alice Doe", req.query("name"));
    EXPECT_STREQ("k=v",       req.query("filter"));
    EXPECT_STREQ("",          req.query("empty"));
    EXPECT_EQ(nullptr,        req.query("missing"));
}

TEST(HttpRequest, QueryEncodingInsideKeyAlsoDecoded) {
    // Real-world: clients may percent-encode reserved chars in the key too.
    HeaderIndex h;
    HttpRequestImpl req("GET", "/q?my%20key=val", h, nullptr, 0);
    EXPECT_STREQ("val", req.query("my key"));
}

TEST(HttpRequest, MalformedQueryPairsSkipped) {
    // "=v" has an empty key — skipped (no name to query by).
    // "noequals" has no '=' — Go's net/url keeps it as key="noequals",
    // value="", so query("noequals") returns the empty string. The
    // valid pair beside them still parses.
    HeaderIndex h;
    HttpRequestImpl req("GET", "/q?=v&noequals&ok=1", h, nullptr, 0);
    EXPECT_STREQ("1",   req.query("ok"));
    EXPECT_EQ(nullptr,  req.query(""));
    EXPECT_STREQ("",    req.query("noequals"));
}

TEST(HttpRequest, SetPathParamRoundTrip) {
    HeaderIndex h;
    HttpRequestImpl req("GET", "/user/42/order/777", h, nullptr, 0);
    // Phase 6 dispatch does Url::decode then this:
    ASSERT_TRUE(req.setPathParam("uid", "42"));
    ASSERT_TRUE(req.setPathParam("oid", "777"));
    EXPECT_STREQ("42",  req.pathParam("uid"));
    EXPECT_STREQ("777", req.pathParam("oid"));
    EXPECT_EQ(nullptr,  req.pathParam("missing"));
}

TEST(HttpRequest, PathParamAutoDecodeChainFromUrl) {
    // Covers the milestone criterion "pathParam / query auto-decode 확인
    // (encoded URL → 원본 byte)". Caller (Phase 6 dispatch) percent-decodes
    // the raw URL segment before binding via setPathParam; the test mimics
    // that wire-up to verify the contract holds end-to-end.
    HeaderIndex h;
    HttpRequestImpl req("GET", "/user/Alice%20Doe", h, nullptr, 0);

    // Mimic the router→dispatch→setPathParam handoff. We don't reach into
    // HttpRouter here — we just hand decode-then-bind exactly as Phase 6
    // would. The Url::decode call is the unit covered by the Url tests; this
    // case proves the resulting value lands in the HttpRequest correctly.
    std::string decoded = Bn3Monkey::Url::decode("Alice%20Doe");
    ASSERT_TRUE(req.setPathParam("name", decoded.c_str()));
    EXPECT_STREQ("Alice Doe", req.pathParam("name"));
}

TEST(HttpRequest, SetPathParamRejectsOversize) {
    // 32-byte name cap, 128-byte value cap (header constants). Caller asks
    // for "no truncation" via the return value rather than silent loss.
    HeaderIndex h;
    HttpRequestImpl req("GET", "/", h, nullptr, 0);

    std::string long_name(HttpRequestImpl::PARAM_NAME_CAP, 'x');  // exactly cap, no NUL room
    EXPECT_FALSE(req.setPathParam(long_name.c_str(), "v"));

    std::string long_value(HttpRequestImpl::PARAM_VALUE_CAP, 'y');
    EXPECT_FALSE(req.setPathParam("k", long_value.c_str()));

    // Both rejections must NOT have inserted partial state — pathParam still
    // returns nullptr for those names.
    EXPECT_EQ(nullptr, req.pathParam(long_name.c_str()));
    EXPECT_EQ(nullptr, req.pathParam("k"));
}

TEST(HttpRequest, SetPathParamCapAtMaxCount) {
    HeaderIndex h;
    HttpRequestImpl req("GET", "/", h, nullptr, 0);
    for (size_t i = 0; i < HttpRequestImpl::MAX_PATH_PARAMS; ++i) {
        char name[8];
        std::snprintf(name, sizeof(name), "p%zu", i);
        ASSERT_TRUE(req.setPathParam(name, "v"));
    }
    EXPECT_FALSE(req.setPathParam("overflow", "v"));
}

TEST(HttpRequest, EmptyPathAccepted) {
    // Defensive: ctor handles nullptr/empty inputs without crashing. Phase 6
    // shouldn't actually hand it a null path, but the impl must not UB.
    HeaderIndex h;
    HttpRequestImpl req("GET", nullptr, h, nullptr, 0);
    EXPECT_STREQ("", req.path());
}
