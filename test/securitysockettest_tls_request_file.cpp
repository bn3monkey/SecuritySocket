// Custom-Protocol file transfer over TLS (10 MB up, 10 MB back down).
//
// The plaintext twin is securitysockettest_tcp_request_file.cpp. Running the
// same exchange over TLS is what makes this suite worth having: 10 MB does not
// fit in one TLS record, one socket buffer, or one SSL_write, so it is the test
// that actually exercises the parts of the TLS path that a small echo never
// touches —
//   * WRITE_STREAM: thousands of 4 KB frames arriving across record boundaries,
//     which forces the SSL_pending drain loop (a level-triggered epoll does not
//     re-fire for bytes already decrypted inside the SSL object).
//   * READ_STREAM: a 10 MB response, which forces SSL_write to return
//     WANT_WRITE and the connection to re-arm and resume mid-buffer.
//
// The handler (FileRequestHandler) is shared verbatim with the plaintext suite;
// only the client's transport differs. Anonymous namespace because the
// plaintext TU already exports createTestCases / runFileClient.

#include <gtest/gtest.h>

#include <SecuritySocket.hpp>

#include <cstdio>
#include <thread>
#include <vector>

#include "securitysockettest_helper.hpp"
#include "securitysockettest_file_protocol.hpp"
#include "securitysockettest_tls_fixture.hpp"

using namespace Bn3MonkeyTest;

namespace
{
    constexpr uint16_t kTlsFilePort = 21446;

    Bn3Monkey::NetworkConfiguration fileConfig()
    {
        return Bn3Monkey::NetworkConfiguration{
            "127.0.0.1", kTlsFilePort, false, 5, 5000, 5000, 100, 8192 };
    }

    std::vector<std::vector<char>> createTlsTestCases()
    {
        std::vector<std::vector<char>> ret;
        for (char c = 'a'; c <= 'z'; c++)
            ret.push_back(std::vector<char>(4096, c));
        return ret;
    }

