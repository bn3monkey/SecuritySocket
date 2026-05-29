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

// ===========================================================================
// Stress suite — a realistic ~119-route REST table (users / auth / posts /
// products / orders / carts / files / projects / teams / admin / versioned /
// search / webhooks / static assets). Registering the whole table exercises
// the SegmentTrie-backed router under many shared prefixes and repeated pool
// growth (SegmentPool starts at capacity 32). The match assertions below
// cover the cases the router supports today (static + :param).
//
// Catch-all: the 5 '*' routes (/assets/*path, /static/*filepath,
// /docs/:version/*slug, /download/:bucket/*objectPath, /proxy/*target) are
// supported — a '*name' segment must be last, absorbs the rest of the path
// (interior '/' included), and is the lowest-priority child (static > param >
// wildcard). Asserted live in HttpRouterStress.CatchAllRoutesMatch below.
// ===========================================================================
namespace {

struct RouteDef { const char* method; const char* path; };

// The full route table (verbatim from the stress spec).
const RouteDef kStressRoutes[] = {
    {"GET","/"}, {"GET","/health"}, {"GET","/status"}, {"GET","/metrics"}, {"GET","/version"},

    {"GET","/api/users"}, {"POST","/api/users"},
    {"GET","/api/users/:userId"}, {"PUT","/api/users/:userId"},
    {"PATCH","/api/users/:userId"}, {"DELETE","/api/users/:userId"},

    {"GET","/api/users/:userId/profile"}, {"PATCH","/api/users/:userId/profile"},
    {"GET","/api/users/:userId/settings"}, {"PATCH","/api/users/:userId/settings"},
    {"POST","/api/users/:userId/avatar"}, {"DELETE","/api/users/:userId/avatar"},

    {"GET","/api/auth/session"}, {"POST","/api/auth/login"}, {"POST","/api/auth/logout"},
    {"POST","/api/auth/refresh"}, {"POST","/api/auth/password/reset"}, {"POST","/api/auth/password/change"},

    {"GET","/api/posts"}, {"POST","/api/posts"},
    {"GET","/api/posts/:postId"}, {"PUT","/api/posts/:postId"},
    {"PATCH","/api/posts/:postId"}, {"DELETE","/api/posts/:postId"},
    {"POST","/api/posts/:postId/publish"}, {"POST","/api/posts/:postId/unpublish"},

    {"GET","/api/posts/:postId/comments"}, {"POST","/api/posts/:postId/comments"},
    {"GET","/api/posts/:postId/comments/:commentId"},
    {"PATCH","/api/posts/:postId/comments/:commentId"},
    {"DELETE","/api/posts/:postId/comments/:commentId"},

    {"GET","/api/categories"}, {"POST","/api/categories"},
    {"GET","/api/categories/:categoryId"}, {"PATCH","/api/categories/:categoryId"},
    {"DELETE","/api/categories/:categoryId"},

    {"GET","/api/products"}, {"POST","/api/products"},
    {"GET","/api/products/:productId"}, {"PUT","/api/products/:productId"},
    {"PATCH","/api/products/:productId"}, {"DELETE","/api/products/:productId"},

    {"GET","/api/products/:productId/reviews"}, {"POST","/api/products/:productId/reviews"},
    {"GET","/api/products/:productId/reviews/:reviewId"},
    {"PATCH","/api/products/:productId/reviews/:reviewId"},
    {"DELETE","/api/products/:productId/reviews/:reviewId"},

    {"GET","/api/orders"}, {"POST","/api/orders"},
    {"GET","/api/orders/:orderId"}, {"PATCH","/api/orders/:orderId"},
    {"DELETE","/api/orders/:orderId"},
    {"POST","/api/orders/:orderId/cancel"}, {"POST","/api/orders/:orderId/pay"},
    {"POST","/api/orders/:orderId/refund"},

    {"GET","/api/orders/:orderId/items"}, {"POST","/api/orders/:orderId/items"},
    {"PATCH","/api/orders/:orderId/items/:itemId"},
    {"DELETE","/api/orders/:orderId/items/:itemId"},

    {"GET","/api/carts/:cartId"}, {"POST","/api/carts/:cartId/items"},
    {"PATCH","/api/carts/:cartId/items/:itemId"},
    {"DELETE","/api/carts/:cartId/items/:itemId"},
    {"POST","/api/carts/:cartId/checkout"},

    {"GET","/api/files"}, {"POST","/api/files"},
    {"GET","/api/files/:fileId"}, {"DELETE","/api/files/:fileId"},
    {"GET","/api/files/:fileId/download"}, {"POST","/api/files/:fileId/share"},

    {"GET","/api/projects"}, {"POST","/api/projects"},
    {"GET","/api/projects/:projectId"}, {"PATCH","/api/projects/:projectId"},
    {"DELETE","/api/projects/:projectId"},

    {"GET","/api/projects/:projectId/tasks"}, {"POST","/api/projects/:projectId/tasks"},
    {"GET","/api/projects/:projectId/tasks/:taskId"},
    {"PATCH","/api/projects/:projectId/tasks/:taskId"},
    {"DELETE","/api/projects/:projectId/tasks/:taskId"},

    {"GET","/api/teams"}, {"POST","/api/teams"},
    {"GET","/api/teams/:teamId"}, {"PATCH","/api/teams/:teamId"},
    {"DELETE","/api/teams/:teamId"},

    {"GET","/api/teams/:teamId/members"}, {"POST","/api/teams/:teamId/members"},
    {"PATCH","/api/teams/:teamId/members/:memberId"},
    {"DELETE","/api/teams/:teamId/members/:memberId"},

    {"GET","/api/admin/users"},
    {"GET","/api/admin/users/:userId/audit-logs"},
    {"POST","/api/admin/users/:userId/ban"}, {"POST","/api/admin/users/:userId/unban"},

    {"GET","/api/v1/users"}, {"GET","/api/v1/users/:userId"},
    {"GET","/api/v2/users"}, {"GET","/api/v2/users/:userId"},

    {"GET","/api/search"}, {"GET","/api/search/users"},
    {"GET","/api/search/posts"}, {"GET","/api/search/products"},

    {"POST","/api/webhooks/github"}, {"POST","/api/webhooks/stripe"}, {"POST","/api/webhooks/slack"},

    // Catch-all routes — registered, but matching is PENDING (see note above).
    {"GET","/assets/*path"}, {"GET","/static/*filepath"},
    {"GET","/docs/:version/*slug"}, {"GET","/download/:bucket/*objectPath"},
    {"GET","/proxy/*target"},
};

HttpRouter::HandlerFn noopHandler() {
    return [](ClientConnection&, HttpRequest&, HttpResponse&) {};
}

void registerOne(HttpRouterImpl& r, const char* method, const char* path) {
    if      (std::strcmp(method, "GET")    == 0) r.get    (path, noopHandler());
    else if (std::strcmp(method, "POST")   == 0) r.post   (path, noopHandler());
    else if (std::strcmp(method, "PUT")    == 0) r.put    (path, noopHandler());
    else if (std::strcmp(method, "PATCH")  == 0) r.patch  (path, noopHandler());
    else if (std::strcmp(method, "DELETE") == 0) r.del    (path, noopHandler());
    else if (std::strcmp(method, "HEAD")   == 0) r.head   (path, noopHandler());
    else if (std::strcmp(method, "OPTIONS")== 0) r.options(path, noopHandler());
}

void registerAll(HttpRouterImpl& r) {
    for (const RouteDef& rd : kStressRoutes) registerOne(r, rd.method, rd.path);
}

// Convenience: does (method, path) resolve to a registered handler?
bool hits(const HttpRouterImpl& r, const char* method, const char* path) {
    return r.match(method, path).fn != nullptr;
}

}  // namespace

