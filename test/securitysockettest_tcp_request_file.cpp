#include <gtest/gtest.h>

#include <SecuritySocket.hpp>
#include <thread>
#include <random>
#include <utility>
#include <cstdio>

#include "securitysockettest_helper.hpp"
#include "securitysockettest_file_protocol.hpp"   // FileRequestHandler + structs

std::vector<std::vector<char>> createTestCases() {
    std::vector<std::vector<char>> ret;

    for (char c = 'a'; c <= 'z'; c++) {
        std::vector<char> test_case;
        test_case.reserve(4096);
        for (size_t i = 0; i < 4096; i++)
        {
            test_case.push_back(c);
        }
        ret.push_back(test_case);
    }

    return ret;
}

void runFileClient(int32_t client_no)
{
    using namespace Bn3Monkey;

    NetworkConfiguration config{
        "127.0.0.1",
        21345,
        false,
        5,
        1000,
        1000,
        100,
        8192
    };

    Client client{ config };

    {
        auto ret = client.open();
        ASSERT_EQ(NetworkResultCode::SUCCESS, ret.code());
    }
    {
        auto ret = client.connect();
        ASSERT_EQ(NetworkResultCode::SUCCESS, ret.code());
    }

    auto test_cases = createTestCases();
    const size_t ten_mb = 10 * 1024 * 1024;
    const size_t chunk_count = ten_mb / 4096;
    int32_t request_no{ 0 };
    FILE* fp{ nullptr };

    char filename[256];
    snprintf(filename, sizeof(filename), "testfile_%d.txt", client_no);

    // ── CREATE_FILE (FAST) — server opens "wb", returns the FILE* ──
    {
        FileRequestHeader request_header{ FileRequestType::CREATE_FILE, ++request_no,
                                          sizeof(FileOpenRequestPayload), client_no };
        FileOpenRequestPayload createRequest;
        snprintf(createRequest.filename, sizeof(createRequest.filename), "%s", filename);

        printConcurrent("[Server -> Client %d] : Create File\n", request_header.client_no);
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

    // ── WRITE_CHUNK × N (continuous, response-less; last chunk ends the stream) ──
    for (size_t i = 0; i < chunk_count; i++)
    {
        const int32_t last = (i == chunk_count - 1) ? 1 : 0;
        FileRequestHeader request_header{ FileRequestType::WRITE_CHUNK, ++request_no,
                                          sizeof(FileWriteRequestPayload), client_no, last };
        FileWriteRequestPayload writeRequest;
        writeRequest.fp = fp;
        writeRequest.length = 4096;

        auto& test_case = test_cases[i % (test_cases.size())];
        memcpy(writeRequest.data, test_case.data(), 4096);

        client.write(&request_header, sizeof(request_header));
        client.write(&writeRequest, sizeof(writeRequest));
    }

    // ── CLOSE_FILE (FAST) — flushes the upload to disk ──
    {
        FileRequestHeader request_header{ FileRequestType::CLOSE_FILE, ++request_no,
                                          sizeof(FileCloseRequestPayload), client_no };
        FileCloseRequestPayload closeRequest{ fp };

        printConcurrent("[Server -> Client %d] : Close File (write)\n", request_header.client_no);
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

        printConcurrent("[Server -> Client %d] : Open File\n", request_header.client_no);
        client.write(&request_header, sizeof(request_header));
        client.write(&openRequest, sizeof(openRequest));

        FileOpenResponse response;
        ASSERT_EQ(NetworkResultCode::SUCCESS,
                  readFully(client, &response, sizeof(response)).code());
        EXPECT_EQ(response.header.response_no, request_header.request_no);
        EXPECT_EQ(response.header.request_type, request_header.request_type);
        fp = response.fp;
        total = response.total_size;
    }
    EXPECT_EQ(ten_mb, total);

    // ── READ_BEGIN (READ_STREAM entry) — server streams `total` raw bytes ──
    {
        FileRequestHeader request_header{ FileRequestType::READ_BEGIN, ++request_no,
                                          sizeof(ReadBeginPayload), client_no };
        ReadBeginPayload readBegin{ fp, 4096, total };

        printConcurrent("[Server -> Client %d] : Read Begin\n", request_header.client_no);
        client.write(&request_header, sizeof(request_header));
        client.write(&readBegin, sizeof(readBegin));

        std::vector<char> got(total);
        ASSERT_EQ(NetworkResultCode::SUCCESS,
                  readFully(client, got.data(), total).code());

        for (size_t i = 0; i < chunk_count; i++) {
            auto& test_case = test_cases[i % (test_cases.size())];
            EXPECT_TRUE(memcmp(got.data() + i * 4096, test_case.data(), 4096) == 0)
                << "mismatch at chunk " << i;
        }
    }

    // ── CLOSE_FILE (FAST) ──
    {
        FileRequestHeader request_header{ FileRequestType::CLOSE_FILE, ++request_no,
                                          sizeof(FileCloseRequestPayload), client_no };
        FileCloseRequestPayload closeRequest{ fp };

        printConcurrent("[Server -> Client %d] : Close File (read)\n", request_header.client_no);
        client.write(&request_header, sizeof(request_header));
        client.write(&closeRequest, sizeof(closeRequest));

        FileCloseResponse response;
        ASSERT_EQ(NetworkResultCode::SUCCESS,
                  readFully(client, &response, sizeof(response)).code());
        EXPECT_EQ(response.header.response_no, request_header.request_no);
        EXPECT_EQ(response.header.request_type, request_header.request_type);
    }
}

TEST(TCPRequestFile, runOneClient)
{
    using namespace Bn3Monkey;
    initializeSecuritySocket();


    NetworkConfiguration config{
        "127.0.0.1",
        21345,
        false,
        5,
        1000,
        1000,
        100,
        8192
    };

    FileRequestHandler handler;
    RequestServer server{ config };


    auto result = server.open(&handler, 4);
    ASSERT_EQ(NetworkResultCode::SUCCESS, result.code());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::thread client1{ runFileClient, 1 };

    client1.join();

    server.close();

    releaseSecuritySocket();
    return;
}


TEST(TCPRequestFile, runFourClient)
{
    using namespace Bn3Monkey;
    initializeSecuritySocket();


    NetworkConfiguration config{
        "127.0.0.1",
        21345,
        false,
        5,
        1000,
        1000,
        100,
        8192
    };

    FileRequestHandler handler;
    RequestServer server{ config };



    auto result = server.open(&handler, 4);
    ASSERT_EQ(NetworkResultCode::SUCCESS, result.code());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::thread client1{ runFileClient, 1 };
    std::thread client2{ runFileClient, 2 };
    std::thread client3{ runFileClient, 3 };
    std::thread client4{ runFileClient, 4 };

    client1.join();
    client2.join();
    client3.join();
    client4.join();

    server.close();

    releaseSecuritySocket();
    return;
}