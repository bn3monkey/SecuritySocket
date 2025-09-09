#include <gtest/gtest.h>

#include <SecuritySocket.hpp>
#include <thread>
#include <random>
#include <utility>
#include <cstdio>

#include "securitysockettest_helper.hpp"

enum class FileRequestType : int32_t {
    CREATE_FILE,
    OPEN_FILE,
    READ_FILE,
    WRITE_FILE,
    CLOSE_FILE,
};

struct FileRequestHeader : public Bn3Monkey::SocketRequestHeader
{
    FileRequestType request_type{ FileRequestType::CREATE_FILE };
    int32_t request_no{ 0 };
    size_t payload_size{ 0 };
    int32_t client_no{ 0 };

    FileRequestHeader(FileRequestType request_type, int32_t request_no, size_t payload_size, int32_t client_no) :
        request_type(request_type),
        request_no(request_no),
        payload_size(payload_size),
        client_no(client_no) {
    }

    size_t payloadSize() override { return payload_size; }
};
struct FileOpenRequestPayload
{
    char filename[256]{ 0 };
};
struct FileWriteRequestPayload
{
    FILE* fp{ nullptr };
    size_t length{ 0 };
    char data[4096]{ 0 };
};
struct FileReadRequestPayload
{
    FILE* fp{ nullptr };
    size_t length;
};
struct FileCloseRequestPayload
{
    FILE* fp;
};

struct FileResponseHeader {
    FileRequestType request_type{ FileRequestType::CREATE_FILE };
    int32_t response_no{ 0 };
    size_t payload_size{ 0 };
};

struct FileOpenResponse {
    FileResponseHeader header;
    FILE* fp;
};
struct FileReadResponse
{
    FileResponseHeader header;
    size_t length;
    char data[4096]{ 0 };

    FileReadResponse() {

    }

    FileReadResponse(FileResponseHeader header) : header(header) {
    }
};
struct FileCloseResponse {
    FileResponseHeader header;
};


struct FileRequestHandler : public Bn3Monkey::SocketRequestHandler
{
    size_t headerSize() override {
        return sizeof(FileRequestHeader);
    }
    Bn3Monkey::SocketRequestMode onModeClassified(Bn3Monkey::SocketRequestHeader* header) override {
        auto* derived_header = reinterpret_cast<FileRequestHeader*>(header);
        switch (derived_header->request_type) {
        case FileRequestType::CREATE_FILE:
        case FileRequestType::OPEN_FILE:
        case FileRequestType::CLOSE_FILE:
            return Bn3Monkey::SocketRequestMode::FAST;
        case FileRequestType::READ_FILE:
            return Bn3Monkey::SocketRequestMode::READ_STREAM;
        case FileRequestType::WRITE_FILE:
            return Bn3Monkey::SocketRequestMode::WRITE_STREAM;
        }
        return Bn3Monkey::SocketRequestMode::FAST;
    }

    void onClientConnected(const char* ip, int port) override {
        printConcurrent("Client connected (ip : %s port : %d)\n", ip, port);
    }

    void onClientDisconnected(const char* ip, int port) override {
        printConcurrent("Client disconnected (ip : %s port : %d)\n", ip, port);
    }