    void runTlsFileClient(int32_t client_no)
    {
        using namespace Bn3Monkey;

        Client client{ fileConfig(), makeClientTls() };

        ASSERT_EQ(NetworkResultCode::SUCCESS, client.open().code());
        ASSERT_EQ(NetworkResultCode::SUCCESS, client.connect().code());

        auto test_cases = createTlsTestCases();
        const size_t ten_mb      = 10 * 1024 * 1024;
        const size_t chunk_count = ten_mb / 4096;
        int32_t request_no{ 0 };
        FILE* fp{ nullptr };

        char filename[256];
        snprintf(filename, sizeof(filename), "tlsfile_%d.txt", client_no);

        // ── CREATE_FILE (FAST) — server opens "wb", returns the FILE* ──
        {
            FileRequestHeader request_header{ FileRequestType::CREATE_FILE, ++request_no,
                                              sizeof(FileOpenRequestPayload), client_no };
            FileOpenRequestPayload createRequest;
            snprintf(createRequest.filename, sizeof(createRequest.filename), "%s", filename);

            client.write(&request_header, sizeof(request_header));
            client.write(&createRequest, sizeof(createRequest));

            FileOpenResponse response;
            ASSERT_EQ(NetworkResultCode::SUCCESS,
                      readFully(client, &response, sizeof(response)).code());
            EXPECT_EQ(response.header.response_no, request_header.request_no);
            EXPECT_EQ(response.header.request_type, request_header.request_type);
            fp = response.fp;
        }

        // ── WRITE_BEGIN (WRITE_STREAM entry) ──
        {
            FileRequestHeader begin_header{ FileRequestType::WRITE_BEGIN, ++request_no,
                                            sizeof(WriteBeginPayload), client_no };
            WriteBeginPayload begin{ fp };
            client.write(&begin_header, sizeof(begin_header));
            client.write(&begin, sizeof(begin));
        }

        // ── WRITE_CHUNK × N (response-less; the last chunk ends the stream) ──
        for (size_t i = 0; i < chunk_count; i++)
        {
            const int32_t last = (i == chunk_count - 1) ? 1 : 0;
            FileRequestHeader request_header{ FileRequestType::WRITE_CHUNK, ++request_no,
                                              sizeof(FileWriteRequestPayload), client_no, last };
            FileWriteRequestPayload writeRequest;
            writeRequest.fp     = fp;
            writeRequest.length = 4096;

            auto& test_case = test_cases[i % test_cases.size()];
            memcpy(writeRequest.data, test_case.data(), 4096);

            client.write(&request_header, sizeof(request_header));
            client.write(&writeRequest, sizeof(writeRequest));
        }

        // ── CLOSE_FILE (FAST) — flushes the upload to disk ──
        {
            FileRequestHeader request_header{ FileRequestType::CLOSE_FILE, ++request_no,
                                              sizeof(FileCloseRequestPayload), client_no };
            FileCloseRequestPayload closeRequest{ fp };

            client.write(&request_header, sizeof(request_header));
            client.write(&closeRequest, sizeof(closeRequest));

            FileCloseResponse response;
            ASSERT_EQ(NetworkResultCode::SUCCESS,
                      readFully(client, &response, sizeof(response)).code());
            EXPECT_EQ(response.header.response_no, request_header.request_no);
            EXPECT_EQ(response.header.request_type, request_header.request_type);
        }

        // ── OPEN_FILE (FAST) — reopen "rb", learn the total size ──
        size_t total = 0;
        {
            FileRequestHeader request_header{ FileRequestType::OPEN_FILE, ++request_no,
                                              sizeof(FileOpenRequestPayload), client_no };
            FileOpenRequestPayload openRequest;
            snprintf(openRequest.filename, sizeof(openRequest.filename), "%s", filename);

            client.write(&request_header, sizeof(request_header));
            client.write(&openRequest, sizeof(openRequest));

            FileOpenResponse response;
            ASSERT_EQ(NetworkResultCode::SUCCESS,
                      readFully(client, &response, sizeof(response)).code());
            EXPECT_EQ(response.header.response_no, request_header.request_no);
            EXPECT_EQ(response.header.request_type, request_header.request_type);
            fp    = response.fp;
            total = response.total_size;
        }
        // Every byte we streamed up made it to disk intact.
        EXPECT_EQ(ten_mb, total);

        // ── READ_BEGIN (READ_STREAM entry) — server streams `total` raw bytes ──
        {
            FileRequestHeader request_header{ FileRequestType::READ_BEGIN, ++request_no,
                                              sizeof(ReadBeginPayload), client_no };
            ReadBeginPayload readBegin{ fp, 4096, total };

            client.write(&request_header, sizeof(request_header));
            client.write(&readBegin, sizeof(readBegin));

            std::vector<char> got(total);
            ASSERT_EQ(NetworkResultCode::SUCCESS,
                      readFully(client, got.data(), total).code());

            for (size_t i = 0; i < chunk_count; i++)
            {
                auto& test_case = test_cases[i % test_cases.size()];
                EXPECT_TRUE(memcmp(got.data() + i * 4096, test_case.data(), 4096) == 0)
                    << "mismatch at chunk " << i;
            }
        }

        // ── CLOSE_FILE (FAST) ──
        {
            FileRequestHeader request_header{ FileRequestType::CLOSE_FILE, ++request_no,
                                              sizeof(FileCloseRequestPayload), client_no };
            FileCloseRequestPayload closeRequest{ fp };

            client.write(&request_header, sizeof(request_header));
            client.write(&closeRequest, sizeof(closeRequest));

            FileCloseResponse response;
            ASSERT_EQ(NetworkResultCode::SUCCESS,
                      readFully(client, &response, sizeof(response)).code());
            EXPECT_EQ(response.header.response_no, request_header.request_no);
            EXPECT_EQ(response.header.request_type, request_header.request_type);
        }

        client.close();
        std::remove(filename);
    }
}

TEST(TLSRequestFile, runOneClient)
{
    using namespace Bn3Monkey;
    initializeSecuritySocket();

    ServerCertFiles certs;
    FileRequestHandler handler;
    RequestServer server{ fileConfig(), makeServerTls(certs) };

    ASSERT_EQ(NetworkResultCode::SUCCESS, server.open(&handler, 4).code());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    runTlsFileClient(1);

    server.close();
    releaseSecuritySocket();
}

TEST(TLSRequestFile, runFourClient)
{
    using namespace Bn3Monkey;
    initializeSecuritySocket();

    ServerCertFiles certs;
    FileRequestHandler handler;
    RequestServer server{ fileConfig(), makeServerTls(certs) };

    ASSERT_EQ(NetworkResultCode::SUCCESS, server.open(&handler, 4).code());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 40 MB in flight across four encrypted connections at once.
    std::thread c1{ runTlsFileClient, 1 };
    std::thread c2{ runTlsFileClient, 2 };
    std::thread c3{ runTlsFileClient, 3 };
    std::thread c4{ runTlsFileClient, 4 };

    c1.join();
    c2.join();
    c3.join();
    c4.join();

    server.close();
    releaseSecuritySocket();
}
