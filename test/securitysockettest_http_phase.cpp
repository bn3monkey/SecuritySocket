// HttpPhase unit tests — HTTP/1.1 dispatch wait points.
//
// Drives HttpPhase against a real HttpRouterImpl and asserts the ConnectionState
// for each request:
//   matched route          -> SendingHttpResponse -> (keep-alive) WaitingForNextHttpRequest
//                                                  -> (close)      Closed
//   header parsed, body not -> ReceivingHttpRequest (NEED_MORE)
//   404 / 405 / malformed   -> Closing
//   matching WS Upgrade     -> SendingHandshakeResponse (+ setWebSocket)
//   SLOW route              -> SendingHttpResponse (dispatched inline)
// Each test pushes 100+ requests through the phase.

#include <gtest/gtest.h>

#include "securitysockettest_phase_host.hpp"
#include "../src/implementation/connection/HttpPhase.hpp"
#include "../src/implementation/http/HttpRouter.hpp"

#include <string>

using namespace Bn3Monkey;
using PhaseTest::FakePhaseHost;

namespace
{
    constexpr int kRounds = 120;

    // Register the routes the tests below exercise. Static handlers keep the
    // HandlerFn trivially copyable into the router.
    void ok    (ClientConnection&, HttpRequest&, HttpResponse& res) { res.status(200); }
    void okSlow(ClientConnection&, HttpRequest&, HttpResponse& res) { res.status(200); }
    void okUser(ClientConnection&, HttpRequest&, HttpResponse& res) { res.status(200); }

    void configure(HttpRouterImpl& r)
    {
        r.get ("/ping",     ok);
        r.post("/echo",     ok);
        r.get ("/slow",     okSlow, RequestProcessingMode::SLOW);
        r.get ("/user/:id", okUser);
    }

    std::string get(const std::string& path, bool keep_alive = true)
    {
        std::string r = "GET " + path + " HTTP/1.1\r\nHost: x\r\n";
        if (!keep_alive) r += "Connection: close\r\n";
        r += "\r\n";
        return r;
    }
}

// Keep-alive GETs: response then recycle, > 100 times on one connection.
TEST(HttpPhase, KeepAliveGetsRecycle)
{
    FakePhaseHost  host;
    HttpRouterImpl router; configure(router);
    host.setRouter(&router);
    HttpPhase phase;

    for (int i = 0; i < kRounds; ++i) {
        std::string req = get("/ping");
        host.feed(req.data(), req.size());
        EXPECT_EQ(ConnectionState::SendingHttpResponse, host.drive(phase)) << "round " << i;
        EXPECT_GT(host.outputSize(), 0u);
        EXPECT_EQ(ConnectionState::WaitingForNextHttpRequest, host.completeSend(phase));
    }
}

// Connection: close -> the post-send state is terminal (Closed).
TEST(HttpPhase, ConnectionCloseEndsConnection)
{
    FakePhaseHost  host;
    HttpRouterImpl router; configure(router);
    host.setRouter(&router);
    HttpPhase phase;

    for (int i = 0; i < kRounds; ++i) {
        std::string req = get("/ping", /*keep_alive=*/false);
        host.feed(req.data(), req.size());
        EXPECT_EQ(ConnectionState::SendingHttpResponse, host.drive(phase)) << "round " << i;
        EXPECT_EQ(ConnectionState::Closed, host.completeSend(phase));
    }
}

// POST with a body split across two recvs: header-only is NEED_MORE, the body
// completes the message.
TEST(HttpPhase, BodySplitAcrossRecvsNeedsMoreThenDispatches)
{
    FakePhaseHost  host;
    HttpRouterImpl router; configure(router);
    host.setRouter(&router);
    HttpPhase phase;

    const std::string body(32, 'x');
    for (int i = 0; i < kRounds; ++i) {
        std::string head = "POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: " +
                           std::to_string(body.size()) + "\r\n\r\n";
        host.feed(head.data(), head.size());
        EXPECT_EQ(ConnectionState::ReceivingHttpRequest, host.drive(phase)) << "hdr round " << i;

        host.feed(body.data(), body.size());
        EXPECT_EQ(ConnectionState::SendingHttpResponse, host.drive(phase)) << "body round " << i;
        EXPECT_EQ(ConnectionState::WaitingForNextHttpRequest, host.completeSend(phase));
    }
}

// Unknown path -> Closing (404, error/close path).
TEST(HttpPhase, UnknownRouteCloses)
{
    HttpRouterImpl router; configure(router);
    for (int i = 0; i < kRounds; ++i) {
        FakePhaseHost host; host.setRouter(&router);
        HttpPhase phase;
        std::string req = get("/no/such/route/" + std::to_string(i));
        host.feed(req.data(), req.size());
        EXPECT_EQ(ConnectionState::Closing, host.drive(phase)) << "round " << i;
    }
}

// Wrong method on an existing path -> Closing (405 or 404; the state is Closing
// either way).
TEST(HttpPhase, WrongMethodCloses)
{
    HttpRouterImpl router; configure(router);
    for (int i = 0; i < kRounds; ++i) {
        FakePhaseHost host; host.setRouter(&router);
        HttpPhase phase;
        // DELETE on /ping (only GET registered).
        std::string req = "DELETE /ping HTTP/1.1\r\nHost: x\r\n\r\n";
        host.feed(req.data(), req.size());
        EXPECT_EQ(ConnectionState::Closing, host.drive(phase)) << "round " << i;
    }
}