TEST(HttpRouterStress, RegistersFullRouteTableAndMatchesSupportedRoutes) {
    HttpRouterImpl r;
    registerAll(r);   // ~119 routes → repeated SegmentPool growth past cap 32

    // ── Static endpoints resolve ──
    EXPECT_TRUE(hits(r, "GET", "/"));
    EXPECT_TRUE(hits(r, "GET", "/health"));
    EXPECT_TRUE(hits(r, "GET", "/api/users"));
    EXPECT_TRUE(hits(r, "GET", "/api/auth/session"));
    EXPECT_TRUE(hits(r, "GET", "/api/v2/users"));
    EXPECT_TRUE(hits(r, "GET", "/api/search/products"));

    // ── Method discrimination on the same path ──
    EXPECT_TRUE(hits(r, "GET",  "/api/users"));
    EXPECT_TRUE(hits(r, "POST", "/api/users"));

    // ── Single :param, value extracted ──
    {
        auto m = r.match("GET", "/api/users/42");
        ASSERT_NE(nullptr, m.fn);
        ASSERT_EQ(1u, m.param_count);
        EXPECT_EQ(0, std::strncmp("userId", m.params[0].name,  m.params[0].name_len));
        EXPECT_EQ(0, std::strncmp("42",     m.params[0].value, m.params[0].value_len));
    }
    EXPECT_TRUE(hits(r, "PUT",    "/api/users/42"));
    EXPECT_TRUE(hits(r, "DELETE", "/api/users/42"));

    // ── Two nested :params, bound in declaration order ──
    {
        auto m = r.match("GET", "/api/posts/7/comments/99");
        ASSERT_NE(nullptr, m.fn);
        ASSERT_EQ(2u, m.param_count);
        EXPECT_EQ(0, std::strncmp("postId",    m.params[0].name,  m.params[0].name_len));
        EXPECT_EQ(0, std::strncmp("7",         m.params[0].value, m.params[0].value_len));
        EXPECT_EQ(0, std::strncmp("commentId", m.params[1].name,  m.params[1].name_len));
        EXPECT_EQ(0, std::strncmp("99",        m.params[1].value, m.params[1].value_len));
    }
    {
        auto m = r.match("PATCH", "/api/orders/1001/items/55");
        ASSERT_NE(nullptr, m.fn);
        ASSERT_EQ(2u, m.param_count);
        EXPECT_EQ(0, std::strncmp("orderId", m.params[0].name,  m.params[0].name_len));
        EXPECT_EQ(0, std::strncmp("1001",    m.params[0].value, m.params[0].value_len));
        EXPECT_EQ(0, std::strncmp("itemId",  m.params[1].name,  m.params[1].name_len));
        EXPECT_EQ(0, std::strncmp("55",      m.params[1].value, m.params[1].value_len));
    }

    // ── 405: path exists but not for this method (login is POST-only) ──
    {
        auto m = r.match("GET", "/api/auth/login");
        EXPECT_EQ(nullptr, m.fn);
        EXPECT_TRUE(m.method_not_allowed);
    }
    // ── 404: nothing registered, no fallback ──
    {
        auto m = r.match("GET", "/api/does-not-exist");
        EXPECT_EQ(nullptr, m.fn);
        EXPECT_FALSE(m.method_not_allowed);
    }
}

