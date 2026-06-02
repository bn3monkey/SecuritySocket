// SecuritySocket HTTP client regression tests (driven by our own HttpClient).
//
// Mirror of securitysockettest_http_server.cpp, but the third-party libcurl
// client is replaced with the Phase 7 Bn3Monkey::HttpClient. A real
// RequestServer is started with an HttpRequestHandler and HttpClient drives it
// end-to-end: route matching, status/body, request body echo, path params,
// 404/405, keep-alive reuse, and the full 100-case routing matrix over the wire.
//
// Unlike the libcurl file this is NOT gated by SECURITYSOCKET_TEST_USE_CURL —
// it exercises our own client, so it always compiles and runs.

#include <gtest/gtest.h>

#include <SecuritySocket.hpp>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace Bn3Monkey;

constexpr uint16_t kHttpPort = 28851;

NetworkConfiguration clientConfig(uint16_t port) {
    return NetworkConfiguration{ "127.0.0.1", port, false, 5, 2000, 2000, 50, 8192 };
}

// Echo/route handler exercised by the basic tests below.
class EchoHttpHandler : public HttpRequestHandler {
public:
    void registerRoutes(HttpRouter& router) override {
        router.get("/ping", [](ClientConnection&, HttpRequest&, HttpResponse& res) {
            res.status(200).body("pong", 4);
        });
        router.post("/echo", [](ClientConnection&, HttpRequest& req, HttpResponse& res) {
            res.status(200).body(req.body(), req.bodySize());
        });
        router.get("/user/:id", [](ClientConnection&, HttpRequest& req, HttpResponse& res) {
            const char* id = req.pathParam("id");
            res.status(200).body(id, id ? std::strlen(id) : 0);
        });
    }
};

// RAII: start a RequestServer with the given handler, stop on dtor.
class ServerRunner {
public:
    ServerRunner(RequestHandler& handler, uint16_t port)
        : _server(NetworkConfiguration{ "127.0.0.1", port, false, 5, 1000, 1000, 100, 8192 }) {
        auto r = _server.open(&handler, 8);
        _ok = (r.code() == NetworkResultCode::SUCCESS);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ~ServerRunner() { _server.close(); }
    bool ok() const { return _ok; }
private:
    RequestServer _server;
    bool _ok{ false };
};

std::string bodyOf(const HttpClientResponse& r) {
    if (!r.body() || r.bodySize() == 0) return std::string();
    return std::string(static_cast<const char*>(r.body()), r.bodySize());
}

// Winsock init/teardown per test (WSAStartup is refcounted, so nesting is safe).
class HttpClientTest : public ::testing::Test {
protected:
    void SetUp() override    { initializeSecuritySocket(); }
    void TearDown() override  { releaseSecuritySocket(); }
};

}  // namespace

// ── basic regression ──

TEST_F(HttpClientTest, GetRouteReturnsBody) {
    EchoHttpHandler handler;
    ServerRunner srv(handler, kHttpPort);
    ASSERT_TRUE(srv.ok());

    HttpClient client{ clientConfig(kHttpPort) };
    auto resp = client.get("/ping");
    ASSERT_EQ(NetworkResultCode::SUCCESS, resp.resultCode());
    EXPECT_EQ(200, resp.status());
    EXPECT_EQ("pong", bodyOf(resp));
}

TEST_F(HttpClientTest, PostEchoesRequestBody) {
    EchoHttpHandler handler;
    ServerRunner srv(handler, kHttpPort);
    ASSERT_TRUE(srv.ok());

    const std::string payload = "the quick brown fox jumps over the lazy dog";

    HttpClient client{ clientConfig(kHttpPort) };
    auto resp = client.post("/echo", payload.data(), payload.size());
    ASSERT_EQ(NetworkResultCode::SUCCESS, resp.resultCode());
    EXPECT_EQ(200, resp.status());
    EXPECT_EQ(payload, bodyOf(resp));
}

TEST_F(HttpClientTest, PathParamIsBound) {
    EchoHttpHandler handler;
    ServerRunner srv(handler, kHttpPort);
    ASSERT_TRUE(srv.ok());

    HttpClient client{ clientConfig(kHttpPort) };
    auto resp = client.get("/user/42");
    ASSERT_EQ(NetworkResultCode::SUCCESS, resp.resultCode());
    EXPECT_EQ(200, resp.status());
    EXPECT_EQ("42", bodyOf(resp));
}

TEST_F(HttpClientTest, UnknownRouteReturns404) {
    EchoHttpHandler handler;
    ServerRunner srv(handler, kHttpPort);
    ASSERT_TRUE(srv.ok());

    HttpClient client{ clientConfig(kHttpPort) };
    auto resp = client.get("/no/such/route");
    ASSERT_EQ(NetworkResultCode::SUCCESS, resp.resultCode());
    EXPECT_EQ(404, resp.status());
}

