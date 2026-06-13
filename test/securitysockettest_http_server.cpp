// SecuritySocket HTTP server regression tests (driven by libcurl).
//
// A real RequestServer is started with an HttpRequestHandler, then libcurl
// (curl_easy) drives it as a standard HTTP/1.1 client — proving the server
// interoperates with a third-party client, not just our own codec. Covers
// route matching, status/body, request body echo, path params, 404, and
// keep-alive connection reuse across many requests on one socket.
//
// Everything is gated by SECURITYSOCKET_TEST_USE_CURL (defined by the root
// CMakeLists.txt only when the build option of the same name is ON). With the
// option OFF this file compiles to an empty TU.

#if defined(SECURITYSOCKET_TEST_USE_CURL)

#include <gtest/gtest.h>

#include <curl/curl.h>

#include <SecuritySocket.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

// Resolving the running test executable's own directory (the multipart test
// stages its large-file scratch tree next to the binary).
#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#elif defined(__linux__)
#  include <unistd.h>
#endif

namespace {

// ── libcurl global init (once for the whole test binary) ──
class HttpServerCurlEnv : public ::testing::Environment {
public:
    void SetUp() override {
        ASSERT_EQ(CURLE_OK, curl_global_init(CURL_GLOBAL_DEFAULT));
    }
    void TearDown() override {
        curl_global_cleanup();
    }
};

::testing::Environment* const g_http_server_curl_env =
    ::testing::AddGlobalTestEnvironment(new HttpServerCurlEnv);

size_t discardWrite(char*, size_t size, size_t nmemb, void*) {
    return size * nmemb;
}

size_t appendToString(char* ptr, size_t size, size_t nmemb, void* userdata) {
    const size_t n = size * nmemb;
    static_cast<std::string*>(userdata)->append(ptr, n);
    return n;
}

constexpr uint16_t kHttpPort = 28771;

std::string baseUrl(const char* path) {
    return std::string("http://127.0.0.1:") + std::to_string(kHttpPort) + path;
}

// Echo/route handler exercised by the tests below.
class EchoHttpHandler : public Bn3Monkey::HttpRequestHandler {
public:
    void registerRoutes(Bn3Monkey::HttpRouter& router) override {
        router.get("/ping", [](const Bn3Monkey::ClientConnection&,
                               Bn3Monkey::HttpRequest&,
                               Bn3Monkey::HttpResponse& res) {
            res.status(200).body("pong", 4);
        });
        router.post("/echo", [](const Bn3Monkey::ClientConnection&,
                                Bn3Monkey::HttpRequest& req,
                                Bn3Monkey::HttpResponse& res) {
            res.status(200).body(req.body(), req.bodySize());
        });
        router.get("/user/:id", [](const Bn3Monkey::ClientConnection&,
                                   Bn3Monkey::HttpRequest& req,
                                   Bn3Monkey::HttpResponse& res) {
            const char* id = req.pathParam("id");
            res.status(200).body(id, id ? std::strlen(id) : 0);
        });
    }
};

// RAII: start a RequestServer with the given handler on kHttpPort, stop on dtor.
class ServerRunner {
public:
    explicit ServerRunner(Bn3Monkey::RequestHandler& handler)
        : _server(Bn3Monkey::NetworkConfiguration{
              "127.0.0.1", kHttpPort, false, 5, 1000, 1000, 100, 8192 }) {
        auto r = _server.open(&handler, 8);
        _ok = (r.code() == Bn3Monkey::NetworkResultCode::SUCCESS);
        // Give the listen loop a moment to start accepting.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ~ServerRunner() { _server.close(); }
    bool ok() const { return _ok; }
private:
    Bn3Monkey::RequestServer _server;
    bool _ok{ false };
};

// Perform one request on a (possibly reused) handle. Returns false on transport
// error; on success fills out_code / out_body.
bool perform(CURL* h, long& out_code, std::string& out_body) {
    out_body.clear();
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, &appendToString);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &out_body);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, 5000L);
    const CURLcode rc = curl_easy_perform(h);
    if (rc != CURLE_OK) return false;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &out_code);
    return true;
}

}  // namespace