TEST(HttpRouterStress, CatchAllRoutesMatch) {
    HttpRouterImpl r;
    registerAll(r);

    // /assets/*path — absorbs the remainder, interior '/' included.
    {
        auto m = r.match("GET", "/assets/css/app.css");
        ASSERT_NE(nullptr, m.fn);
        ASSERT_EQ(1u, m.param_count);
        EXPECT_EQ(0, std::strncmp("path",        m.params[0].name,  m.params[0].name_len));
        EXPECT_EQ(0, std::strncmp("css/app.css", m.params[0].value, m.params[0].value_len));
        EXPECT_EQ(11u, m.params[0].value_len);
    }
    // /static/*filepath
    {
        auto m = r.match("GET", "/static/js/bundle.min.js");
        ASSERT_NE(nullptr, m.fn);
        ASSERT_EQ(1u, m.param_count);
        EXPECT_EQ(0, std::strncmp("filepath",         m.params[0].name,  m.params[0].name_len));
        EXPECT_EQ(0, std::strncmp("js/bundle.min.js", m.params[0].value, m.params[0].value_len));
    }
    // /docs/:version/*slug — a param followed by a catch-all.
    {
        auto m = r.match("GET", "/docs/v2/guides/intro");
        ASSERT_NE(nullptr, m.fn);
        ASSERT_EQ(2u, m.param_count);
        EXPECT_EQ(0, std::strncmp("version",      m.params[0].name,  m.params[0].name_len));
        EXPECT_EQ(0, std::strncmp("v2",           m.params[0].value, m.params[0].value_len));
        EXPECT_EQ(0, std::strncmp("slug",         m.params[1].name,  m.params[1].name_len));
        EXPECT_EQ(0, std::strncmp("guides/intro", m.params[1].value, m.params[1].value_len));
    }
    // /download/:bucket/*objectPath
    {
        auto m = r.match("GET", "/download/bucket1/path/to/file.zip");
        ASSERT_NE(nullptr, m.fn);
        ASSERT_EQ(2u, m.param_count);
        EXPECT_EQ(0, std::strncmp("bucket",            m.params[0].name,  m.params[0].name_len));
        EXPECT_EQ(0, std::strncmp("bucket1",           m.params[0].value, m.params[0].value_len));
        EXPECT_EQ(0, std::strncmp("objectPath",        m.params[1].name,  m.params[1].name_len));
        EXPECT_EQ(0, std::strncmp("path/to/file.zip",  m.params[1].value, m.params[1].value_len));
    }
    // Catch-all requires at least one trailing segment.
    EXPECT_EQ(nullptr, r.match("GET", "/assets").fn);
    EXPECT_EQ(nullptr, r.match("GET", "/assets/").fn);
}

