// CustomPhase unit tests — raw Custom Protocol dispatch wait points.
//
// Drives CustomPhase against a StubCustomHandler whose 8-byte header encodes the
// dispatch mode, asserting the ConnectionState for each message:
//   FAST / READ_STREAM -> SendingCustomResponse  (response produced)
//   SLOW               -> SendingCustomResponse  (run inline via dispatchSlow)
//   WRITE_STREAM        -> WaitingForNextCustomMessage  (no response)
//   incomplete header/payload -> ReceivingCustomMessage  (NEED_MORE, no drain)
// onSendComplete always recycles to WaitingForNextCustomMessage.
//
// Each test runs > 100 messages through one phase instance (the phase is
// stateless; messages are drained per dispatch), exercising the drain path.

#include <gtest/gtest.h>

#include "securitysockettest_phase_host.hpp"
#include "../src/implementation/connection/CustomPhase.hpp"

using namespace Bn3Monkey;
using PhaseTest::FakePhaseHost;
using PhaseTest::StubCustomHandler;

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
        EXPECT_EQ(sizeof(PhaseTest::StubHeader), host.outputSize());
        EXPECT_FALSE(host.slowUsed());
        EXPECT_EQ(ConnectionState::WaitingForNextCustomMessage, host.completeSend(phase));
    }
    EXPECT_EQ(kRounds, handler.process_count);
}

// READ_STREAM is treated like FAST (client reads from server -> needs a response).
TEST(CustomPhase, ReadStreamProducesResponse)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    CustomPhase phase;

    for (int i = 0; i < kRounds; ++i) {
        auto msg = PhaseTest::makeCustomMessage(/*READ_STREAM*/ 2, 16);
        host.feed(msg);
        EXPECT_EQ(ConnectionState::SendingCustomResponse, host.drive(phase)) << "round " << i;
        EXPECT_GT(host.outputSize(), 0u);
        EXPECT_EQ(ConnectionState::WaitingForNextCustomMessage, host.completeSend(phase));
    }
    EXPECT_EQ(kRounds, handler.process_count);
}

// WRITE_STREAM: server only ingests -> no response, straight back to waiting.
TEST(CustomPhase, WriteStreamConsumesWithoutResponse)
{
    FakePhaseHost     host;
    StubCustomHandler handler;
    host.setCustom(&handler);
    CustomPhase phase;

    for (int i = 0; i < kRounds; ++i) {
        auto msg = PhaseTest::makeCustomMessage(/*WRITE_STREAM*/ 3, 16);
        host.feed(msg);
        EXPECT_EQ(ConnectionState::WaitingForNextCustomMessage, host.drive(phase)) << "round " << i;
        EXPECT_EQ(0u, host.outputSize());   // nothing written back
        EXPECT_EQ(0u, host.inputPending()); // message fully drained
    }
    EXPECT_EQ(kRounds, handler.no_response_count);
    EXPECT_EQ(0,       handler.process_count);
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

        // (a) first 4 header bytes only -> need more (header incomplete)
        host.feed(msg.data(), 4);
        EXPECT_EQ(ConnectionState::ReceivingCustomMessage, host.drive(phase)) << "hdr round " << i;

        // (b) rest of header (now 8 bytes) -> header complete but payload missing
        host.feed(msg.data() + 4, sizeof(PhaseTest::StubHeader) - 4);
        EXPECT_EQ(ConnectionState::ReceivingCustomMessage, host.drive(phase)) << "pay round " << i;

        // (c) the payload -> full message dispatched
        host.feed(msg.data() + sizeof(PhaseTest::StubHeader),
                  msg.size() - sizeof(PhaseTest::StubHeader));
        EXPECT_EQ(ConnectionState::SendingCustomResponse, host.drive(phase)) << "done round " << i;
        EXPECT_EQ(ConnectionState::WaitingForNextCustomMessage, host.completeSend(phase));
    }
}
