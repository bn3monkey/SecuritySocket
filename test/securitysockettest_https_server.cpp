// HTTP/1.1 over TLS — the HTTPS server path.
//
// The plaintext twin (securitysockettest_http_server.cpp) drives the server with
// libcurl to prove third-party interoperability. This suite cannot: the vendored
// libcurl is configured with CURL_ENABLE_SSL=OFF, so it has no HTTPS support at
// all. It uses the project's own HttpClient over a TLS transport instead.
//
// That is a weaker independence claim, so what this suite is really for is the
// *seam*: the HTTP phase parses plaintext handed to it by the TLS layer, and the
// router, keep-alive, path params and 404s must behave exactly as they do on a
// bare socket. Third-party HTTPS interop is covered separately by driving
// `openssl s_client` against the server (see docs/workplan/TLS_SERVER.md).

#include <gtest/gtest.h>

#include <SecuritySocket.hpp>

#include <cstring>
#include <string>
#include <thread>

#include "securitysockettest_helper.hpp"     // printConcurrent
#include "securitysockettest_tls_fixture.hpp"

using namespace Bn3MonkeyTest;

namespace
{
    constexpr uint16_t kHttpsPort = 28871;

    Bn3Monkey::NetworkConfiguration httpsConfig()
    {
        return Bn3Monkey::NetworkConfiguration{
            "127.0.0.1", kHttpsPort, false, 5, 3000, 3000, 100, 8192 };
    }

    class HttpsEchoHandler : public Bn3Monkey::HttpRequestHandler
    {
    public:
        void onConnected(const Bn3Monkey::ClientConnection& conn) override
        {
            printConcurrent("[server] onConnected    ip=%s port=%u secure=%d  (TLS handshake OK)\n",
                            conn.ip(), conn.port(), (int)conn.isSecure());
        }

        void onDisconnected(const Bn3Monkey::ClientConnection& conn) override
        {
            printConcurrent("[server] onDisconnected ip=%s port=%u\n", conn.ip(), conn.port());
        }

        void registerRoutes(Bn3Monkey::HttpRouter& router) override
        {
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

            // Reports the transport the request arrived on, so a test can assert
            // the handler really is seeing an encrypted connection.
            router.get("/secure", [](const Bn3Monkey::ClientConnection& conn,
                                     Bn3Monkey::HttpRequest&,
                                     Bn3Monkey::HttpResponse& res) {
                const char* answer = conn.isSecure() ? "yes" : "no";
                res.status(200).body(answer, std::strlen(answer));
            });
        }
    };

    // RAII: start an HTTPS RequestServer, stop it on scope exit.
    class HttpsServerRunner
    {
    public:
        explicit HttpsServerRunner(Bn3Monkey::RequestHandler& handler)
            : _server(httpsConfig(), makeServerTls(_certs))
        {
            auto r = _server.open(&handler, 8);
            _ok = (r.code() == Bn3Monkey::NetworkResultCode::SUCCESS);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        ~HttpsServerRunner() { _server.close(); }

        bool ok() const { return _ok; }

    private:
        ServerCertFiles         _certs;
        Bn3Monkey::RequestServer _server;
        bool                     _ok{ false };
    };

    std::string bodyOf(const Bn3Monkey::HttpClientResponse& res)
    {
        return std::string(static_cast<const char*>(res.body()), res.bodySize());
    }