// Malformed request line -> Closing (400).
TEST(HttpPhase, MalformedRequestCloses)
{
    HttpRouterImpl router; configure(router);
    for (int i = 0; i < kRounds; ++i) {
        FakePhaseHost host; host.setRouter(&router);
        HttpPhase phase;
        // Missing the HTTP-version token after the path → picohttpparser -1.
        std::string req = "GET /ping\r\n\r\n";
        host.feed(req.data(), req.size());
        EXPECT_EQ(ConnectionState::Closing, host.drive(phase)) << "round " << i;
    }
}

// A matching WebSocket Upgrade leaves the HTTP group via SendingHandshakeResponse
// and latches the connection as upgraded.
TEST(HttpPhase, WebSocketUpgradeHandshake)
{
    HttpRouterImpl router; configure(router);
    for (int i = 0; i < kRounds; ++i) {
        FakePhaseHost host;
        host.setRouter(&router);
        host.setWsPattern("/ws");
        HttpPhase phase;

        std::string req =
            "GET /ws HTTP/1.1\r\nHost: x\r\n"
            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n";
        host.feed(req.data(), req.size());

        EXPECT_EQ(ConnectionState::SendingHandshakeResponse, host.drive(phase)) << "round " << i;
        EXPECT_TRUE(host.isWebSocket());
        EXPECT_GT(host.outputSize(), 0u);   // the 101 handshake
    }
}

// Regression (query string vs. routing): a GET whose request-target carries a
// ?query must still match the registered route, and the query must reach the
// handler. Before the fix, the trie was matched against the full target
// (path + "?query"), so every GET with a query string fell through to 404.
namespace
{
    struct Captured {
        bool        called  = false;
        std::string q_a, q_b, id;
    };
    Captured g_captured;

    void captureQuery(ClientConnection&, HttpRequest& req, HttpResponse& res)
    {
        g_captured.called = true;
        if (const char* a = req.query("a")) g_captured.q_a = a;
        if (const char* b = req.query("b")) g_captured.q_b = b;
        if (const char* id = req.pathParam("id")) g_captured.id = id;
        res.status(200);
    }
}

TEST(HttpPhase, GetWithQueryStringStillRoutes)
{
    HttpRouterImpl router;
    router.get("/x",         captureQuery);
    router.get("/items/:id", captureQuery);

    // /x?a=1&b=2 -> matches /x, query reaches the handler.
    {
        FakePhaseHost host; host.setRouter(&router);
        HttpPhase phase;
        g_captured = Captured{};
        std::string req = get("/x?a=1&b=2");
        host.feed(req.data(), req.size());
        EXPECT_EQ(ConnectionState::SendingHttpResponse, host.drive(phase));
        EXPECT_TRUE(g_captured.called);
        EXPECT_EQ("1", g_captured.q_a);
        EXPECT_EQ("2", g_captured.q_b);
    }

    // /x (no query) still matches.
    {
        FakePhaseHost host; host.setRouter(&router);
        HttpPhase phase;
        g_captured = Captured{};
        std::string req = get("/x");
        host.feed(req.data(), req.size());
        EXPECT_EQ(ConnectionState::SendingHttpResponse, host.drive(phase));
        EXPECT_TRUE(g_captured.called);
    }

    // Path param + query: ?x=1 must not bleed into the bound :id value.
    {
        FakePhaseHost host; host.setRouter(&router);
        HttpPhase phase;
        g_captured = Captured{};
        std::string req = get("/items/42?x=1");
        host.feed(req.data(), req.size());
        EXPECT_EQ(ConnectionState::SendingHttpResponse, host.drive(phase));
        EXPECT_TRUE(g_captured.called);
        EXPECT_EQ("42", g_captured.id);
    }

    // base64-ish session value carrying a percent-encoded '+' decodes to '+'.
    {
        FakePhaseHost host; host.setRouter(&router);
        HttpPhase phase;
        g_captured = Captured{};
        std::string req = get("/x?a=ab%2Bcd&b=2");
        host.feed(req.data(), req.size());
        EXPECT_EQ(ConnectionState::SendingHttpResponse, host.drive(phase));
        EXPECT_TRUE(g_captured.called);
        EXPECT_EQ("ab+cd", g_captured.q_a);
    }
}

// SLOW route is dispatched (inline here) and still produces a response.
TEST(HttpPhase, SlowRouteDispatches)
{
    FakePhaseHost  host;
    HttpRouterImpl router; configure(router);
    host.setRouter(&router);
    HttpPhase phase;

    for (int i = 0; i < kRounds; ++i) {
        std::string req = get("/slow");
        host.feed(req.data(), req.size());
        EXPECT_EQ(ConnectionState::SendingHttpResponse, host.drive(phase)) << "round " << i;
        EXPECT_TRUE(host.slowUsed());
        EXPECT_EQ(ConnectionState::WaitingForNextHttpRequest, host.completeSend(phase));
    }
}
