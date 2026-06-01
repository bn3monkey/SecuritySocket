// StagingBuffer unit tests — focus on compact() correctness under the
// pipelined drain/reserve cycle that the Custom-protocol read path relies on.
//
// Regression guard: compact() must memmove the live window [_sent, _received)
// from head() (= _data + _sent), NOT from _data + pending(). The latter copies
// the wrong region and silently corrupts pipelined bytes, which surfaced as the
// TCPRequestFile post-WRITE-stream desync (CLOSE/OPEN response mismatch).

#include <gtest/gtest.h>

#include "../src/implementation/core/memory/buffer.hpp"

#include <cstring>
#include <vector>

using Bn3Monkey::StagingBuffer;

namespace
{
    // Fill the tail with a deterministic byte pattern (value = global offset).
    void fillPattern(StagingBuffer& buf, size_t n, unsigned char start)
    {
        auto* p = static_cast<unsigned char*>(buf.tail());
        for (size_t i = 0; i < n; ++i) p[i] = static_cast<unsigned char>(start + i);
        buf.fill(n);
    }
}

// compact() with _sent in the middle must preserve the live window exactly.
TEST(StagingBuffer, CompactPreservesLiveWindow)
{
    StagingBuffer buf(256);

    // Write 100 bytes [0..100), consume the first 60. Live = bytes valued 60..99.
    fillPattern(buf, 100, /*start=*/0);
    buf.drain(60);
    ASSERT_EQ(buf.pending(), 40u);

    buf.compact();

    ASSERT_EQ(buf.sent(), 0u);
    ASSERT_EQ(buf.pending(), 40u);
    const auto* head = static_cast<const unsigned char*>(buf.head());
    for (size_t i = 0; i < 40; ++i) {
        EXPECT_EQ(head[i], static_cast<unsigned char>(60 + i))
            << "live byte " << i << " corrupted by compact()";
    }
}

// reserve() that cannot fit at the tail must compact first and keep the
// partial (un-consumed) message intact — the exact pipeline-boundary case.
TEST(StagingBuffer, ReserveCompactsKeepingPartialMessage)
{
    StagingBuffer buf(128);

    // Fill to capacity, consume all but a 24-byte trailing partial header.
    fillPattern(buf, 128, /*start=*/0);
    buf.drain(104);
    ASSERT_EQ(buf.pending(), 24u);
    ASSERT_EQ(buf.remaining(), 0u);

    // Ask for room the tail can't give → reserve() must reclaim via compact().
    ASSERT_TRUE(buf.reserve(64));
    ASSERT_GE(buf.remaining(), 64u);
    ASSERT_EQ(buf.pending(), 24u);

    const auto* head = static_cast<const unsigned char*>(buf.head());
    for (size_t i = 0; i < 24; ++i) {
        EXPECT_EQ(head[i], static_cast<unsigned char>(104 + i))
            << "partial-message byte " << i << " corrupted by reserve()/compact()";
    }
}

// Multi-round drain/fill/compact: emulate consuming fixed-size messages out of
// a stream whose chunk size doesn't divide the message size, so a partial
// always rides across the compaction boundary.
TEST(StagingBuffer, PipelinedMessagesSurviveRepeatedCompaction)
{
    constexpr size_t kMsg = 40;
    StagingBuffer buf(100);

    unsigned char next_write = 0;   // value stamped on the next byte appended
    unsigned char next_read  = 0;   // value expected on the next byte consumed

    for (int round = 0; round < 50; ++round) {
        // Top up the tail with as many pattern bytes as fit (compact if needed).
        if (buf.remaining() == 0) ASSERT_TRUE(buf.reserve(kMsg));
        const size_t room = buf.remaining();
        auto* p = static_cast<unsigned char*>(buf.tail());
        for (size_t i = 0; i < room; ++i) p[i] = static_cast<unsigned char>(next_write++);
        buf.fill(room);

        // Consume whole messages, verifying every byte is the value we wrote.
        while (buf.pending() >= kMsg) {
            const auto* h = static_cast<const unsigned char*>(buf.head());
            for (size_t i = 0; i < kMsg; ++i) {
                ASSERT_EQ(h[i], next_read++)
                    << "round " << round << " message byte " << i << " corrupted";
            }
            buf.drain(kMsg);
        }
    }
}