// ===========================================================================
// 100 stress test cases (REQUEST → EXPECTED). These document the intended
// behaviour against the route table above. Cases #93-100 require catch-all
// support and are marked PENDING. Turn these into EXPECT_* once the matrix is
// reviewed (and catch-all is implemented for the PENDING block).
//
//   #   METHOD   REQUEST PATH                                  EXPECT             PARAMS (name=value …)
// ---- static collection / singleton GETs --------------------------------------
//   1  GET       /                                             200 root           —
//   2  GET       /health                                       200                —
//   3  GET       /status                                       200                —
//   4  GET       /metrics                                      200                —
//   5  GET       /version                                      200                —
//   6  GET       /api/users                                    200                —
//   7  GET       /api/posts                                    200                —
//   8  GET       /api/categories                               200                —
//   9  GET       /api/products                                 200                —
//  10  GET       /api/orders                                   200                —
//  11  GET       /api/files                                    200                —
//  12  GET       /api/projects                                 200                —
//  13  GET       /api/teams                                    200                —
//  14  GET       /api/admin/users                              200                —
//  15  GET       /api/auth/session                             200                —
//  16  GET       /api/v1/users                                 200                —
//  17  GET       /api/v2/users                                 200                —
//  18  GET       /api/search                                   200                —
//  19  GET       /api/search/users                             200                —
//  20  GET       /api/search/posts                             200                —
// ---- POST collections ---------------------------------------------------------
//  21  POST      /api/users                                    200                —
//  22  POST      /api/posts                                    200                —
//  23  POST      /api/categories                               200                —
//  24  POST      /api/products                                 200                —
//  25  POST      /api/orders                                   200                —
//  26  POST      /api/files                                    200                —
//  27  POST      /api/projects                                 200                —
//  28  POST      /api/teams                                    200                —
//  29  POST      /api/auth/login                               200                —
//  30  POST      /api/auth/logout                              200                —
//  31  POST      /api/auth/refresh                             200                —
//  32  POST      /api/auth/password/reset                      200                —
//  33  POST      /api/auth/password/change                     200                —
//  34  POST      /api/webhooks/github                          200                —
//  35  POST      /api/webhooks/stripe                          200                —
//  36  POST      /api/webhooks/slack                           200                —
// ---- single :param + value ----------------------------------------------------
//  37  GET       /api/users/42                                 200                userId=42
//  38  PUT       /api/users/42                                 200                userId=42
//  39  PATCH     /api/users/42                                 200                userId=42
//  40  DELETE    /api/users/42                                 200                userId=42
//  41  GET       /api/posts/7                                  200                postId=7
//  42  PUT       /api/posts/7                                  200                postId=7
//  43  PATCH     /api/posts/7                                  200                postId=7
//  44  DELETE    /api/posts/7                                  200                postId=7
//  45  GET       /api/categories/electronics                   200                categoryId=electronics
//  46  PATCH     /api/categories/electronics                   200                categoryId=electronics
//  47  DELETE    /api/categories/electronics                   200                categoryId=electronics
//  48  GET       /api/products/99                              200                productId=99
//  49  PUT       /api/products/99                              200                productId=99
//  50  GET       /api/orders/1001                              200                orderId=1001
//  51  PATCH     /api/orders/1001                              200                orderId=1001
//  52  DELETE    /api/orders/1001                              200                orderId=1001
//  53  GET       /api/files/f-7                                200                fileId=f-7
//  54  DELETE    /api/files/f-7                                200                fileId=f-7
//  55  GET       /api/projects/p1                              200                projectId=p1
//  56  GET       /api/teams/t1                                 200                teamId=t1
//  57  GET       /api/carts/c1                                 200                cartId=c1
//  58  GET       /api/v1/users/42                              200                userId=42
//  59  GET       /api/v2/users/99                              200                userId=99
// ---- sub-resources under one :param ------------------------------------------
//  60  GET       /api/users/42/profile                         200                userId=42
//  61  PATCH     /api/users/42/settings                        200                userId=42
//  62  POST      /api/users/42/avatar                          200                userId=42
//  63  DELETE    /api/users/42/avatar                          200                userId=42
//  64  POST      /api/posts/7/publish                          200                postId=7
//  65  POST      /api/posts/7/unpublish                        200                postId=7
//  66  GET       /api/posts/7/comments                         200                postId=7
//  67  POST      /api/posts/7/comments                         200                postId=7
//  68  GET       /api/products/99/reviews                      200                productId=99
//  69  POST      /api/orders/1001/cancel                       200                orderId=1001
//  70  POST      /api/orders/1001/pay                          200                orderId=1001
//  71  POST      /api/orders/1001/refund                       200                orderId=1001
//  72  GET       /api/orders/1001/items                        200                orderId=1001
//  73  POST      /api/carts/c1/items                           200                cartId=c1
//  74  POST      /api/carts/c1/checkout                        200                cartId=c1
//  75  GET       /api/files/f-7/download                       200                fileId=f-7
//  76  POST      /api/files/f-7/share                          200                fileId=f-7
//  77  GET       /api/projects/p1/tasks                        200                projectId=p1
//  78  GET       /api/teams/t1/members                         200                teamId=t1
//  79  GET       /api/admin/users/42/audit-logs                200                userId=42
//  80  POST      /api/admin/users/42/ban                       200                userId=42
// ---- two nested :params -------------------------------------------------------
//  81  GET       /api/posts/7/comments/99                      200                postId=7, commentId=99
//  82  PATCH     /api/posts/7/comments/99                      200                postId=7, commentId=99
//  83  DELETE    /api/posts/7/comments/99                      200                postId=7, commentId=99
//  84  GET       /api/products/99/reviews/5                    200                productId=99, reviewId=5
//  85  PATCH     /api/orders/1001/items/55                     200                orderId=1001, itemId=55
//  86  DELETE    /api/carts/c1/items/9                         200                cartId=c1, itemId=9
//  87  GET       /api/projects/p1/tasks/t9                     200                projectId=p1, taskId=t9
//  88  PATCH     /api/teams/t1/members/m3                      200                teamId=t1, memberId=m3
// ---- 405 (path matches, wrong method) ----------------------------------------
//  89  GET       /api/auth/login                               405                — (login is POST-only)
//  90  PUT       /api/posts                                    405                — (GET/POST only)
//  91  POST      /api/users/42                                 405                — (no POST on /users/:id)
//  92  GET       /api/posts/7/publish                          405                — (POST-only)
// ---- 404 / malformed ----------------------------------------------------------
//  93  GET       /api/unknown                                  404                —
//  94  GET       /api/users/42/unknown                         404                —
//  95  GET       /api//users                                   404                — (double slash)
//  96  GET       (empty)                                       404                — (no leading slash)
// ---- catch-all ('*' implemented; asserted in CatchAllRoutesMatch) -------------
//  97  GET       /assets/css/app.css                           200                path=css/app.css
//  98  GET       /static/js/bundle.min.js                      200                filepath=js/bundle.min.js
//  99  GET       /docs/v2/guides/intro                         200                version=v2, slug=guides/intro
// 100  GET       /download/bucket1/path/to/file.zip            200                bucket=bucket1, objectPath=path/to/file.zip
//       (also: GET /proxy/http/example.com/api → target=http/example.com/api;
//              GET /assets and GET /assets/ → 404, catch-all requires ≥1 segment)
// ===========================================================================
