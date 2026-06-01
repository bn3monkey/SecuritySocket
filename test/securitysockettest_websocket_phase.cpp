// WebSocketPhase unit tests — WebSocket frame dispatch wait points.
//
// WebSocket transports Custom Protocol messages over BINARY frames. Drives
// WebSocketPhase with hand-built masked client frames and asserts the
// ConnectionState:
//   complete BINARY message      -> SendingWebSocketResponse -> WaitingForNextWebSocketMessage
//   fragmented (BINARY+CONTINUATION) -> ReceivingWebSocketFrame then SendingWebSocketResponse
//   PING                          -> SendingWebSocketResponse (Pong)
//   PONG                          -> ReceivingWebSocketFrame (noop)
//   CLOSE / TEXT                  -> Closing
//   WRITE_STREAM message          -> ReceivingWebSocketFrame (no response)
//   partial frame                 -> ReceivingWebSocketFrame (NEED_MORE)
// Each test pushes 100+ frames/messages through the phase.

#include <gtest/gtest.h>

#include "securitysockettest_phase_host.hpp"
#include "../src/implementation/connection/WebSocketPhase.hpp"

#include <vector>

using namespace Bn3Monkey;
using PhaseTest::FakePhaseHost;
using PhaseTest::StubCustomHandler;

namespace { constexpr int kRounds = 120; }

// A complete BINARY frame carrying one FAST Custom message -> response, recycle.
TEST(WebSocketPhase, BinaryMessageDispatchesAndRecycles)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    WebSocketPhase phase;

    for (int i = 0; i < kRounds; ++i) {
        auto frame = PhaseTest::makeClientBinaryMessage(/*FAST*/ 0, /*payload*/ 16);
        host.feed(frame);
        EXPECT_EQ(ConnectionState::SendingWebSocketResponse, host.drive(phase)) << "round " << i;
        EXPECT_GT(host.outputSize(), 0u);
        EXPECT_EQ(ConnectionState::WaitingForNextWebSocketMessage, host.completeSend(phase));
    }
    EXPECT_EQ(kRounds, handler.process_count);
}

// A message split into BINARY(fin=0) + CONTINUATION(fin=1): the first frame is
// buffered (stay reading), the FIN frame completes and dispatches.
TEST(WebSocketPhase, FragmentedMessageReassembles)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    WebSocketPhase phase;

    for (int i = 0; i < kRounds; ++i) {
        auto msg = PhaseTest::makeCustomMessage(/*FAST*/ 0, 16);   // 24 bytes
        const size_t half = msg.size() / 2;

        auto f1 = PhaseTest::makeClientFrame(WsOpcode::BINARY, msg.data(), half, /*fin=*/false);
        host.feed(f1);
        EXPECT_EQ(ConnectionState::ReceivingWebSocketFrame, host.drive(phase)) << "frag1 round " << i;

        auto f2 = PhaseTest::makeClientFrame(WsOpcode::CONTINUATION,
                                             msg.data() + half, msg.size() - half, /*fin=*/true);
        host.feed(f2);
        EXPECT_EQ(ConnectionState::SendingWebSocketResponse, host.drive(phase)) << "frag2 round " << i;
        EXPECT_EQ(ConnectionState::WaitingForNextWebSocketMessage, host.completeSend(phase));
    }
    EXPECT_EQ(kRounds, handler.process_count);
}

// PING -> a Pong response is queued.
TEST(WebSocketPhase, PingProducesPong)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    WebSocketPhase phase;

    const char ping[] = "hello";
    for (int i = 0; i < kRounds; ++i) {
        auto frame = PhaseTest::makeClientFrame(WsOpcode::PING, ping, sizeof(ping) - 1, true);
        host.feed(frame);
        EXPECT_EQ(ConnectionState::SendingWebSocketResponse, host.drive(phase)) << "round " << i;
        EXPECT_GT(host.outputSize(), 0u);
        EXPECT_EQ(ConnectionState::WaitingForNextWebSocketMessage, host.completeSend(phase));
    }
}