// ── smoke (kept from Phase 1) ──
TEST(HttpServerSmoke, LibCurlVersionMeetsMinimum) {
    curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
    ASSERT_NE(nullptr, info);
    ASSERT_GE(info->version_num, 0x080B00) << "libcurl version: " << info->version;
}

TEST(HttpServerSmoke, LoopbackConnectFailsCleanly) {
    CURL* handle = curl_easy_init();
    ASSERT_NE(nullptr, handle);
    curl_easy_setopt(handle, CURLOPT_URL, "http://127.0.0.1:1/");
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 1000L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS,        2000L);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &discardWrite);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    const CURLcode rc = curl_easy_perform(handle);
    EXPECT_NE(CURLE_OK, rc) << "expected loopback:1 to fail, got: "
                            << curl_easy_strerror(rc);
    curl_easy_cleanup(handle);
}

// ── real server regression ──

TEST(HttpServer, GetRouteReturnsBody) {
    EchoHttpHandler handler;
    ServerRunner srv(handler);
    ASSERT_TRUE(srv.ok());

    CURL* h = curl_easy_init();
    ASSERT_NE(nullptr, h);
    curl_easy_setopt(h, CURLOPT_URL, baseUrl("/ping").c_str());

    long code = 0; std::string body;
    ASSERT_TRUE(perform(h, code, body)) << "GET /ping transport failed";
    EXPECT_EQ(200, code);
    EXPECT_EQ("pong", body);

    curl_easy_cleanup(h);
}

TEST(HttpServer, PostEchoesRequestBody) {
    EchoHttpHandler handler;
    ServerRunner srv(handler);
    ASSERT_TRUE(srv.ok());

    const std::string payload = "the quick brown fox jumps over the lazy dog";

    CURL* h = curl_easy_init();
    ASSERT_NE(nullptr, h);
    curl_easy_setopt(h, CURLOPT_URL, baseUrl("/echo").c_str());
    curl_easy_setopt(h, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));

    long code = 0; std::string body;
    ASSERT_TRUE(perform(h, code, body)) << "POST /echo transport failed";
    EXPECT_EQ(200, code);
    EXPECT_EQ(payload, body);

    curl_easy_cleanup(h);
}

TEST(HttpServer, PathParamIsDecodedAndBound) {
    EchoHttpHandler handler;
    ServerRunner srv(handler);
    ASSERT_TRUE(srv.ok());

    CURL* h = curl_easy_init();
    ASSERT_NE(nullptr, h);
    curl_easy_setopt(h, CURLOPT_URL, baseUrl("/user/42").c_str());

    long code = 0; std::string body;
    ASSERT_TRUE(perform(h, code, body)) << "GET /user/42 transport failed";
    EXPECT_EQ(200, code);
    EXPECT_EQ("42", body);

    curl_easy_cleanup(h);
}

TEST(HttpServer, UnknownRouteReturns404) {
    EchoHttpHandler handler;
    ServerRunner srv(handler);
    ASSERT_TRUE(srv.ok());

    CURL* h = curl_easy_init();
    ASSERT_NE(nullptr, h);
    curl_easy_setopt(h, CURLOPT_URL, baseUrl("/no/such/route").c_str());

    long code = 0; std::string body;
    ASSERT_TRUE(perform(h, code, body)) << "GET /missing transport failed";
    EXPECT_EQ(404, code);

    curl_easy_cleanup(h);
}

// Keep-alive: one handle reused for many requests reuses the TCP connection,
// exercising the server's WaitingForNextHttpRequest recycle path.
TEST(HttpServer, KeepAliveManyRequestsOnOneConnection) {
    EchoHttpHandler handler;
    ServerRunner srv(handler);
    ASSERT_TRUE(srv.ok());

    CURL* h = curl_easy_init();
    ASSERT_NE(nullptr, h);
    curl_easy_setopt(h, CURLOPT_URL, baseUrl("/ping").c_str());

    for (int i = 0; i < 120; ++i) {
        long code = 0; std::string body;
        ASSERT_TRUE(perform(h, code, body)) << "keep-alive request " << i << " failed";
        EXPECT_EQ(200, code) << "request " << i;
        EXPECT_EQ("pong", body) << "request " << i;
    }

    // All requests rode one connection (curl reuses it); confirm no reconnect.
    long reused = 0;
    curl_easy_getinfo(h, CURLINFO_NUM_CONNECTS, &reused);
    EXPECT_LE(reused, 1L) << "expected a single TCP connection for keep-alive";

    curl_easy_cleanup(h);
}

