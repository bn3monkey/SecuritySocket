// HttpRouter trie unit tests.
//
// Pure in-process — no socket, no server, no libcurl. Builds an
// HttpRouterImpl, registers some routes, and exercises the match()
// surface that Phase 6's server dispatch will rely on.

#include <gtest/gtest.h>

#include "../src/implementation/http/HttpRouter.hpp"

#include <string>

using Bn3Monkey::HttpRouter;
using Bn3Monkey::HttpRouterImpl;
using Bn3Monkey::RequestProcessingMode;
using Bn3Monkey::ClientConnection;
using Bn3Monkey::HttpRequest;
using Bn3Monkey::HttpResponse;

namespace {

// Sentinel handler — match() returns a pointer to the stored HandlerFn but
// the function never runs during routing tests. We mark each route with a
// tagged lambda so the test can compare pointers indirectly: we identify a
// route by which handler-id it was registered with via captured int.
HttpRouter::HandlerFn makeTagged(int* tag_out, int tag) {
    return [tag_out, tag](ClientConnection&, HttpRequest&, HttpResponse&) {
        *tag_out = tag;
    };
}

// Helper: fire the matched handler against dummy conn/req/res. We don't
// supply real instances — the lambdas in these tests ignore their args —
// so we cast nullptr through reinterpret_cast to satisfy the signature.
void invoke(const HttpRouter::HandlerFn& fn) {
    ClientConnection* c = nullptr;
    HttpRequest*      q = nullptr;
    HttpResponse*     r = nullptr;
    fn(*c, *q, *r);
}

}  // namespace

TEST(HttpRouter, StaticPathMatchesExactly) {
    HttpRouterImpl router;
    int tag = 0;
    router.get("/health", makeTagged(&tag, 1), RequestProcessingMode::FAST);

    auto m = router.match("GET", "/health");
    ASSERT_NE(nullptr, m.fn);
    invoke(*m.fn);
    EXPECT_EQ(1, tag);
    EXPECT_EQ(RequestProcessingMode::FAST, m.mode);
    EXPECT_EQ(0u, m.param_count);
    EXPECT_FALSE(m.fallback_used);
    EXPECT_FALSE(m.method_not_allowed);
}

TEST(HttpRouter, MethodDiscrimination) {
    HttpRouterImpl router;
    int get_tag = 0, post_tag = 0;
    router.get ("/items", makeTagged(&get_tag,  10), RequestProcessingMode::FAST);
    router.post("/items", makeTagged(&post_tag, 20), RequestProcessingMode::FAST);

    auto g = router.match("GET",  "/items");
    auto p = router.match("POST", "/items");
    ASSERT_NE(nullptr, g.fn);
    ASSERT_NE(nullptr, p.fn);
    invoke(*g.fn);
    invoke(*p.fn);
    EXPECT_EQ(10, get_tag);
    EXPECT_EQ(20, post_tag);
}

TEST(HttpRouter, ParamSegmentMatchesAndExtractsValue) {
    HttpRouterImpl router;
    int tag = 0;
    router.get("/user/:id", makeTagged(&tag, 1), RequestProcessingMode::FAST);

    auto m = router.match("GET", "/user/42");
    ASSERT_NE(nullptr, m.fn);
    ASSERT_EQ(1u, m.param_count);
    EXPECT_EQ(2u, m.params[0].name_len);
    EXPECT_EQ(0, std::strncmp("id", m.params[0].name, 2));
    EXPECT_EQ(2u, m.params[0].value_len);
    EXPECT_EQ(0, std::strncmp("42", m.params[0].value, 2));
}

TEST(HttpRouter, StaticSegmentBeatsParamSegment) {
    // Per milestone §2-5 충돌 규칙: "/user/me" wins over "/user/:id".
    HttpRouterImpl router;
    int static_tag = 0, param_tag = 0;
    router.get("/user/:id", makeTagged(&param_tag,  111), RequestProcessingMode::FAST);
    router.get("/user/me",  makeTagged(&static_tag, 222), RequestProcessingMode::FAST);

    {
        auto m = router.match("GET", "/user/me");
        ASSERT_NE(nullptr, m.fn);
        invoke(*m.fn);
        EXPECT_EQ(222, static_tag);
        EXPECT_EQ(0u, m.param_count);
    }
    {
        auto m = router.match("GET", "/user/99");
        ASSERT_NE(nullptr, m.fn);
        invoke(*m.fn);
        EXPECT_EQ(111, param_tag);
        EXPECT_EQ(1u, m.param_count);
    }
}

