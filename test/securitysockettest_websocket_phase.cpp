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
//   WRITE_STREAM                  -> ReceivingWebSocketStream (begin), chunks ingested
//   READ_STREAM                   -> SendingWebSocketStream (begin + framed chunks)
//   PING mid WRITE_STREAM         -> Pong, then resume ReceivingWebSocketStream
//   partial frame                 -> ReceivingWebSocketFrame (NEED_MORE)

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
        auto msg = PhaseTest::makeCustomMessage(/*FAST*/ 0, 16);
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

// WRITE_STREAM over WS: the begin message parks in ReceivingWebSocketStream;
// each subsequent BINARY message is a chunk routed to onWriteStreamData (no
// response) until the last chunk returns COMPLETE.
TEST(WebSocketPhase, WriteStreamIngestsChunks)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    WebSocketPhase phase;

    auto begin = PhaseTest::makeClientBinaryMessage(/*WRITE_STREAM*/ 3, 0);
    host.feed(begin);
    EXPECT_EQ(ConnectionState::ReceivingWebSocketStream, host.drive(phase));
    EXPECT_EQ(1, handler.write_begin_count);

    constexpr int N = 30;
    std::vector<char> expected;
    for (int i = 0; i < N; ++i) {
        const int last = (i == N - 1) ? 1 : 0;
        auto chunk = PhaseTest::makeClientBinaryMessage(/*tag*/ 3, 50, /*fin*/ true, last);
        for (int j = 0; j < 50; ++j) expected.push_back(static_cast<char>('A' + (j % 26)));
        host.feed(chunk);
        const auto st = host.drive(phase);
        if (last) {
            EXPECT_EQ(ConnectionState::WaitingForNextWebSocketMessage, st) << "last " << i;
        } else {
            EXPECT_EQ(ConnectionState::ReceivingWebSocketStream, st) << "chunk " << i;
        }
        EXPECT_EQ(0u, host.outputSize());   // no response
    }
    EXPECT_EQ(N, handler.write_data_count);
    EXPECT_EQ(0, handler.process_count);
    ASSERT_EQ(expected.size(), handler.write_accum.size());
    EXPECT_TRUE(std::memcmp(expected.data(), handler.write_accum.data(), expected.size()) == 0);
}

// A control frame (PING) arriving mid WRITE_STREAM is answered with a Pong, and
// after the Pong flushes the connection RESUMES the stream state (rather than
// falling back to WaitingForNextWebSocketMessage).
TEST(WebSocketPhase, ControlFrameMidStreamResumes)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    WebSocketPhase phase;

    auto begin = PhaseTest::makeClientBinaryMessage(/*WRITE_STREAM*/ 3, 0);
    host.feed(begin);
    EXPECT_EQ(ConnectionState::ReceivingWebSocketStream, host.drive(phase));

    // Ingest a couple of chunks, then a PING interrupts.
    auto c0 = PhaseTest::makeClientBinaryMessage(3, 32, true, /*last*/ 0);
    host.feed(c0);
    EXPECT_EQ(ConnectionState::ReceivingWebSocketStream, host.drive(phase));

    const char ping[] = "ka";
    auto pingFrame = PhaseTest::makeClientFrame(WsOpcode::PING, ping, sizeof(ping) - 1, true);
    host.feed(pingFrame);
    EXPECT_EQ(ConnectionState::SendingWebSocketResponse, host.drive(phase));   // Pong queued
    EXPECT_GT(host.outputSize(), 0u);

    // After the Pong flush, resume the stream.
    EXPECT_EQ(ConnectionState::ReceivingWebSocketStream, host.completeSend(phase));

    // Streaming continues: a chunk still routes to onWriteStreamData.
    auto last = PhaseTest::makeClientBinaryMessage(3, 32, true, /*last*/ 1);
    host.feed(last);
    EXPECT_EQ(ConnectionState::WaitingForNextWebSocketMessage, host.drive(phase));
    EXPECT_EQ(2, handler.write_data_count);   // c0 + last
}

// READ_STREAM over WS: begin produces the first framed chunk and parks in
// SendingWebSocketStream; each send completion frames the next chunk until
// COMPLETE. Each chunk is wrapped in one BINARY frame (framed > raw payload).
TEST(WebSocketPhase, ReadStreamEmitsFramedChunks)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    WebSocketPhase phase;

    constexpr int K = 10;
    auto begin = PhaseTest::makeClientBinaryMessage(/*READ_STREAM*/ 2, 0, /*fin*/ true,
                                                    /*last*/ 0, /*chunk_count*/ K);
    host.feed(begin);
    EXPECT_EQ(ConnectionState::SendingWebSocketStream, host.drive(phase));
    EXPECT_EQ(1, handler.read_begin_count);
    EXPECT_EQ(1, handler.read_data_count);
    EXPECT_GT(host.outputSize(), StubCustomHandler::kReadChunkSize);   // framed

    for (int i = 1; i < K; ++i) {
        EXPECT_EQ(ConnectionState::SendingWebSocketStream, host.completeSend(phase)) << "chunk " << i;
        EXPECT_GT(host.outputSize(), StubCustomHandler::kReadChunkSize);
    }
    EXPECT_EQ(K, handler.read_data_count);
    EXPECT_EQ(ConnectionState::WaitingForNextWebSocketMessage, host.completeSend(phase));
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