    // One line per HTTPS exchange so the terminal shows what actually went over
    // the wire: method, path, status, and a short body preview.
    void logExchange(const char* method, const char* path,
                     const Bn3Monkey::HttpClientResponse& res)
    {
        const std::string body = bodyOf(res);
        printConcurrent("[client] %-4s %-16s -> %d  (%zu bytes) \"%.40s\"\n",
                        method, path, res.status(), body.size(), body.c_str());
    }
}

TEST(HttpsServer, GetRouteReturnsBody)
{
    using namespace Bn3Monkey;
    initializeSecuritySocket();

    HttpsEchoHandler handler;
    HttpsServerRunner server{ handler };
    ASSERT_TRUE(server.ok());

    HttpClient client{ httpsConfig(), makeClientTls() };
    auto res = client.get("/ping");
    logExchange("GET", "/ping", res);

    ASSERT_EQ(NetworkResultCode::SUCCESS, res.resultCode());
    EXPECT_EQ(200, res.status());
    EXPECT_EQ("pong", bodyOf(res));

    releaseSecuritySocket();
}

// The handler must observe the connection as encrypted — this is the assertion
// that the TLS socket, not the plaintext one, is the transport underneath.
TEST(HttpsServer, HandlerSeesConnectionAsSecure)
{
    using namespace Bn3Monkey;
    initializeSecuritySocket();

    HttpsEchoHandler handler;
    HttpsServerRunner server{ handler };
    ASSERT_TRUE(server.ok());

    HttpClient client{ httpsConfig(), makeClientTls() };
    auto res = client.get("/secure");
    logExchange("GET", "/secure", res);

    ASSERT_EQ(NetworkResultCode::SUCCESS, res.resultCode());
    EXPECT_EQ(200, res.status());
    EXPECT_EQ("yes", bodyOf(res));

    releaseSecuritySocket();
}

TEST(HttpsServer, PostEchoesRequestBody)
{
    using namespace Bn3Monkey;
    initializeSecuritySocket();

    HttpsEchoHandler handler;
    HttpsServerRunner server{ handler };
    ASSERT_TRUE(server.ok());

    HttpClient client{ httpsConfig(), makeClientTls() };
    const std::string payload = "the quick brown fox jumps over the lazy dog";
    auto res = client.post("/echo", payload.data(), payload.size());
    logExchange("POST", "/echo", res);

    ASSERT_EQ(NetworkResultCode::SUCCESS, res.resultCode());
    EXPECT_EQ(200, res.status());
    EXPECT_EQ(payload, bodyOf(res));

    releaseSecuritySocket();
}

// A body far larger than one TLS record (16 KB) and larger than the 8 KB pdu:
// the request must be reassembled across records on the way in, and the response
// written across records on the way out.
TEST(HttpsServer, PostLargeBodySpansManyTlsRecords)
{
    using namespace Bn3Monkey;
    initializeSecuritySocket();

    HttpsEchoHandler handler;
    HttpsServerRunner server{ handler };
    ASSERT_TRUE(server.ok());

    std::string payload;
    payload.reserve(64 * 1024);
    for (size_t i = 0; i < 64 * 1024; ++i)
        payload.push_back(static_cast<char>('a' + (i % 26)));

    HttpClient client{ httpsConfig(), makeClientTls() };
    printConcurrent("[client] POST /echo with %zu KB body (spans many TLS records)...\n",
                    payload.size() / 1024);
    auto res = client.post("/echo", payload.data(), payload.size());
    printConcurrent("[client] POST /echo -> %d, echoed %zu bytes back\n",
                    res.status(), res.bodySize());

    ASSERT_EQ(NetworkResultCode::SUCCESS, res.resultCode());
    EXPECT_EQ(200, res.status());
    EXPECT_EQ(payload.size(), res.bodySize());
    EXPECT_EQ(payload, bodyOf(res));

    releaseSecuritySocket();
}

TEST(HttpsServer, PathParamIsBound)
{
    using namespace Bn3Monkey;
    initializeSecuritySocket();

    HttpsEchoHandler handler;
    HttpsServerRunner server{ handler };
    ASSERT_TRUE(server.ok());

    HttpClient client{ httpsConfig(), makeClientTls() };
    auto res = client.get("/user/42");
    logExchange("GET", "/user/42", res);

    ASSERT_EQ(NetworkResultCode::SUCCESS, res.resultCode());
    EXPECT_EQ(200, res.status());
    EXPECT_EQ("42", bodyOf(res));

    releaseSecuritySocket();
}

TEST(HttpsServer, UnknownRouteReturns404)
{
    using namespace Bn3Monkey;
    initializeSecuritySocket();

    HttpsEchoHandler handler;
    HttpsServerRunner server{ handler };
    ASSERT_TRUE(server.ok());

    HttpClient client{ httpsConfig(), makeClientTls() };
    auto res = client.get("/no/such/route");
    logExchange("GET", "/no/such/route", res);

    ASSERT_EQ(NetworkResultCode::SUCCESS, res.resultCode());
    EXPECT_EQ(404, res.status());

    releaseSecuritySocket();
}

// Keep-alive over TLS: every request after the first rides the *same* SSL
// session, so this covers the path where the connection returns to a read state
// and decrypts a fresh request without a new handshake.
TEST(HttpsServer, KeepAliveManyRequestsOnOneConnection)
{
    using namespace Bn3Monkey;
    initializeSecuritySocket();

    HttpsEchoHandler handler;
    HttpsServerRunner server{ handler };
    ASSERT_TRUE(server.ok());

    HttpClient client{ httpsConfig(), makeClientTls() };

    printConcurrent("[client] 20 GET /ping on ONE TLS session (keep-alive, one handshake)\n");
    for (int i = 0; i < 20; ++i)
    {
        auto res = client.get("/ping");
        ASSERT_EQ(NetworkResultCode::SUCCESS, res.resultCode()) << "request " << i;
        EXPECT_EQ(200, res.status()) << "request " << i;
        EXPECT_EQ("pong", bodyOf(res)) << "request " << i;
        printConcurrent("[client]   request %2d/20 -> %d \"%s\"\n", i + 1, res.status(), bodyOf(res).c_str());
    }

    releaseSecuritySocket();
}