TEST(HttpRouter, MultiplePathParamsBoundInOrder) {
    HttpRouterImpl router;
    int tag = 0;
    router.get("/u/:uid/order/:oid", makeTagged(&tag, 1), RequestProcessingMode::FAST);

    auto m = router.match("GET", "/u/alice/order/777");
    ASSERT_NE(nullptr, m.fn);
    ASSERT_EQ(2u, m.param_count);
    // First param — name "uid", value "alice"
    EXPECT_EQ(0, std::strncmp("uid",   m.params[0].name,  m.params[0].name_len));
    EXPECT_EQ(0, std::strncmp("alice", m.params[0].value, m.params[0].value_len));
    // Second param — name "oid", value "777"
    EXPECT_EQ(0, std::strncmp("oid", m.params[1].name,  m.params[1].name_len));
    EXPECT_EQ(0, std::strncmp("777", m.params[1].value, m.params[1].value_len));
}

TEST(HttpRouter, NoMatchAndNoFallbackReturnsNullFn) {
    HttpRouterImpl router;
    int tag = 0;
    router.get("/known", makeTagged(&tag, 1), RequestProcessingMode::FAST);

    auto m = router.match("GET", "/unknown");
    EXPECT_EQ(nullptr, m.fn);
    EXPECT_FALSE(m.fallback_used);
    EXPECT_FALSE(m.method_not_allowed);
}

TEST(HttpRouter, FallbackInvokedOnPathMiss) {
    HttpRouterImpl router;
    int route_tag = 0, fallback_tag = 0;
    router.get("/known",  makeTagged(&route_tag,    1), RequestProcessingMode::FAST);
    router.fallback        (makeTagged(&fallback_tag, 99), RequestProcessingMode::FAST);

    auto m = router.match("GET", "/unknown");
    ASSERT_NE(nullptr, m.fn);
    EXPECT_TRUE(m.fallback_used);
    invoke(*m.fn);
    EXPECT_EQ(99, fallback_tag);
    EXPECT_EQ(0, route_tag);
}

TEST(HttpRouter, MethodNotAllowedWhenPathMatchesButMethodDoesnt) {
    // /items is GET-only. A POST to it must surface as method_not_allowed,
    // NOT as a 404 fallback — Phase 6 turns this into a 405 response.
    HttpRouterImpl router;
    int get_tag = 0, fallback_tag = 0;
    router.get("/items", makeTagged(&get_tag, 1), RequestProcessingMode::FAST);
    router.fallback     (makeTagged(&fallback_tag, 99), RequestProcessingMode::FAST);

    auto m = router.match("POST", "/items");
    EXPECT_EQ(nullptr, m.fn);
    EXPECT_TRUE(m.method_not_allowed);
    EXPECT_FALSE(m.fallback_used);
}

TEST(HttpRouter, RootPathHandled) {
    HttpRouterImpl router;
    int tag = 0;
    router.get("/", makeTagged(&tag, 1), RequestProcessingMode::FAST);

    auto m = router.match("GET", "/");
    ASSERT_NE(nullptr, m.fn);
    invoke(*m.fn);
    EXPECT_EQ(1, tag);
}

TEST(HttpRouter, ModeIsStoredPerRoute) {
    // Each route's FAST/SLOW choice is sticky — registration-time decision.
    HttpRouterImpl router;
    int fast_tag = 0, slow_tag = 0;
    router.get("/fast", makeTagged(&fast_tag, 1), RequestProcessingMode::FAST);
    router.get("/slow", makeTagged(&slow_tag, 2), RequestProcessingMode::SLOW);

    EXPECT_EQ(RequestProcessingMode::FAST, router.match("GET", "/fast").mode);
    EXPECT_EQ(RequestProcessingMode::SLOW, router.match("GET", "/slow").mode);
}

TEST(HttpRouter, ModeDefaultsToFastWhenOmitted) {
    // The public HttpRouter interface declares mode = FAST as the default.
    // Calling get() through the base-class pointer must produce FAST without
    // the test having to spell it. base.get() takes the default branch in
    // the interface declaration; we then read back via the impl's match().
    HttpRouterImpl router;
    HttpRouter& base = router;
    int tag = 0;
    base.get("/x", makeTagged(&tag, 1));  // no mode arg

    EXPECT_EQ(RequestProcessingMode::FAST, router.match("GET", "/x").mode);
}

