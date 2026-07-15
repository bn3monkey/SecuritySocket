// Custom-Protocol echo over TLS.
//
// The plaintext twin of this suite is securitysockettest_tcp_request_echo.cpp;
// this one runs the identical exchange through a TLS RequestServer and a TLS
// Client, so what it really proves is that the encrypted transport is
// transparent to the protocol layer — the same framing, the same handler, the
// same responses, with SSL_read/SSL_write underneath.
//
// Everything lives in an anonymous namespace: the plaintext suite declares
// test_patterns / EchoRequestHandler / runEchoClient at file scope with external
// linkage, and both TUs are linked into securitysockettest.

#include <gtest/gtest.h>

#include <SecuritySocket.hpp>

#include <cstring>
#include <thread>
#include <vector>

#include "securitysockettest_helper.hpp"
#include "securitysockettest_tls_fixture.hpp"

using namespace Bn3MonkeyTest;

namespace
{
    // Distinct from the plaintext suite's port so the two can run back to back
    // without a lingering TIME_WAIT listener colliding.
    constexpr uint16_t kTlsEchoPort = 21445;

    const char* kTlsEchoPatterns[] = {
        "Hello, world!",
        "The quick brown fox jumps over the lazy dog.",
        "1234567890",
        "!@#$%^&*()_+-=[]{}|;':,.<>/?`~",
        "한글 테스트 메시지",
        "こんにちは世界",
        "😀😃😄😁😆😅😂🤣",
        "Line1\nLine2\nLine3",
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
        "Mixed123!@#한글ABC"
    };

    struct TlsEchoRequestHeader
    {
        int32_t request_type{ 0 };
        int32_t request_no{ 0 };
        size_t  payload_size{ 0 };
        int32_t client_no{ 0 };

        TlsEchoRequestHeader(int32_t request_type, int32_t request_no,
                             size_t payload_size, int32_t client_no)
            : request_type(request_type), request_no(request_no),
              payload_size(payload_size), client_no(client_no) {}
    };

    struct TlsEchoResponseHeader
    {
        int32_t request_type{ 0 };
        int32_t response_no{ 0 };
        size_t  payload_size{ 0 };
    };

    struct TlsEchoResponse
    {
        TlsEchoResponseHeader header;
        char data[4096]{ 0 };

        TlsEchoResponse() {}
        TlsEchoResponse(TlsEchoResponseHeader header, const char* data, size_t input_size)
            : header(header)
        {
            memcpy(this->data, data, input_size);
        }
    };

    struct TlsEchoRequestHandler : public Bn3Monkey::CustomProtocolRequestHandler
    {
        std::atomic<int> connected{ 0 };
        std::atomic<int> secure_connections{ 0 };

        size_t headerSize() override { return sizeof(TlsEchoRequestHeader); }

        size_t payloadSize(const void* header) override
        {
            return reinterpret_cast<const TlsEchoRequestHeader*>(header)->payload_size;
        }

        Bn3Monkey::RequestProcessingMode classifyMode(const void*) override
        {
            return Bn3Monkey::RequestProcessingMode::FAST;
        }

        void onConnected(const Bn3Monkey::ClientConnection& conn) override
        {
            ++connected;
            // The whole point of the suite: the handler is handed a connection
            // that reports itself encrypted, and it never had to know about TLS.
            if (conn.isSecure()) ++secure_connections;
            printConcurrent("[server] onConnected    ip=%s port=%u secure=%d  (handshake OK)\n",
                            conn.ip(), conn.port(), (int)conn.isSecure());
        }

        void onDisconnected(const Bn3Monkey::ClientConnection& conn) override
        {
            printConcurrent("[server] onDisconnected ip=%s port=%u\n", conn.ip(), conn.port());
        }

        void process(const Bn3Monkey::ClientConnection&,
                     const Bn3Monkey::CustomProtocolRequest& req,
                     Bn3Monkey::CustomProtocolResponse& res) override
        {
            auto* header       = reinterpret_cast<const TlsEchoRequestHeader*>(req.header());
            auto* input_buffer = reinterpret_cast<const char*>(req.payload());
            const size_t input_size = req.payloadLength();

            printConcurrent("[server] process        client=%d req#=%d payload=%zu bytes: \"%.40s\"\n",
                            header->client_no, header->request_no, input_size, input_buffer);

            new (res.data()) TlsEchoResponse{
                { header->request_type, header->request_no, sizeof(TlsEchoResponse) },
                input_buffer, input_size };
            res.setLength(sizeof(TlsEchoResponse));
        }
    };

    Bn3Monkey::NetworkConfiguration echoConfig()
    {
        return Bn3Monkey::NetworkConfiguration{
            "127.0.0.1", kTlsEchoPort, false, 5, 3000, 3000, 100, 8192 };
    }

