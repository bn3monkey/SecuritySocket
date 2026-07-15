// HttpRouter::registerStatic — static file serving.
//
// Builds a small web root on disk (index.html, app.css, assets/app.js) plus a
// secret file OUTSIDE that root, starts a RequestServer whose handler calls
// registerStatic(), and drives it with the project's own HttpClient. Plain HTTP
// (no TLS) — the file-serving logic is orthogonal to the transport, and the
// TLS path already has its own coverage in securitysockettest_https_server.cpp.

#include <gtest/gtest.h>

#include <SecuritySocket.hpp>

#include <cerrno>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif

using namespace Bn3Monkey;

namespace
{
    constexpr uint16_t kStaticPort = 28971;

    bool makeDir(const std::string& p)
    {
#if defined(_WIN32)
        return ::_mkdir(p.c_str()) == 0 || errno == EEXIST;
#else
        return ::mkdir(p.c_str(), 0755) == 0 || errno == EEXIST;
#endif
    }

    void writeFile(const std::string& path, const std::string& content)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    std::string bodyOf(const HttpClientResponse& res)
    {
        return std::string(static_cast<const char*>(res.body()), res.bodySize());
    }

    // A web root laid out once and reused by every test. The secret lives one
    // level ABOVE the root, so any request that reads it is a traversal escape.
    struct WebRoot
    {
        std::string base;
        std::string root;
        const std::string secret = "TOP-SECRET-DO-NOT-SERVE";

        WebRoot()
        {
            base = std::string(::testing::TempDir()) + "ss_static";
            root = base + "/wwwroot";
            makeDir(base);
            makeDir(root);
            makeDir(root + "/assets");

            writeFile(root + "/index.html",     "<!doctype html><h1>INDEX</h1>");
            writeFile(root + "/app.css",        "body{color:red}");
            writeFile(root + "/assets/app.js",  "console.log('hi')");
            writeFile(base + "/secret.txt",     secret);

            // A dotfile that lives INSIDE the root. It must still never be served
            // — ".env" / ".git" leakage is a classic breach even without traversal.
            writeFile(root + "/.env",           "API_KEY=" + secret);
        }
    };

    NetworkConfiguration staticConfig()
    {
        return NetworkConfiguration{ "127.0.0.1", kStaticPort, false, 5, 2000, 2000, 100, 8192 };
    }

    // Handler that mounts the web root. Also registers a real API route, so the
    // tests can prove an exact route still wins over the static catch-all.
    class StaticHandler : public HttpRequestHandler
    {
    public:
        StaticHandler(std::string root, bool spa) : _root(std::move(root)), _spa(spa) {}

        void registerRoutes(HttpRouter& router) override
        {
            router.get("/api/ping", [](ClientConnection&, HttpRequest&, HttpResponse& res) {
                res.status(200).body("pong", 4);
            });
            router.registerStatic(_root.c_str(), _spa);
        }

    private:
        std::string _root;
        bool        _spa;
    };

