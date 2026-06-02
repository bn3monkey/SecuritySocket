#include <gtest/gtest.h>

#include <SecuritySocket.hpp>
#include <cstddef>

// Phase 5 contract tests for CustomProtocolRequestHandler's capability flags.
// These don't open a socket — they pin the compile-time + runtime contract the
// server's dispatch selection (Phase 6) relies on:
//   - supportUserProtocol() is always true for this handler family.
//   - A default-constructed handler is raw-TCP only (supportWebSocket() false).
//   - Supplying a WebSocketConfiguration with a pattern flips supportWebSocket()
//     to true and is readable back via webSocketConfig().

namespace {

// Minimal concrete handler. The size/classify/process methods are never called
// here — only the capability flags and the WS-config plumbing are under test.
struct StubCustomHandler : public Bn3Monkey::CustomProtocolRequestHandler
{
    using Bn3Monkey::CustomProtocolRequestHandler::CustomProtocolRequestHandler;

    size_t headerSize() override { return 0; }
    size_t payloadSize(const void*) override { return 0; }
    Bn3Monkey::RequestProcessingMode classifyMode(const void*) override {
        return Bn3Monkey::RequestProcessingMode::FAST;
    }
    void process(const Bn3Monkey::ClientConnection&,
                 const Bn3Monkey::CustomProtocolRequest&,
                 Bn3Monkey::CustomProtocolResponse&) override {}
};

} // namespace

TEST(CustomProtocolHandler, DefaultConstructedIsRawTcpOnly)
{
    StubCustomHandler handler;

    EXPECT_TRUE(handler.supportUserProtocol());
    EXPECT_FALSE(handler.supportWebSocket());
    EXPECT_FALSE(handler.webSocketConfig().valid());
}

TEST(CustomProtocolHandler, WebSocketConfigEnablesWebSocketSupport)
{
    Bn3Monkey::WebSocketConfiguration ws;
    ws.pattern = "/ws/echo";

    StubCustomHandler handler{ ws };

    EXPECT_TRUE(handler.supportUserProtocol());
    EXPECT_TRUE(handler.supportWebSocket());
    ASSERT_TRUE(handler.webSocketConfig().valid());
    EXPECT_STREQ("/ws/echo", handler.webSocketConfig().pattern);
}
