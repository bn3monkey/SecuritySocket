// CustomPhase unit tests — raw Custom Protocol dispatch wait points.
//
// Drives CustomPhase against a StubCustomHandler whose header encodes the
// dispatch mode, asserting the ConnectionState for each message:
//   FAST / SLOW   -> SendingCustomResponse  (response produced)
//   WRITE_STREAM  -> ReceivingCustomStream  (begin), then chunks ingested via
//                    onWriteStreamData until COMPLETE (-> WaitingForNext) /
//                    ABORT (-> Closing). No response is produced.
//   READ_STREAM   -> SendingCustomStream    (begin + first chunk), then one
//                    chunk per send completion until COMPLETE.
//   incomplete header/payload -> ReceivingCustomMessage (NEED_MORE, no drain)
//
// FakePhaseHost mirrors the host assigning _state = phase return, so the phase
// observes its own previous transition through host.state().

#include <gtest/gtest.h>

#include "securitysockettest_phase_host.hpp"
#include "../src/implementation/connection/CustomPhase.hpp"

#include <cstring>
#include <vector>

using namespace Bn3Monkey;
using PhaseTest::FakePhaseHost;
using PhaseTest::StubCustomHandler;
using PhaseTest::StubHeader;

namespace { constexpr int kRounds = 120; }

// FAST: every message yields a response and recycles to "waiting".
TEST(CustomPhase, FastMessagesProduceResponseAndRecycle)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    CustomPhase phase;

    for (int i = 0; i < kRounds; ++i) {
        auto msg = PhaseTest::makeCustomMessage(/*mode=FAST*/ 0, /*payload*/ 16);
        host.feed(msg);

        EXPECT_EQ(ConnectionState::SendingCustomResponse, host.drive(phase)) << "round " << i;
        EXPECT_EQ(sizeof(StubHeader), host.outputSize());
        EXPECT_FALSE(host.slowUsed());
        EXPECT_EQ(ConnectionState::WaitingForNextCustomMessage, host.completeSend(phase));
    }
    EXPECT_EQ(kRounds, handler.process_count);
}

// SLOW: dispatched to the worker (inline here) and still yields a response.
TEST(CustomPhase, SlowMessagesDispatchAndRespond)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    CustomPhase phase;

    for (int i = 0; i < kRounds; ++i) {
        auto msg = PhaseTest::makeCustomMessage(/*SLOW*/ 1, 16);
        host.feed(msg);
        EXPECT_EQ(ConnectionState::SendingCustomResponse, host.drive(phase)) << "round " << i;
        EXPECT_TRUE(host.slowUsed());
        EXPECT_GT(host.outputSize(), 0u);
        EXPECT_EQ(ConnectionState::WaitingForNextCustomMessage, host.completeSend(phase));
    }
    EXPECT_EQ(kRounds, handler.process_count);
}

// Incomplete header, then incomplete payload, then completion — the NEED_MORE
// path stays in ReceivingCustomMessage WITHOUT draining until the full message
// has arrived.
TEST(CustomPhase, PartialMessagesNeedMoreThenDispatch)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    CustomPhase phase;

    for (int i = 0; i < kRounds; ++i) {
        auto msg = PhaseTest::makeCustomMessage(/*FAST*/ 0, 32);

        host.feed(msg.data(), 4);   // (a) partial header
        EXPECT_EQ(ConnectionState::ReceivingCustomMessage, host.drive(phase)) << "hdr round " << i;

        host.feed(msg.data() + 4, sizeof(StubHeader) - 4);   // (b) header complete, payload missing
        EXPECT_EQ(ConnectionState::ReceivingCustomMessage, host.drive(phase)) << "pay round " << i;

        host.feed(msg.data() + sizeof(StubHeader),           // (c) payload -> dispatch
                  msg.size() - sizeof(StubHeader));
        EXPECT_EQ(ConnectionState::SendingCustomResponse, host.drive(phase)) << "done round " << i;
        EXPECT_EQ(ConnectionState::WaitingForNextCustomMessage, host.completeSend(phase));
    }
}

