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
    int32_t request_no{ 0 };
    FILE* fp{ nullptr };

    // Create Handle
    {
        FileRequestHeader request_header{ FileRequestType::CREATE_HANDLE, ++request_no, 0, client_no };
        client.write(&request_header, sizeof(request_header));

        FileResponseHeader response;
        client.read(&response, sizeof(response));

        auto& response_header = response;
        EXPECT_EQ(response_header.response_no, request_header.request_no);
        EXPECT_EQ(response_header.request_type, request_header.request_type);
    }

    // Create File
    {
        FileRequestHeader request_header{ FileRequestType::CREATE_FILE, ++request_no, sizeof(FileOpenRequestPayload), client_no};
        FileOpenRequestPayload createRequest;
        snprintf(createRequest.filename, sizeof(createRequest.filename), "testfile_%d.txt", client_no);

        printConcurrent("[Server -> Client %d] : Create  File\n", request_header.client_no);
        client.write(&request_header, sizeof(request_header));
        client.write(&createRequest, sizeof(createRequest));

        FileOpenResponse response;
        client.read(&response, sizeof(response));

        auto& response_header = response.header;
        EXPECT_EQ(response_header.response_no, request_header.request_no);
        EXPECT_EQ(response_header.request_type, request_header.request_type);

        fp = response.fp;
    }

    // Sending 10MB Byte
    size_t ten_mb = 10 * 1024 * 1024;
    for (size_t i = 0; i < ten_mb / 4096; i++)
    {
        FileRequestHeader request_header{ FileRequestType::WRITE_FILE, ++request_no, sizeof(FileWriteRequestPayload), client_no };
        FileWriteRequestPayload writeRequest;
        writeRequest.fp = fp;
        writeRequest.length = 4096;

        auto& test_case = test_cases[i % (test_cases.size())];
        memcpy(writeRequest.data, test_case.data(), 4096);

        printConcurrent("[Server -> Client %d] : Write  File (%zu)\n", request_header.client_no, i);
        client.write(&request_header, sizeof(request_header));
        client.write(&writeRequest, sizeof(writeRequest));
    }

    // Close File
    {
        FileRequestHeader request_header{ FileRequestType::CLOSE_FILE, ++request_no, sizeof(FileCloseRequestPayload), client_no };
        FileCloseRequestPayload closeRequest;
        closeRequest.fp = fp;

        printConcurrent("[Server -> Client %d] : Close  File\n", request_header.client_no);
        client.write(&request_header, sizeof(request_header));
        client.write(&closeRequest, sizeof(closeRequest));

        FileCloseResponse response;
        client.read(&response, sizeof(response));

        auto& response_header = response.header;
        EXPECT_EQ(response_header.response_no, request_header.request_no);
        EXPECT_EQ(response_header.request_type, request_header.request_type);
    }

    // Open File
    {
        FileRequestHeader request_header{ FileRequestType::OPEN_FILE, ++request_no, sizeof(FileOpenRequestPayload), client_no };
        FileOpenRequestPayload openRequest;
        snprintf(openRequest.filename, sizeof(openRequest.filename), "testfile_%d.txt", client_no);

        printConcurrent("[Server -> Client %d] : Open  File\n", request_header.client_no);
        client.write(&request_header, sizeof(request_header));
        client.write(&openRequest, sizeof(openRequest));

        FileOpenResponse response;
        client.read(&response, sizeof(response));

        auto& response_header = response.header;
        EXPECT_EQ(response_header.response_no, request_header.request_no);
        EXPECT_EQ(response_header.request_type, request_header.request_type);

        fp = response.fp;
    }

    // Receive 10MB Byte
    for (size_t i = 0; i < ten_mb / 4096; i++)
    {
        FileRequestHeader request_header{ FileRequestType::READ_FILE, ++request_no, sizeof(FileReadRequestPayload), client_no };
        FileReadRequestPayload readRequest;
        readRequest.fp = fp;
        readRequest.length = 4096;

        auto& test_case = test_cases[i % (test_cases.size())];
        
        printConcurrent("[Server -> Client %d] : Read  File (%zu)\n", request_header.client_no, i);
        client.write(&request_header, sizeof(request_header));
        client.write(&readRequest, sizeof(readRequest));

        FileReadResponse response;
        client.read(&response, sizeof(response));

        auto& response_header = response.header;
        EXPECT_EQ(response_header.response_no, request_header.request_no);
        EXPECT_EQ(response_header.request_type, request_header.request_type);

        EXPECT_EQ(response.length, readRequest.length);
        EXPECT_TRUE(memcmp(response.data, test_case.data(), 4096) == 0);
    }

    // Close File
    {
        FileRequestHeader request_header{ FileRequestType::CLOSE_FILE, ++request_no, sizeof(FileCloseRequestPayload), client_no };
        FileCloseRequestPayload closeRequest;
        closeRequest.fp = fp;

        printConcurrent("[Server -> Client %d] : Close  File\n", request_header.client_no);
        client.write(&request_header, sizeof(request_header));
        client.write(&closeRequest, sizeof(closeRequest));

        FileCloseResponse response;
        client.read(&response, sizeof(response));

        auto& response_header = response.header;
        EXPECT_EQ(response_header.response_no, request_header.request_no);
        EXPECT_EQ(response_header.request_type, request_header.request_type);
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