    void runTlsEchoClient(int32_t client_no)
    {
        using namespace Bn3Monkey;

        Client client{ echoConfig(), makeClientTls() };

        printConcurrent("[client %d] opening + TLS handshake...\n", client_no);
        ASSERT_EQ(NetworkResultCode::SUCCESS, client.open().code());
        ASSERT_EQ(NetworkResultCode::SUCCESS, client.connect().code());
        printConcurrent("[client %d] connected (TLS handshake done)\n", client_no);

        int32_t count{ 0 };
        for (auto* pattern : kTlsEchoPatterns)
        {
            const size_t pattern_len = strlen(pattern);
            TlsEchoRequestHeader request_header{ 0, count++, pattern_len, client_no };
            client.write(&request_header, sizeof(request_header));
            client.write(pattern, pattern_len);

            // readFully, not a bare read: the response spans several TLS records,
            // and SSL_read hands back at most one record's worth per call.
            std::vector<char> buffer(sizeof(TlsEchoResponse));
            ASSERT_EQ(NetworkResultCode::SUCCESS,
                      readFully(client, buffer.data(), buffer.size()).code());

            auto& response = *reinterpret_cast<TlsEchoResponse*>(buffer.data());
            EXPECT_EQ(response.header.response_no, request_header.request_no);
            EXPECT_EQ(response.header.request_type, request_header.request_type);
            EXPECT_STREQ(response.data, pattern);
            printConcurrent("[client %d] req#=%d echoed OK: \"%.40s\"\n",
                            client_no, request_header.request_no, response.data);
        }

        printConcurrent("[client %d] all %d patterns echoed, closing\n",
                        client_no, (int)(sizeof(kTlsEchoPatterns) / sizeof(kTlsEchoPatterns[0])));
        client.close();
    }
}

TEST(TLSRequestEcho, runOneClient)
{
    using namespace Bn3Monkey;
    initializeSecuritySocket();

    ServerCertFiles certs;
    TlsEchoRequestHandler handler;
    RequestServer server{ echoConfig(), makeServerTls(certs) };

    ASSERT_EQ(NetworkResultCode::SUCCESS, server.open(&handler, 4).code());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    runTlsEchoClient(1);

    server.close();

    EXPECT_EQ(1, handler.connected.load());
    EXPECT_EQ(1, handler.secure_connections.load());

    releaseSecuritySocket();
}

TEST(TLSRequestEcho, runFourClient)
{
    using namespace Bn3Monkey;
    initializeSecuritySocket();

    ServerCertFiles certs;
    TlsEchoRequestHandler handler;
    RequestServer server{ echoConfig(), makeServerTls(certs) };

    ASSERT_EQ(NetworkResultCode::SUCCESS, server.open(&handler, 4).code());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Four concurrent handshakes, each followed by ten request/response rounds.
    // The accept loop is single-threaded, so this also pins that one client's
    // handshake cannot stall the others.
    std::thread c1{ runTlsEchoClient, 1 };
    std::thread c2{ runTlsEchoClient, 2 };
    std::thread c3{ runTlsEchoClient, 3 };
    std::thread c4{ runTlsEchoClient, 4 };

    c1.join();
    c2.join();
    c3.join();
    c4.join();

    server.close();

    EXPECT_EQ(4, handler.connected.load());
    EXPECT_EQ(4, handler.secure_connections.load());

    releaseSecuritySocket();
}

// A plaintext client must not be able to talk to a TLS server: its first bytes
// are not a ClientHello, so the handshake fails and the connection is dropped
// without ever reaching the handler.
TEST(TLSRequestEcho, plaintextClientIsRejected)
{
    using namespace Bn3Monkey;
    initializeSecuritySocket();

    ServerCertFiles certs;
    TlsEchoRequestHandler handler;
    RequestServer server{ echoConfig(), makeServerTls(certs) };

    ASSERT_EQ(NetworkResultCode::SUCCESS, server.open(&handler, 4).code());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    {
        Client plain{ echoConfig() };   // no TLS configuration
        ASSERT_EQ(NetworkResultCode::SUCCESS, plain.open().code());
        // The TCP connect itself succeeds — the server accepts the socket and
        // only then discovers the peer is not speaking TLS.
        plain.connect();

        TlsEchoRequestHeader request_header{ 0, 0, 5, 99 };
        plain.write(&request_header, sizeof(request_header));
        plain.write("hello", 5);

        // A bare read() would *succeed* here: OpenSSL answers the malformed
        // ClientHello with a TLS alert record before hanging up, and those alert
        // bytes are perfectly readable bytes. What must never arrive is a
        // response — the request is never parsed, let alone dispatched.
        std::vector<char> buffer(sizeof(TlsEchoResponse));
        auto r = readFully(plain, buffer.data(), buffer.size());
        EXPECT_NE(NetworkResultCode::SUCCESS, r.code())
            << "a plaintext peer received a full response from a TLS server";

        plain.close();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    server.close();

    // onConnected() fires only once the handshake completes, so a peer that
    // never got through it was never announced to the handler.
    EXPECT_EQ(0, handler.connected.load());

    releaseSecuritySocket();
}