    // RAII server on kStaticPort.
    class Runner
    {
    public:
        explicit Runner(RequestHandler& handler) : _server(staticConfig())
        {
            _ok = (_server.open(&handler, 4).code() == NetworkResultCode::SUCCESS);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        ~Runner() { _server.close(); }
        bool ok() const { return _ok; }

    private:
        RequestServer _server;
        bool          _ok{ false };
    };
}

TEST(HttpStatic, RootServesIndexHtml)
{
    initializeSecuritySocket();
    WebRoot web;
    StaticHandler handler{ web.root, false };
    Runner server{ handler };
    ASSERT_TRUE(server.ok());

    HttpClient client{ staticConfig() };
    auto res = client.get("/");

    ASSERT_EQ(NetworkResultCode::SUCCESS, res.resultCode());
    EXPECT_EQ(200, res.status());
    EXPECT_NE(std::string::npos, bodyOf(res).find("INDEX"));
    EXPECT_STREQ("text/html; charset=utf-8", res.header("Content-Type"));

    releaseSecuritySocket();
}

TEST(HttpStatic, ServesCssWithCorrectMime)
{
    initializeSecuritySocket();
    WebRoot web;
    StaticHandler handler{ web.root, false };
    Runner server{ handler };
    ASSERT_TRUE(server.ok());

    HttpClient client{ staticConfig() };
    auto res = client.get("/app.css");

    ASSERT_EQ(NetworkResultCode::SUCCESS, res.resultCode());
    EXPECT_EQ(200, res.status());
    EXPECT_EQ("body{color:red}", bodyOf(res));
    EXPECT_STREQ("text/css; charset=utf-8", res.header("Content-Type"));

    releaseSecuritySocket();
}

TEST(HttpStatic, ServesNestedAssetWithJsMime)
{
    initializeSecuritySocket();
    WebRoot web;
    StaticHandler handler{ web.root, false };
    Runner server{ handler };
    ASSERT_TRUE(server.ok());

    HttpClient client{ staticConfig() };
    auto res = client.get("/assets/app.js");   // interior '/' absorbed by the catch-all

    ASSERT_EQ(NetworkResultCode::SUCCESS, res.resultCode());
    EXPECT_EQ(200, res.status());
    EXPECT_EQ("console.log('hi')", bodyOf(res));
    EXPECT_STREQ("text/javascript; charset=utf-8", res.header("Content-Type"));

    releaseSecuritySocket();
}

TEST(HttpStatic, ExactApiRouteWinsOverStaticCatchAll)
{
    initializeSecuritySocket();
    WebRoot web;
    StaticHandler handler{ web.root, false };
    Runner server{ handler };
    ASSERT_TRUE(server.ok());

    HttpClient client{ staticConfig() };
    auto res = client.get("/api/ping");

    ASSERT_EQ(NetworkResultCode::SUCCESS, res.resultCode());
    EXPECT_EQ(200, res.status());
    EXPECT_EQ("pong", bodyOf(res));

    releaseSecuritySocket();
}

TEST(HttpStatic, UnknownFileReturns404WithoutSpa)
{
    initializeSecuritySocket();
    WebRoot web;
    StaticHandler handler{ web.root, false };
    Runner server{ handler };
    ASSERT_TRUE(server.ok());

    HttpClient client{ staticConfig() };
    auto res = client.get("/does/not/exist.png");

    ASSERT_EQ(NetworkResultCode::SUCCESS, res.resultCode());
    EXPECT_EQ(404, res.status());

    releaseSecuritySocket();
}

// The security test. Two encodings of the same escape — a literal ".." and a
// percent-encoded "%2e%2e" (which the server decodes to "..") — must both be
// refused, and the secret one directory up must never appear in a response.
TEST(HttpStatic, PathTraversalIsBlocked)
{
    initializeSecuritySocket();
    WebRoot web;
    StaticHandler handler{ web.root, false };
    Runner server{ handler };
    ASSERT_TRUE(server.ok());

    HttpClient client{ staticConfig() };

    for (const char* attack : { "/../secret.txt",
                                "/%2e%2e/secret.txt",
                                "/assets/../../secret.txt" })
    {
        auto res = client.get(attack);
        ASSERT_EQ(NetworkResultCode::SUCCESS, res.resultCode()) << attack;
        EXPECT_NE(200, res.status()) << "traversal served a file: " << attack;
        EXPECT_EQ(std::string::npos, bodyOf(res).find(web.secret))
            << "secret leaked via " << attack;
    }

    releaseSecuritySocket();
}

// A dotfile inside the root is refused even though there is no traversal — the
// path is perfectly legal, it just names something that must not be exposed.
TEST(HttpStatic, DotfilesAreNotServed)
{
    initializeSecuritySocket();
    WebRoot web;
    StaticHandler handler{ web.root, false };
    Runner server{ handler };
    ASSERT_TRUE(server.ok());

    HttpClient client{ staticConfig() };

    for (const char* attack : { "/.env", "/.git/config", "/assets/../.env" })
    {
        auto res = client.get(attack);
        ASSERT_EQ(NetworkResultCode::SUCCESS, res.resultCode()) << attack;
        EXPECT_NE(200, res.status()) << "dotfile served: " << attack;
        EXPECT_EQ(std::string::npos, bodyOf(res).find(web.secret))
            << "secret leaked via " << attack;
    }

    releaseSecuritySocket();
}

// An exact route registered for the SAME path as the static catch-all must win,
// and the router must warn (to stderr) rather than silently pick one. Here the
// user claims "/" for a custom landing page; registerStatic's "/" yields to it.
TEST(HttpStatic, ExplicitRootRouteWinsOverStaticIndex)
{
    initializeSecuritySocket();
    WebRoot web;

    class RootOwningHandler : public HttpRequestHandler
    {
    public:
        explicit RootOwningHandler(std::string root) : _root(std::move(root)) {}
        void registerRoutes(HttpRouter& router) override
        {
            // Declared BEFORE registerStatic: first-writer-wins keeps it.
            router.get("/", [](ClientConnection&, HttpRequest&, HttpResponse& res) {
                res.status(200).body("CUSTOM-LANDING", 14);
            });
            router.registerStatic(_root.c_str(), false);   // its "/" is ignored (warns)
        }
    private:
        std::string _root;
    };

    RootOwningHandler handler{ web.root };
    Runner server{ handler };
    ASSERT_TRUE(server.ok());

    HttpClient client{ staticConfig() };
    auto res = client.get("/");

    ASSERT_EQ(NetworkResultCode::SUCCESS, res.resultCode());
    EXPECT_EQ(200, res.status());
    EXPECT_EQ("CUSTOM-LANDING", bodyOf(res));   // not index.html

    // ...and files still serve through the catch-all.
    auto css = client.get("/app.css");
    EXPECT_EQ(200, css.status());
    EXPECT_EQ("body{color:red}", bodyOf(css));

    releaseSecuritySocket();
}

TEST(HttpStatic, SpaFallbackServesIndexForUnknownPath)
{
    initializeSecuritySocket();
    WebRoot web;
    StaticHandler handler{ web.root, /*spa=*/true };
    Runner server{ handler };
    ASSERT_TRUE(server.ok());

    HttpClient client{ staticConfig() };

    // A real asset still serves itself...
    {
        auto res = client.get("/app.css");
        EXPECT_EQ(200, res.status());
        EXPECT_EQ("body{color:red}", bodyOf(res));
    }
    // ...but a client-side route falls through to index.html instead of 404.
    {
        auto res = client.get("/dashboard/settings");
        ASSERT_EQ(NetworkResultCode::SUCCESS, res.resultCode());
        EXPECT_EQ(200, res.status());
        EXPECT_NE(std::string::npos, bodyOf(res).find("INDEX"));
        EXPECT_STREQ("text/html; charset=utf-8", res.header("Content-Type"));
    }

    releaseSecuritySocket();
}

// Even with SPA fallback on, a traversal attempt must not be rewarded with the
// secret — it should get index.html (the fallback), never the escaped file.
TEST(HttpStatic, SpaFallbackDoesNotDefeatTraversalGuard)
{
    initializeSecuritySocket();
    WebRoot web;
    StaticHandler handler{ web.root, /*spa=*/true };
    Runner server{ handler };
    ASSERT_TRUE(server.ok());

    HttpClient client{ staticConfig() };
    auto res = client.get("/%2e%2e/secret.txt");

    ASSERT_EQ(NetworkResultCode::SUCCESS, res.resultCode());
    EXPECT_EQ(std::string::npos, bodyOf(res).find(web.secret));

    releaseSecuritySocket();
}