TEST(HttpRouter, AllSevenMethodsRouteIndependently) {
    HttpRouterImpl router;
    int g = 0, p = 0, pu = 0, d = 0, pa = 0, h = 0, o = 0;
    router.get    ("/r", makeTagged(&g,  1));
    router.post   ("/r", makeTagged(&p,  2));
    router.put    ("/r", makeTagged(&pu, 3));
    router.del    ("/r", makeTagged(&d,  4));
    router.patch  ("/r", makeTagged(&pa, 5));
    router.head   ("/r", makeTagged(&h,  6));
    router.options("/r", makeTagged(&o,  7));

    invoke(*router.match("GET",     "/r").fn); EXPECT_EQ(1, g);
    invoke(*router.match("POST",    "/r").fn); EXPECT_EQ(2, p);
    invoke(*router.match("PUT",     "/r").fn); EXPECT_EQ(3, pu);
    invoke(*router.match("DELETE",  "/r").fn); EXPECT_EQ(4, d);
    invoke(*router.match("PATCH",   "/r").fn); EXPECT_EQ(5, pa);
    invoke(*router.match("HEAD",    "/r").fn); EXPECT_EQ(6, h);
    invoke(*router.match("OPTIONS", "/r").fn); EXPECT_EQ(7, o);
}

TEST(HttpRouter, UnknownMethodGoesToFallback) {
    HttpRouterImpl router;
    int route_tag = 0, fallback_tag = 0;
    router.get("/r", makeTagged(&route_tag, 1));
    router.fallback   (makeTagged(&fallback_tag, 99));

    // FOOBAR isn't a recognised HTTP verb — router can't tell which slot
    // to look in. Behaves like a path miss → fallback fires.
    auto m = router.match("FOOBAR", "/r");
    ASSERT_NE(nullptr, m.fn);
    EXPECT_TRUE(m.fallback_used);
    invoke(*m.fn);
    EXPECT_EQ(99, fallback_tag);
    EXPECT_EQ(0, route_tag);
}

TEST(HttpRouter, MalformedPathRejected) {
    // Empty path / no leading slash → no match (even with fallback empty).
    HttpRouterImpl router;
    EXPECT_EQ(nullptr, router.match("GET", "").fn);
    EXPECT_EQ(nullptr, router.match("GET", "foo").fn);
}

TEST(HttpRouter, DoubleSlashDoesNotMatchSingleSlashRoute) {
    // "/foo//bar" must NOT match "/foo/bar". Defensive: silently collapsing
    // would let two distinct URLs resolve to the same handler — sometimes a
    // routing bug, sometimes an attack vector.
    HttpRouterImpl router;
    int tag = 0;
    router.get("/foo/bar", makeTagged(&tag, 1));

    EXPECT_EQ(nullptr, router.match("GET", "/foo//bar").fn);
}

TEST(HttpRouter, ParseMethodKnownAndUnknown) {
    EXPECT_EQ(HttpRouterImpl::Method::GET,     HttpRouterImpl::parseMethod("GET",     3));
    EXPECT_EQ(HttpRouterImpl::Method::POST,    HttpRouterImpl::parseMethod("POST",    4));
    EXPECT_EQ(HttpRouterImpl::Method::PUT,     HttpRouterImpl::parseMethod("PUT",     3));
    EXPECT_EQ(HttpRouterImpl::Method::DEL,     HttpRouterImpl::parseMethod("DELETE",  6));
    EXPECT_EQ(HttpRouterImpl::Method::PATCH,   HttpRouterImpl::parseMethod("PATCH",   5));
    EXPECT_EQ(HttpRouterImpl::Method::HEAD,    HttpRouterImpl::parseMethod("HEAD",    4));
    EXPECT_EQ(HttpRouterImpl::Method::OPTIONS, HttpRouterImpl::parseMethod("OPTIONS", 7));
    // Lowercase / mixed-case → unknown. RFC 7230 §3.1.1 says methods are
    // case-sensitive and the upper-case tokens are the conventional set;
    // we follow that strictly to avoid surprising routing collisions.
    EXPECT_EQ(HttpRouterImpl::Method::COUNT,   HttpRouterImpl::parseMethod("get",     3));
    EXPECT_EQ(HttpRouterImpl::Method::COUNT,   HttpRouterImpl::parseMethod("FOOBAR",  6));
    EXPECT_EQ(HttpRouterImpl::Method::COUNT,   HttpRouterImpl::parseMethod(nullptr,   0));
}