// WRITE_STREAM: the begin message fires onWriteStreamBegin and parks in
// ReceivingCustomStream; each chunk routes to onWriteStreamData (no response)
// until the last chunk returns COMPLETE. Fixed memory: each chunk is drained
// immediately so the buffer never accumulates more than one chunk.
TEST(CustomPhase, WriteStreamIngestsChunksUntilComplete)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    CustomPhase phase;

    // begin (payload-less stream entry)
    auto begin = PhaseTest::makeCustomMessage(/*WRITE_STREAM*/ 3, 0);
    host.feed(begin);
    EXPECT_EQ(ConnectionState::ReceivingCustomStream, host.drive(phase));
    EXPECT_EQ(1, handler.write_begin_count);
    EXPECT_EQ(0u, host.inputPending());

    constexpr int N = 64;
    constexpr int kChunkPayload = 100;
    std::vector<char> expected;
    for (int i = 0; i < N; ++i) {
        const int last = (i == N - 1) ? 1 : 0;
        auto chunk = PhaseTest::makeCustomMessage(/*tag*/ 3, kChunkPayload, last);
        for (int j = 0; j < kChunkPayload; ++j) {
            expected.push_back(static_cast<char>('A' + (j % 26)));
        }
        host.feed(chunk);
        const auto st = host.drive(phase);
        if (last) {
            EXPECT_EQ(ConnectionState::WaitingForNextCustomMessage, st) << "last chunk " << i;
        } else {
            EXPECT_EQ(ConnectionState::ReceivingCustomStream, st) << "chunk " << i;
        }
        EXPECT_EQ(0u, host.inputPending()) << "chunk " << i;   // fixed memory
        EXPECT_EQ(0u, host.outputSize());                      // no response
    }
    EXPECT_EQ(N, handler.write_data_count);
    EXPECT_EQ(0, handler.process_count);
    ASSERT_EQ(expected.size(), handler.write_accum.size());
    EXPECT_TRUE(std::memcmp(expected.data(), handler.write_accum.data(), expected.size()) == 0);
}

// All chunks delivered in a single buffer are processed in one onReadable pass
// (the phase loops internally over buffered chunks).
TEST(CustomPhase, WriteStreamProcessesBatchedChunksInOnePass)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    CustomPhase phase;

    auto begin = PhaseTest::makeCustomMessage(/*WRITE_STREAM*/ 3, 0);
    host.feed(begin);
    EXPECT_EQ(ConnectionState::ReceivingCustomStream, host.drive(phase));

    constexpr int N = 32;
    std::vector<char> batch;
    for (int i = 0; i < N; ++i) {
        auto chunk = PhaseTest::makeCustomMessage(3, 40, /*last*/ (i == N - 1) ? 1 : 0);
        batch.insert(batch.end(), chunk.begin(), chunk.end());
    }
    host.feed(batch);
    EXPECT_EQ(ConnectionState::WaitingForNextCustomMessage, host.drive(phase));
    EXPECT_EQ(N, handler.write_data_count);
    EXPECT_EQ(0u, host.inputPending());
}