// PONG -> counters only, stay reading (no response).
TEST(WebSocketPhase, PongIsNoop)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    WebSocketPhase phase;

    const char pong[] = "pong";
    for (int i = 0; i < kRounds; ++i) {
        auto frame = PhaseTest::makeClientFrame(WsOpcode::PONG, pong, sizeof(pong) - 1, true);
        host.feed(frame);
        EXPECT_EQ(ConnectionState::ReceivingWebSocketFrame, host.drive(phase)) << "round " << i;
        EXPECT_EQ(0u, host.inputPending());   // control frame drained
    }
}

// CLOSE -> echo a close and move to Closing.
TEST(WebSocketPhase, CloseFrameCloses)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    WebSocketPhase phase;

    const char close_payload[] = { 0x03, char(0xE8) };   // status 1000
    for (int i = 0; i < kRounds; ++i) {
        auto frame = PhaseTest::makeClientFrame(WsOpcode::CLOSE,
                                                close_payload, sizeof(close_payload), true);
        host.feed(frame);
        EXPECT_EQ(ConnectionState::Closing, host.drive(phase)) << "round " << i;
    }
}

// TEXT is never a valid Custom carrier -> Closing (1003).
TEST(WebSocketPhase, TextFrameCloses)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    WebSocketPhase phase;

    const char text[] = "plain text";
    for (int i = 0; i < kRounds; ++i) {
        auto frame = PhaseTest::makeClientFrame(WsOpcode::TEXT, text, sizeof(text) - 1, true);
        host.feed(frame);
        EXPECT_EQ(ConnectionState::Closing, host.drive(phase)) << "round " << i;
    }
}

// WRITE_STREAM message over WS: ingested without a response, stay reading.
TEST(WebSocketPhase, WriteStreamMessageNoResponse)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    WebSocketPhase phase;

    for (int i = 0; i < kRounds; ++i) {
        auto frame = PhaseTest::makeClientBinaryMessage(/*WRITE_STREAM*/ 3, 16);
        host.feed(frame);
        EXPECT_EQ(ConnectionState::ReceivingWebSocketFrame, host.drive(phase)) << "round " << i;
        EXPECT_EQ(0u, host.outputSize());
    }
    EXPECT_EQ(kRounds, handler.no_response_count);
}

// READ_STREAM message over WS: unlike WRITE_STREAM, this is an RPC-style read
// (request -> single response), matching CustomPhase (raw TCP) so the same
// handler behaves identically on both transports. It must dispatch through
// process() and produce a response, NOT processWithoutResponse().
TEST(WebSocketPhase, ReadStreamMessageProducesResponse)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    WebSocketPhase phase;

    for (int i = 0; i < kRounds; ++i) {
        auto frame = PhaseTest::makeClientBinaryMessage(/*READ_STREAM*/ 2, 16);
        host.feed(frame);
        EXPECT_EQ(ConnectionState::SendingWebSocketResponse, host.drive(phase)) << "round " << i;
        EXPECT_GT(host.outputSize(), 0u);
        EXPECT_EQ(ConnectionState::WaitingForNextWebSocketMessage, host.completeSend(phase));
    }
    EXPECT_EQ(kRounds, handler.process_count);
    EXPECT_EQ(0, handler.no_response_count);
}

// A frame delivered in two recvs: the first (partial) is NEED_MORE, the rest
// completes and dispatches.
TEST(WebSocketPhase, PartialFrameNeedsMoreThenDispatches)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    WebSocketPhase phase;

    for (int i = 0; i < kRounds; ++i) {
        auto frame = PhaseTest::makeClientBinaryMessage(/*FAST*/ 0, 16);
        const size_t cut = frame.size() / 2;

        host.feed(frame.data(), cut);
        EXPECT_EQ(ConnectionState::ReceivingWebSocketFrame, host.drive(phase)) << "partial round " << i;

        host.feed(frame.data() + cut, frame.size() - cut);
        EXPECT_EQ(ConnectionState::SendingWebSocketResponse, host.drive(phase)) << "rest round " << i;
        EXPECT_EQ(ConnectionState::WaitingForNextWebSocketMessage, host.completeSend(phase));
    }
}