// ===========================================================================
// 100-case stress matrix, run end-to-end through a real RequestServer.
//
// The 100-case table documented in securitysockettest_http_router.cpp is a
// *routing* contract (router->match assertions, in-process). This test turns it
// into a live HTTP contract: a stub handler registers the full ~119-route REST
// table, each route echoing the path params the trie bound (so the test can
// assert param extraction over the wire), and libcurl drives every case,
// asserting status (200 / 404 / 405) and, for param/catch-all routes, the
// echoed "name=value,..." body.
//
// Case #96 ("(empty) / no leading slash") is a router-only malformed case that
// is not expressible from an HTTP client (a URL path always has a leading '/'),
// so it stays asserted by the in-process router test, not here.
// ===========================================================================
namespace {

constexpr uint16_t kStressPort = 28772;

std::string stressUrl(const char* path) {
    return std::string("http://127.0.0.1:") + std::to_string(kStressPort) + path;
}

struct StressRoute { const char* method; const char* pattern; };

// The full route table (verbatim from the routing stress spec).
const StressRoute kStressRoutes[] = {
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

    {"GET","/assets/*path"}, {"GET","/static/*filepath"},
    {"GET","/docs/:version/*slug"}, {"GET","/download/:bucket/*objectPath"},
    {"GET","/proxy/*target"},
};

// Extract the `:param` / `*catchall` names from a pattern, in declaration order.
std::vector<std::string> paramNames(const char* pattern) {
    std::vector<std::string> names;
    std::string seg;
    for (const char* c = pattern; ; ++c) {
        if (*c == '/' || *c == '\0') {
            if (!seg.empty() && (seg[0] == ':' || seg[0] == '*'))
                names.push_back(seg.substr(1));
            seg.clear();
            if (*c == '\0') break;
        } else {
            seg.push_back(*c);
        }
    }
    return names;
}

// 200 + body echoing the bound params as "name=value,name=value". A no-param
// route produces an empty body; "/" is special-cased to "root".
Bn3Monkey::HttpRouter::HandlerFn echoParamsHandler(std::vector<std::string> names) {
    return [names](Bn3Monkey::ClientConnection&,
                   Bn3Monkey::HttpRequest& req,
                   Bn3Monkey::HttpResponse& res) {
        // HttpResponse::body() BORROWS — the bytes must outlive the handler
        // return, since serialize() runs after it. A thread_local scratch buffer
        // (one per event/worker thread) gives the body stable storage that lives
        // past this call without racing other connections.
        thread_local std::string out;
        out.clear();
        for (size_t i = 0; i < names.size(); ++i) {
            if (i) out.push_back(',');
            const char* v = req.pathParam(names[i].c_str());
            out += names[i];
            out.push_back('=');
            out += (v ? v : "");
        }
        res.status(200).body(out.data(), out.size());
    };
}

void registerStressRoute(Bn3Monkey::HttpRouter& r, const char* method,
                         const char* pattern, Bn3Monkey::HttpRouter::HandlerFn fn) {
    if      (std::strcmp(method, "GET")    == 0) r.get   (pattern, std::move(fn));
    else if (std::strcmp(method, "POST")   == 0) r.post  (pattern, std::move(fn));
    else if (std::strcmp(method, "PUT")    == 0) r.put   (pattern, std::move(fn));
    else if (std::strcmp(method, "PATCH")  == 0) r.patch (pattern, std::move(fn));
    else if (std::strcmp(method, "DELETE") == 0) r.del   (pattern, std::move(fn));
}

class StressHttpHandler : public Bn3Monkey::HttpRequestHandler {
public:
    void registerRoutes(Bn3Monkey::HttpRouter& r) override {
        for (const StressRoute& rd : kStressRoutes) {
            if (std::strcmp(rd.pattern, "/") == 0) {
                registerStressRoute(r, rd.method, rd.pattern,
                    [](Bn3Monkey::ClientConnection&, Bn3Monkey::HttpRequest&,
                       Bn3Monkey::HttpResponse& res) { res.status(200).body("root", 4); });
            } else {
                registerStressRoute(r, rd.method, rd.pattern,
                                    echoParamsHandler(paramNames(rd.pattern)));
            }
        }
    }
};

struct StressCase {
    int          id;
    const char*  method;
    const char*  path;
    long         status;
    const char*  body;        // expected echoed body; checked only when check_body
    bool         check_body;
};

// The 100-case matrix as a live HTTP contract. body is asserted for param /
// catch-all routes; for plain 200/404/405 cases only the status is checked.
const StressCase kCases[] = {
    // ── static collection / singleton GETs ──
    { 1,"GET","/",                       200,"root",true},
    { 2,"GET","/health",                 200,"",false},
    { 3,"GET","/status",                 200,"",false},
    { 4,"GET","/metrics",                200,"",false},
    { 5,"GET","/version",                200,"",false},
    { 6,"GET","/api/users",              200,"",false},
    { 7,"GET","/api/posts",              200,"",false},
    { 8,"GET","/api/categories",         200,"",false},
    { 9,"GET","/api/products",           200,"",false},
    {10,"GET","/api/orders",             200,"",false},
    {11,"GET","/api/files",              200,"",false},
    {12,"GET","/api/projects",           200,"",false},
    {13,"GET","/api/teams",              200,"",false},
    {14,"GET","/api/admin/users",        200,"",false},
    {15,"GET","/api/auth/session",       200,"",false},
    {16,"GET","/api/v1/users",           200,"",false},
    {17,"GET","/api/v2/users",           200,"",false},
    {18,"GET","/api/search",             200,"",false},
    {19,"GET","/api/search/users",       200,"",false},
    {20,"GET","/api/search/posts",       200,"",false},
    // ── POST collections ──
    {21,"POST","/api/users",                   200,"",false},
    {22,"POST","/api/posts",                   200,"",false},
    {23,"POST","/api/categories",              200,"",false},
    {24,"POST","/api/products",                200,"",false},
    {25,"POST","/api/orders",                  200,"",false},
    {26,"POST","/api/files",                   200,"",false},
    {27,"POST","/api/projects",                200,"",false},
    {28,"POST","/api/teams",                   200,"",false},
    {29,"POST","/api/auth/login",              200,"",false},
    {30,"POST","/api/auth/logout",             200,"",false},
    {31,"POST","/api/auth/refresh",            200,"",false},
    {32,"POST","/api/auth/password/reset",     200,"",false},
    {33,"POST","/api/auth/password/change",    200,"",false},
    {34,"POST","/api/webhooks/github",         200,"",false},
    {35,"POST","/api/webhooks/stripe",         200,"",false},
    {36,"POST","/api/webhooks/slack",          200,"",false},
    // ── single :param + value ──
    {37,"GET",   "/api/users/42",              200,"userId=42",true},
    {38,"PUT",   "/api/users/42",              200,"userId=42",true},
    {39,"PATCH", "/api/users/42",              200,"userId=42",true},
    {40,"DELETE","/api/users/42",              200,"userId=42",true},
    {41,"GET",   "/api/posts/7",               200,"postId=7",true},
    {42,"PUT",   "/api/posts/7",               200,"postId=7",true},
    {43,"PATCH", "/api/posts/7",               200,"postId=7",true},
    {44,"DELETE","/api/posts/7",               200,"postId=7",true},
    {45,"GET",   "/api/categories/electronics",200,"categoryId=electronics",true},
    {46,"PATCH", "/api/categories/electronics",200,"categoryId=electronics",true},
    {47,"DELETE","/api/categories/electronics",200,"categoryId=electronics",true},
    {48,"GET",   "/api/products/99",           200,"productId=99",true},
    {49,"PUT",   "/api/products/99",           200,"productId=99",true},
    {50,"GET",   "/api/orders/1001",           200,"orderId=1001",true},
    {51,"PATCH", "/api/orders/1001",           200,"orderId=1001",true},
    {52,"DELETE","/api/orders/1001",           200,"orderId=1001",true},
    {53,"GET",   "/api/files/f-7",             200,"fileId=f-7",true},
    {54,"DELETE","/api/files/f-7",             200,"fileId=f-7",true},
    {55,"GET",   "/api/projects/p1",           200,"projectId=p1",true},
    {56,"GET",   "/api/teams/t1",              200,"teamId=t1",true},
    {57,"GET",   "/api/carts/c1",              200,"cartId=c1",true},
    {58,"GET",   "/api/v1/users/42",           200,"userId=42",true},
    {59,"GET",   "/api/v2/users/99",           200,"userId=99",true},
    // ── sub-resources under one :param ──
    {60,"GET",   "/api/users/42/profile",      200,"userId=42",true},
    {61,"PATCH", "/api/users/42/settings",     200,"userId=42",true},
    {62,"POST",  "/api/users/42/avatar",       200,"userId=42",true},
    {63,"DELETE","/api/users/42/avatar",       200,"userId=42",true},
    {64,"POST",  "/api/posts/7/publish",       200,"postId=7",true},
    {65,"POST",  "/api/posts/7/unpublish",     200,"postId=7",true},
    {66,"GET",   "/api/posts/7/comments",      200,"postId=7",true},
    {67,"POST",  "/api/posts/7/comments",      200,"postId=7",true},
    {68,"GET",   "/api/products/99/reviews",   200,"productId=99",true},
    {69,"POST",  "/api/orders/1001/cancel",    200,"orderId=1001",true},
    {70,"POST",  "/api/orders/1001/pay",       200,"orderId=1001",true},
    {71,"POST",  "/api/orders/1001/refund",    200,"orderId=1001",true},
    {72,"GET",   "/api/orders/1001/items",     200,"orderId=1001",true},
    {73,"POST",  "/api/carts/c1/items",        200,"cartId=c1",true},
    {74,"POST",  "/api/carts/c1/checkout",     200,"cartId=c1",true},
    {75,"GET",   "/api/files/f-7/download",    200,"fileId=f-7",true},
    {76,"POST",  "/api/files/f-7/share",       200,"fileId=f-7",true},
    {77,"GET",   "/api/projects/p1/tasks",     200,"projectId=p1",true},
    {78,"GET",   "/api/teams/t1/members",      200,"teamId=t1",true},
    {79,"GET",   "/api/admin/users/42/audit-logs",200,"userId=42",true},
    {80,"POST",  "/api/admin/users/42/ban",    200,"userId=42",true},
    // ── two nested :params ──
    {81,"GET",   "/api/posts/7/comments/99",   200,"postId=7,commentId=99",true},
    {82,"PATCH", "/api/posts/7/comments/99",   200,"postId=7,commentId=99",true},
    {83,"DELETE","/api/posts/7/comments/99",   200,"postId=7,commentId=99",true},
    {84,"GET",   "/api/products/99/reviews/5", 200,"productId=99,reviewId=5",true},
    {85,"PATCH", "/api/orders/1001/items/55",  200,"orderId=1001,itemId=55",true},
    {86,"DELETE","/api/carts/c1/items/9",      200,"cartId=c1,itemId=9",true},
    {87,"GET",   "/api/projects/p1/tasks/t9",  200,"projectId=p1,taskId=t9",true},
    {88,"PATCH", "/api/teams/t1/members/m3",   200,"teamId=t1,memberId=m3",true},
    // ── 405 (path matches, wrong method) ──
    {89,"GET",  "/api/auth/login",             405,"",false},
    {90,"PUT",  "/api/posts",                  405,"",false},
    {91,"POST", "/api/users/42",               405,"",false},
    {92,"GET",  "/api/posts/7/publish",        405,"",false},
    // ── 404 / malformed ──
    {93,"GET",  "/api/unknown",                404,"",false},
    {94,"GET",  "/api/users/42/unknown",       404,"",false},
    {95,"GET",  "/api//users",                 404,"",false},   // double slash
    // ── catch-all ──
    {97, "GET","/assets/css/app.css",                200,"path=css/app.css",true},
    {98, "GET","/static/js/bundle.min.js",           200,"filepath=js/bundle.min.js",true},
    {99, "GET","/docs/v2/guides/intro",              200,"version=v2,slug=guides/intro",true},
    {100,"GET","/download/bucket1/path/to/file.zip", 200,"bucket=bucket1,objectPath=path/to/file.zip",true},
    // ── catch-all extras (from the spec footnote) ──
    {101,"GET","/proxy/http/example.com/api",        200,"target=http/example.com/api",true},
    {102,"GET","/assets",                            404,"",false},  // catch-all needs >=1 segment
    {103,"GET","/assets/",                           404,"",false},
};

// Perform one stress case on a fresh handle (404/405 close the connection, so a
// per-case handle keeps the cases independent). PATH_AS_IS keeps "//" literal.
bool performCase(const StressCase& c, long& out_code, std::string& out_body) {
    CURL* h = curl_easy_init();
    if (!h) return false;
    curl_easy_setopt(h, CURLOPT_URL, stressUrl(c.path).c_str());
    curl_easy_setopt(h, CURLOPT_PATH_AS_IS, 1L);
    if (std::strcmp(c.method, "GET") == 0) {
        curl_easy_setopt(h, CURLOPT_HTTPGET, 1L);
    } else {
        curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, c.method);
    }
    const bool ok = perform(h, out_code, out_body);
    curl_easy_cleanup(h);
    return ok;
}

}  // namespace