// Desync guard: after a WRITE_STREAM completes, a pipelined FAST message that
// followed the last chunk in the same buffer parses correctly.
TEST(CustomPhase, WriteStreamCompletionThenPipelinedMessageParses)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    CustomPhase phase;

    auto begin = PhaseTest::makeCustomMessage(/*WRITE_STREAM*/ 3, 0);
    host.feed(begin);
    EXPECT_EQ(ConnectionState::ReceivingCustomStream, host.drive(phase));

    // [last chunk][following FAST message] concatenated into one buffer.
    auto lastChunk = PhaseTest::makeCustomMessage(3, 16, /*last*/ 1);
    auto fastMsg   = PhaseTest::makeCustomMessage(/*FAST*/ 0, 16);
    std::vector<char> buf(lastChunk);
    buf.insert(buf.end(), fastMsg.begin(), fastMsg.end());
    host.feed(buf);

    // First pass consumes the last chunk and completes the stream; the FAST
    // message stays pending (the phase returns at COMPLETE).
    EXPECT_EQ(ConnectionState::WaitingForNextCustomMessage, host.drive(phase));
    EXPECT_GT(host.inputPending(), 0u);
    EXPECT_EQ(1, handler.write_data_count);

    // Second pass parses the pipelined FAST message — no desync.
    EXPECT_EQ(ConnectionState::SendingCustomResponse, host.drive(phase));
    EXPECT_EQ(1, handler.process_count);
    EXPECT_EQ(ConnectionState::WaitingForNextCustomMessage, host.completeSend(phase));
}

// WRITE_STREAM ABORT: a chunk returning ABORT tears the connection down.
TEST(CustomPhase, WriteStreamAbortClosesConnection)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    CustomPhase phase;

    auto begin = PhaseTest::makeCustomMessage(/*WRITE_STREAM*/ 3, 0);
    host.feed(begin);
    EXPECT_EQ(ConnectionState::ReceivingCustomStream, host.drive(phase));

    auto bad = PhaseTest::makeCustomMessage(3, 16, /*last = abort*/ -1);
    host.feed(bad);
    EXPECT_EQ(ConnectionState::Closing, host.drive(phase));
}

// READ_STREAM: the begin message fires onReadStreamBegin, produces the first
// chunk and parks in SendingCustomStream; each send completion produces the
// next chunk until the K-th returns COMPLETE. The last-chunk decision (fill
// time) is one tick ahead of the transition (its flush completion).
TEST(CustomPhase, ReadStreamEmitsChunksUntilComplete)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    CustomPhase phase;

    constexpr int K = 16;
    auto begin = PhaseTest::makeCustomMessage(/*READ_STREAM*/ 2, 0, /*last*/ 0, /*chunk_count*/ K);
    host.feed(begin);

    // Entry: begin + first chunk.
    EXPECT_EQ(ConnectionState::SendingCustomStream, host.drive(phase));
    EXPECT_EQ(1, handler.read_begin_count);
    EXPECT_EQ(1, handler.read_data_count);
    EXPECT_EQ(StubCustomHandler::kReadChunkSize, host.outputSize());

    // Chunks 2..K: one per send completion, all still SendingCustomStream.
    for (int i = 1; i < K; ++i) {
        EXPECT_EQ(ConnectionState::SendingCustomStream, host.completeSend(phase)) << "chunk " << i;
        EXPECT_EQ(StubCustomHandler::kReadChunkSize, host.outputSize());
    }
    EXPECT_EQ(K, handler.read_data_count);

    // The last chunk was flushed; the 1-tick _read_last now ends the stream.
    EXPECT_EQ(ConnectionState::WaitingForNextCustomMessage, host.completeSend(phase));
    EXPECT_EQ(K * StubCustomHandler::kReadChunkSize, handler.read_total_produced);
}

// READ_STREAM ABORT mid-stream: the chunk returning ABORT closes the connection.
TEST(CustomPhase, ReadStreamAbortClosesConnection)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    handler.read_abort_at = 3;   // 3rd onReadStreamData returns ABORT
    CustomPhase phase;

    auto begin = PhaseTest::makeCustomMessage(/*READ_STREAM*/ 2, 0, 0, /*chunk_count*/ 10);
    host.feed(begin);
    EXPECT_EQ(ConnectionState::SendingCustomStream, host.drive(phase));   // chunk 1
    EXPECT_EQ(ConnectionState::SendingCustomStream, host.completeSend(phase));  // chunk 2
    EXPECT_EQ(ConnectionState::Closing, host.completeSend(phase));        // chunk 3 -> ABORT
}
