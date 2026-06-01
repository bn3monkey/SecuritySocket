// SniffPhase unit tests — the protocol-detection wait point.
//
// SniffPhase peeks at head()..pending() WITHOUT draining (carry-over: the
// chosen HTTP/Custom phase re-reads the same bytes), and maps the verdict to a
// ConnectionState: HTTP -> ReceivingHttpRequest, CUSTOM -> ReceivingCustomMessage,
// NEED_MORE -> stay Sniffing. Each test feeds 100+ varied inputs and asserts the
// state per input (fresh input each iteration since sniff never drains).

#include <gtest/gtest.h>

#include "securitysockettest_phase_host.hpp"
#include "../src/implementation/connection/SniffPhase.hpp"

#include <string>

using namespace Bn3Monkey;
using PhaseTest::FakePhaseHost;

namespace
{
    constexpr int kRounds = 120;   // > 100 inputs per case
    const char* const kMethods[] = { "GET", "POST", "PUT", "DELETE",
                                     "OPTIONS", "HEAD", "PATCH", "CONNECT" };
}

// A complete "<METHOD> ..." request line is detected as HTTP.
TEST(SniffPhase, HttpRequestLinesDetectedAsHttp)
{
    FakePhaseHost host;
    SniffPhase    sniff;

    for (int i = 0; i < kRounds; ++i) {
        const char* method = kMethods[i % 8];
        std::string req = std::string(method) + " /path/" + std::to_string(i) +
                          " HTTP/1.1\r\nHost: x\r\n\r\n";
        host.clearInput();
        host.feed(req.data(), req.size());

        EXPECT_EQ(ConnectionState::ReceivingHttpRequest, host.drive(sniff))
            << "round " << i << " method " << method;
        // sniff must NOT drain — the HTTP phase re-reads these bytes.
        EXPECT_EQ(req.size(), host.inputPending());
    }
}

// Binary headers (Custom Protocol) never begin with "<METHOD> ", so CUSTOM.
TEST(SniffPhase, BinaryHeadersDetectedAsCustom)
{
    FakePhaseHost host;
    SniffPhase    sniff;

    for (int i = 0; i < kRounds; ++i) {
        // First byte is a small/high binary value, never an uppercase letter,
        // so it cannot be the start of an HTTP method token.
        char hdr[8];
        hdr[0] = static_cast<char>(i % 0x1F);       // 0x00..0x1E control range
        for (int b = 1; b < 8; ++b) hdr[b] = static_cast<char>(0x80 | (i + b));
        host.clearInput();
        host.feed(hdr, sizeof(hdr));

        EXPECT_EQ(ConnectionState::ReceivingCustomMessage, host.drive(sniff))
            << "round " << i;
        EXPECT_EQ(sizeof(hdr), host.inputPending());
    }
}

// A strict prefix of a method token is still ambiguous -> stay Sniffing.
TEST(SniffPhase, AmbiguousPrefixesStaySniffing)
{
    FakePhaseHost host;
    SniffPhase    sniff;

    // Prefixes of real method tokens, none followed by a space yet.
    const char* const prefixes[] = { "G", "GE", "P", "PU", "PO", "PA",
                                     "D", "DE", "O", "OP", "OPTION", "H",
                                     "C", "CO", "CONNEC" };
    const int n = static_cast<int>(sizeof(prefixes) / sizeof(prefixes[0]));

    for (int i = 0; i < kRounds; ++i) {
        const char* p = prefixes[i % n];
        host.clearInput();
        host.feed(p, std::strlen(p));

        EXPECT_EQ(ConnectionState::Sniffing, host.drive(sniff))
            << "round " << i << " prefix '" << p << "'";
    }
}