TEST(HttpServerStress, FullMatrixOverTheWire) {
    StressHttpHandler handler;
    Bn3Monkey::RequestServer server(Bn3Monkey::NetworkConfiguration{
        "127.0.0.1", kStressPort, false, 5, 1000, 1000, 100, 8192 });
    ASSERT_EQ(Bn3Monkey::NetworkResultCode::SUCCESS, server.open(&handler, 8).code());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    for (const StressCase& c : kCases) {
        long code = 0;
        std::string body;
        ASSERT_TRUE(performCase(c, code, body))
            << "case #" << c.id << " " << c.method << " " << c.path << " transport failed";
        EXPECT_EQ(c.status, code)
            << "case #" << c.id << " " << c.method << " " << c.path;
        if (c.check_body) {
            EXPECT_EQ(std::string(c.body), body)
                << "case #" << c.id << " " << c.method << " " << c.path << " body";
        }
    }

    server.close();
}

// ===========================================================================
// Multipart/form-data large-file upload, end-to-end over a real RequestServer.
//
// The server (this test's main thread) registers a POST /upload route that
// parses a multipart/form-data body by hand — the framework hands the handler
// the raw body via req.body()/bodySize(), it does NOT parse multipart — and
// writes the extracted file into <scratch>/out/. A separate client thread
// drives 10 uploads of 10 MB files each via libcurl's curl_mime API (a real
// multipart/form-data request, not our own codec). The main thread waits for
// the client thread to finish, then byte-compares every original against its
// out/ copy. Both the source tree and the prior run's tree are wiped first,
// and the whole scratch tree is removed at the end.
//
// This relies on NetworkConfiguration::max_http_request_body_size (default
// 64 MB) being large enough for a 10 MB multipart body; a 1 MB cap would 413.
// ===========================================================================
namespace {

namespace fs = std::filesystem;

constexpr uint16_t kMultipartPort = 28773;
constexpr int      kMultipartFileCount = 10;
constexpr size_t   kMultipartFileSize  = 10 * 1024 * 1024;   // 10 MB each

// Directory of the running test binary; the scratch tree is created here per
// the test spec ("테스트 실행 파일이 있는 디렉토리"). Falls back to the CWD if
// the platform path can't be resolved.
fs::path executableDir() {
#if defined(_WIN32)
    char buf[MAX_PATH];
    const DWORD n = ::GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return fs::path(std::string(buf, n)).parent_path();
#elif defined(__linux__)
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf));
    if (n > 0) return fs::path(std::string(buf, static_cast<size_t>(n))).parent_path();