    void onProcessed(
        Bn3Monkey::SocketRequestHeader* header,
        const char* input_buffer,
        size_t input_size,
        char* output_buffer,
        size_t* output_size
    ) override {

        auto* derived_header = reinterpret_cast<FileRequestHeader*>(header);

        switch (derived_header->request_type) {
        case FileRequestType::CREATE_FILE:
            {
                printConcurrent("[Client %d -> Server] : Create File\n", derived_header->client_no);

                auto* open_request_payload = reinterpret_cast<const FileOpenRequestPayload*>(input_buffer);
                auto fp = fopen(open_request_payload->filename, "wb");

                auto* response = new (output_buffer) FileOpenResponse{ {derived_header->request_type, derived_header->request_no, sizeof(FileOpenResponse)}, fp };
                *output_size = sizeof(FileOpenResponse);
            }
            break;
        case FileRequestType::OPEN_FILE:
            {
                printConcurrent("[Client %d -> Server] : Open File\n", derived_header->client_no);

                auto* open_request_payload = reinterpret_cast<const FileOpenRequestPayload*>(input_buffer);
                auto fp = fopen(open_request_payload->filename, "rb");

                auto* response = new (output_buffer) FileOpenResponse{ {derived_header->request_type, derived_header->request_no, sizeof(FileOpenResponse)}, fp };
                *output_size = sizeof(FileOpenResponse);
            }
            break;
        case FileRequestType::CLOSE_FILE:
            {
                printConcurrent("[Client %d -> Server] : Close File\n", derived_header->client_no);

                auto* close_request_payload = reinterpret_cast<const FileCloseRequestPayload*>(input_buffer);
                fclose(close_request_payload->fp);

                auto* response = new (output_buffer) FileCloseResponse{ {derived_header->request_type, derived_header->request_no, sizeof(FileCloseResponse)} };
                *output_size = sizeof(FileCloseResponse);
            }
            break;
        case FileRequestType::READ_FILE:
            {
                printConcurrent("[Client %d -> Server] : Read File\n", derived_header->client_no);

                auto* read_request_payload = reinterpret_cast<const FileReadRequestPayload*>(input_buffer);

                auto response = new (output_buffer) FileReadResponse{
                    {derived_header->request_type, derived_header->request_no, sizeof(FileReadResponse)},
                };
                *output_size = sizeof(FileReadResponse);

                response->length = fread(response->data, 1, read_request_payload->length, read_request_payload->fp);

            }
            break;
        }
    }

    void onProcessedWithoutResponse(
        Bn3Monkey::SocketRequestHeader* header,
        const char* input_buffer,
        size_t input_size
    ) override {
        auto* derived_header = reinterpret_cast<FileRequestHeader*>(header);
        switch (derived_header->request_type) {
        case FileRequestType::WRITE_FILE:
            {
                printConcurrent("[Client %d -> Server] : Write  File\n", derived_header->client_no);

                auto* write_request_payload = reinterpret_cast<const FileWriteRequestPayload*>(input_buffer);

                auto length = fwrite(write_request_payload->data, 1, write_request_payload->length, write_request_payload->fp);
            }
            break;
        }
    }
};

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

    SocketConfiguration config{
        "127.0.0.1",
        20000,
        false,
        false,
        5,
        1000,
        1000,
        100,
        8192
    };

    SocketClient client{ config };

    {
        auto ret = client.open();
        ASSERT_EQ(SocketCode::SUCCESS, ret.code());
    }
    {
        auto ret = client.connect();
        ASSERT_EQ(SocketCode::SUCCESS, ret.code());
    }

    auto test_cases = createTestCases();
    int32_t request_no{ 0 };
    FILE* fp{ nullptr };

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


    SocketConfiguration config{
        "127.0.0.1",
        20000,
        false,
        false,
        5,
        1000,
        1000,
        100,
        8192
    };

    FileRequestHandler handler;
    SocketRequestServer server{ config };

    std::thread client1{ runFileClient, 1 };


    auto result = server.open(&handler, 4);
    ASSERT_EQ(SocketCode::SUCCESS, result.code());


    client1.join();

    server.close();

    releaseSecuritySocket();
    return;
}


TEST(TCPRequestFile, runFourClient)
{
    using namespace Bn3Monkey;
    initializeSecuritySocket();


    SocketConfiguration config{
        "127.0.0.1",
        20000,
        false,
        false,
        5,
        1000,
        1000,
        100,
        8192
    };

    FileRequestHandler handler;
    SocketRequestServer server{ config };

    std::thread client1{ runFileClient, 1 };
    std::thread client2{ runFileClient, 2 };
    std::thread client3{ runFileClient, 3 };
    std::thread client4{ runFileClient, 4 };


    auto result = server.open(&handler, 4);
    ASSERT_EQ(SocketCode::SUCCESS, result.code());


    client1.join();
    client2.join();
    client3.join();
    client4.join();

    server.close();

    releaseSecuritySocket();
    return;
}