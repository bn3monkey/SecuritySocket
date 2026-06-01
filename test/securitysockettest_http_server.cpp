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
#include <cstring>
#include <string>
#include <thread>

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

#endif  // SECURITYSOCKET_TEST_USE_CURL