#endif
    return fs::current_path();
}

// Deterministic-but-not-trivial file content: a per-file seeded PRNG so each
// file differs and the bytes aren't all-equal (which would hide off-by-one
// boundary-trim bugs in the multipart parser).
void writeRandomFile(const fs::path& path, size_t size, uint32_t seed) {
    std::mt19937 rng(seed);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    std::vector<char> chunk(64 * 1024);
    size_t written = 0;
    while (written < size) {
        const size_t n = std::min(chunk.size(), size - written);
        for (size_t i = 0; i < n; ++i) chunk[i] = static_cast<char>(rng() & 0xFF);
        f.write(chunk.data(), static_cast<std::streamsize>(n));
        written += n;
    }
}

// Byte-for-byte file comparison (streamed, so 10 MB doesn't all sit in RAM).
bool filesEqual(const fs::path& a, const fs::path& b) {
    std::error_code ec1, ec2;
    if (fs::file_size(a, ec1) != fs::file_size(b, ec2) || ec1 || ec2) return false;
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    if (!fa || !fb) return false;
    std::vector<char> ba(64 * 1024), bb(64 * 1024);
    while (fa && fb) {
        fa.read(ba.data(), static_cast<std::streamsize>(ba.size()));
        fb.read(bb.data(), static_cast<std::streamsize>(bb.size()));
        const std::streamsize na = fa.gcount(), nb = fb.gcount();
        if (na != nb) return false;
        if (na == 0) break;
        if (std::memcmp(ba.data(), bb.data(), static_cast<size_t>(na)) != 0) return false;
    }
    return true;
}