// Keep-alive: many requests on one HttpClient reuse the TCP connection
// (the server's WaitingForNextHttpRequest recycle path).
TEST_F(HttpClientTest, KeepAliveManyRequestsOnOneClient) {
    EchoHttpHandler handler;
    ServerRunner srv(handler, kHttpPort);
    ASSERT_TRUE(srv.ok());

    HttpClient client{ clientConfig(kHttpPort) };
    for (int i = 0; i < 120; ++i) {
        auto resp = client.get("/ping");
        ASSERT_EQ(NetworkResultCode::SUCCESS, resp.resultCode()) << "request " << i;
        EXPECT_EQ(200, resp.status()) << "request " << i;
        EXPECT_EQ("pong", bodyOf(resp)) << "request " << i;
    }
}

// ===========================================================================
// 100-case routing matrix, run end-to-end through a real RequestServer with
// HttpClient as the driver (the libcurl-driven twin lives in
// securitysockettest_http_server.cpp, gated by SECURITYSOCKET_TEST_USE_CURL).
// ===========================================================================
namespace {

constexpr uint16_t kStressPort = 28852;

struct StressRoute { const char* method; const char* pattern; };

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

HttpRouter::HandlerFn echoParamsHandler(std::vector<std::string> names) {
    return [names](ClientConnection&, HttpRequest& req, HttpResponse& res) {
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

void registerStressRoute(HttpRouter& r, const char* method,
                         const char* pattern, HttpRouter::HandlerFn fn) {
    if      (std::strcmp(method, "GET")    == 0) r.get   (pattern, std::move(fn));
    else if (std::strcmp(method, "POST")   == 0) r.post  (pattern, std::move(fn));
    else if (std::strcmp(method, "PUT")    == 0) r.put   (pattern, std::move(fn));
    else if (std::strcmp(method, "PATCH")  == 0) r.patch (pattern, std::move(fn));
    else if (std::strcmp(method, "DELETE") == 0) r.del   (pattern, std::move(fn));
}

class StressHttpHandler : public HttpRequestHandler {
public:
    void registerRoutes(HttpRouter& r) override {
        for (const StressRoute& rd : kStressRoutes) {
            if (std::strcmp(rd.pattern, "/") == 0) {
                registerStressRoute(r, rd.method, rd.pattern,
                    [](ClientConnection&, HttpRequest&, HttpResponse& res) {
                        res.status(200).body("root", 4); });
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
    int          status;
    const char*  body;
    bool         check_body;
};

const StressCase kCases[] = {
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
    {81,"GET",   "/api/posts/7/comments/99",   200,"postId=7,commentId=99",true},
    {82,"PATCH", "/api/posts/7/comments/99",   200,"postId=7,commentId=99",true},
    {83,"DELETE","/api/posts/7/comments/99",   200,"postId=7,commentId=99",true},
    {84,"GET",   "/api/products/99/reviews/5", 200,"productId=99,reviewId=5",true},
    {85,"PATCH", "/api/orders/1001/items/55",  200,"orderId=1001,itemId=55",true},
    {86,"DELETE","/api/carts/c1/items/9",      200,"cartId=c1,itemId=9",true},
    {87,"GET",   "/api/projects/p1/tasks/t9",  200,"projectId=p1,taskId=t9",true},
    {88,"PATCH", "/api/teams/t1/members/m3",   200,"teamId=t1,memberId=m3",true},
    {89,"GET",  "/api/auth/login",             405,"",false},
    {90,"PUT",  "/api/posts",                  405,"",false},
    {91,"POST", "/api/users/42",               405,"",false},
    {92,"GET",  "/api/posts/7/publish",        405,"",false},
    {93,"GET",  "/api/unknown",                404,"",false},
    {94,"GET",  "/api/users/42/unknown",       404,"",false},
    {95,"GET",  "/api//users",                 404,"",false},
    {97, "GET","/assets/css/app.css",                200,"path=css/app.css",true},
    {98, "GET","/static/js/bundle.min.js",           200,"filepath=js/bundle.min.js",true},
    {99, "GET","/docs/v2/guides/intro",              200,"version=v2,slug=guides/intro",true},
    {100,"GET","/download/bucket1/path/to/file.zip", 200,"bucket=bucket1,objectPath=path/to/file.zip",true},
    {101,"GET","/proxy/http/example.com/api",        200,"target=http/example.com/api",true},
    {102,"GET","/assets",                            404,"",false},
    {103,"GET","/assets/",                           404,"",false},
};

}  // namespace

TEST_F(HttpClientTest, FullRoutingMatrixOverTheWire) {
    StressHttpHandler handler;
    ServerRunner srv(handler, kStressPort);
    ASSERT_TRUE(srv.ok());

    // A fresh client per case keeps the cases independent (404/405 close the
    // connection server-side; our client would reconnect anyway, but per-case
    // isolation mirrors the libcurl twin exactly).
    for (const StressCase& c : kCases) {
        HttpClient client{ clientConfig(kStressPort) };
        HttpClientRequest req;
        req.method(c.method).path("%s", c.path);
        auto resp = client.request(req);

        ASSERT_EQ(NetworkResultCode::SUCCESS, resp.resultCode())
            << "case #" << c.id << " " << c.method << " " << c.path << " transport";
        EXPECT_EQ(c.status, resp.status())
            << "case #" << c.id << " " << c.method << " " << c.path;
        if (c.check_body) {
            EXPECT_EQ(std::string(c.body), bodyOf(resp))
                << "case #" << c.id << " " << c.method << " " << c.path << " body";
        }
    }
}
