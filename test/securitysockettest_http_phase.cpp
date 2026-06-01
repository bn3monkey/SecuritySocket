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