// Binary-safe substring search (the multipart body is raw bytes; std::strstr
// would stop at the first embedded NUL).
const char* findBytes(const char* hay, size_t hlen, const char* needle, size_t nlen) {
    if (nlen == 0 || hlen < nlen) return nullptr;
    for (size_t i = 0; i + nlen <= hlen; ++i) {
        if (std::memcmp(hay + i, needle, nlen) == 0) return hay + i;
    }
    return nullptr;
}

// Minimal single-part multipart/form-data parser: pull the boundary from the
// Content-Type header, locate the first part, read its filename, and slice the
// file bytes out of [end-of-part-headers .. CRLF + closing boundary).
bool parseMultipartFile(Bn3Monkey::HttpRequest& req,
                        std::string& out_filename,
                        std::vector<char>& out_data) {
    const char* ct = req.header("Content-Type");
    if (!ct) return false;
    const char* bp = std::strstr(ct, "boundary=");
    if (!bp) return false;
    std::string boundary = bp + std::strlen("boundary=");
    if (!boundary.empty() && boundary.front() == '"') {       // RFC allows quoting
        boundary.erase(0, 1);
        const auto q = boundary.find('"');
        if (q != std::string::npos) boundary.erase(q);
    }
    const std::string delim = "--" + boundary;

    const char*  body = static_cast<const char*>(req.body());
    const size_t blen = req.bodySize();

    const char* part = findBytes(body, blen, delim.data(), delim.size());
    if (!part) return false;
    part += delim.size();

    const size_t after_delim = blen - static_cast<size_t>(part - body);
    const char* hdr_end = findBytes(part, after_delim, "\r\n\r\n", 4);
    if (!hdr_end) return false;

    const std::string headers(part, static_cast<size_t>(hdr_end - part));
    const auto fpos = headers.find("filename=\"");
    if (fpos != std::string::npos) {
        const auto start = fpos + std::strlen("filename=\"");
        const auto fend = headers.find('"', start);
        if (fend != std::string::npos) out_filename = headers.substr(start, fend - start);
    }

    const char*  data_start = hdr_end + 4;
    const size_t data_rem   = blen - static_cast<size_t>(data_start - body);
    const std::string closing = "\r\n" + delim;               // CRLF precedes the next/closing boundary
    const char* data_end = findBytes(data_start, data_rem, closing.data(), closing.size());
    if (!data_end) return false;

    out_data.assign(data_start, data_end);
    return true;
}

// POST /upload: parse the multipart body, write the file under out/. Registered
// SLOW so the 10 MB parse + disk write runs on the worker thread, off the event
// loop. Reports counts so the test can assert all 10 landed.
class MultipartUploadHandler : public Bn3Monkey::HttpRequestHandler {
public:
    explicit MultipartUploadHandler(fs::path out_dir) : _out_dir(std::move(out_dir)) {}

    void registerRoutes(Bn3Monkey::HttpRouter& router) override {
        router.post("/upload", [this](Bn3Monkey::ClientConnection&,
                                      Bn3Monkey::HttpRequest& req,
                                      Bn3Monkey::HttpResponse& res) {
            std::string       filename;
            std::vector<char> data;
            if (!parseMultipartFile(req, filename, data) || filename.empty()) {
                res.status(400).body("bad multipart", 13);
                return;
            }
            const fs::path dest = _out_dir / fs::path(filename).filename();
            std::ofstream f(dest, std::ios::binary | std::ios::trunc);
            f.write(data.data(), static_cast<std::streamsize>(data.size()));
            f.close();
            _saved.fetch_add(1, std::memory_order_relaxed);
            res.status(200).body("ok", 2);
        }, Bn3Monkey::RequestProcessingMode::SLOW);
    }

    int saved() const { return _saved.load(std::memory_order_relaxed); }

private:
    fs::path         _out_dir;
    std::atomic<int> _saved{ 0 };
};

std::string multipartUrl(const char* path) {
    return std::string("http://127.0.0.1:") + std::to_string(kMultipartPort) + path;
}

}  // namespace

TEST(HttpServerMultipart, LargeFileUploadRoundTrip) {
#if defined(__ANDROID__)
    GTEST_SKIP() << "Multipart large-file upload test is not run on Android.";
#else
    // 2/8. Wipe any leftover scratch tree from a prior run, then (re)create it
    //      next to the test binary with a fresh out/ sink.
    const fs::path scratch = executableDir() / "multipart_large_files";
    const fs::path out_dir = scratch / "out";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    ASSERT_TRUE(fs::create_directories(out_dir, ec)) << "create scratch dir: " << ec.message();

    // 3. Stage 10 x 10 MB source files in the scratch dir.
    std::vector<fs::path> sources;
    for (int i = 0; i < kMultipartFileCount; ++i) {
        const fs::path src = scratch / ("file_" + std::to_string(i) + ".bin");
        writeRandomFile(src, kMultipartFileSize, /*seed*/ 0xC0FFEEu + static_cast<uint32_t>(i));
        ASSERT_EQ(kMultipartFileSize, fs::file_size(src)) << "staged " << src.string();
        sources.push_back(src);
    }

    // Server lives on this (main) thread; default 64 MB body cap admits 10 MB.
    MultipartUploadHandler handler(out_dir);
    Bn3Monkey::RequestServer server(Bn3Monkey::NetworkConfiguration{
        "127.0.0.1", kMultipartPort, false, 5, 5000, 5000, 100, 65536 });
    ASSERT_EQ(Bn3Monkey::NetworkResultCode::SUCCESS, server.open(&handler, 8).code());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 4. Client thread: upload each file as real multipart/form-data via curl_mime.
    std::atomic<int> uploaded{ 0 };
    std::thread client([&] {
        for (const fs::path& src : sources) {
            CURL* h = curl_easy_init();
            if (!h) { ADD_FAILURE() << "curl_easy_init"; return; }
            curl_easy_setopt(h, CURLOPT_URL, multipartUrl("/upload").c_str());

            curl_mime* mime = curl_mime_init(h);
            curl_mimepart* part = curl_mime_addpart(mime);
            curl_mime_name(part, "file");
            // Sets the part filename to the basename and a known size, so curl
            // emits Content-Length (not chunked) — which the server requires.
            curl_mime_filedata(part, src.string().c_str());
            curl_easy_setopt(h, CURLOPT_MIMEPOST, mime);

            long code = 0; std::string resp;
            const bool ok = perform(h, code, resp);
            EXPECT_TRUE(ok) << "upload transport failed for " << src.filename().string();
            EXPECT_EQ(200, code) << "upload " << src.filename().string() << " body=" << resp;
            if (ok && code == 200) uploaded.fetch_add(1, std::memory_order_relaxed);

            curl_mime_free(mime);
            curl_easy_cleanup(h);
        }
    });

    // 6. Server thread waits until the client thread is done.
    client.join();

    EXPECT_EQ(kMultipartFileCount, uploaded.load());
    EXPECT_EQ(kMultipartFileCount, handler.saved());

    // 7. Every original must match its out/ copy byte-for-byte.
    for (const fs::path& src : sources) {
        const fs::path dst = out_dir / src.filename();
        ASSERT_TRUE(fs::exists(dst)) << "missing uploaded copy: " << dst.string();
        EXPECT_TRUE(filesEqual(src, dst))
            << "content mismatch for " << src.filename().string();
    }

    server.close();

    // 8. Remove the scratch tree.
    fs::remove_all(scratch, ec);
#endif  // __ANDROID__
}

#endif  // SECURITYSOCKET_TEST_USE_CURL
