#include <gtest/gtest.h>

#include <SecuritySocket.hpp>
#include <thread>
#include <random>
#include <utility>

#include "securitysockettest_helper.hpp"

const char* test_patterns[] = {
    "Hello, world!",
    "The quick brown fox jumps over the lazy dog.",
    "1234567890",
    "!@#$%^&*()_+-=[]{}|;':,.<>/?`~",
    "한글 테스트 메시지",
    "こんにちは世界",  // 일본어
    "😀😃😄😁😆😅😂🤣",  // 이모지
    "Line1\nLine2\nLine3",
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", // 긴 반복
    "Mixed123!@#한글ABC"
};

struct EchoRequestHeader
{
    int32_t request_type{ 0 };
    int32_t request_no{ 0 };
    size_t payload_size{ 0 };
    int32_t client_no{ 0 };

    EchoRequestHeader(int32_t request_type, int32_t request_no, size_t payload_size, int32_t client_no) :
        request_type(request_type),
        request_no(request_no),
        payload_size(payload_size),
        client_no(client_no) {
    }
};

struct EchoResponseHeader {
    int32_t request_type{ 0 };
    int32_t response_no{ 0 };
    size_t payload_size{ 0 };
};

struct EchoResponse
{
    EchoResponseHeader header;
    char data[4096]{ 0 };

    EchoResponse() {

    }
    EchoResponse(EchoResponseHeader header, const char* data, size_t input_size) : header(header) {
        memcpy(this->data, data, input_size);
    }
};


struct EchoRequestHandler : public Bn3Monkey::CustomProtocolRequestHandler
{
    size_t headerSize() override {
        return sizeof(EchoRequestHeader);
    }
    size_t payloadSize(const void* header) override {
        return reinterpret_cast<const EchoRequestHeader*>(header)->payload_size;
    }
    Bn3Monkey::RequestProcessingMode classifyMode(const void* header) override {
        auto* derived_header = reinterpret_cast<const EchoRequestHeader*>(header);
        switch (derived_header->request_type) {
        case 0:
            return Bn3Monkey::RequestProcessingMode::FAST;
        }
        return Bn3Monkey::RequestProcessingMode::FAST;
    }

    void onConnected(const Bn3Monkey::ClientConnection& conn) override {
        printConcurrent("Client connected (ip : %s port : %u)\n", conn.ip(), conn.port());
    }

    void onDisconnected(const Bn3Monkey::ClientConnection& conn) override {
        printConcurrent("Client disconnected (ip : %s port : %u)\n", conn.ip(), conn.port());
    }

    void process(
        const Bn3Monkey::ClientConnection& conn,
        const Bn3Monkey::CustomProtocolRequest& req,
        Bn3Monkey::CustomProtocolResponse& res
    ) override {
        (void)conn;

        auto* derived_header = reinterpret_cast<const EchoRequestHeader*>(req.header());
        auto* input_buffer   = reinterpret_cast<const char*>(req.payload());
        size_t input_size    = req.payloadLength();

        switch (derived_header->request_type) {
        case 0:
            printConcurrent("[Client %d -> Server] : %s\n", derived_header->client_no, input_buffer);

            new (res.data()) EchoResponse{ {derived_header->request_type, derived_header->request_no, sizeof(EchoResponse)}, input_buffer, input_size };
            res.setLength(sizeof(EchoResponse));

            break;
        }
    }
};

void runEchoClient(int32_t client_no)
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

    int32_t count{ 0 };
    for (auto* pattern : test_patterns) {
        EchoRequestHeader request_header{ 0, count++, strlen(pattern), client_no};
        client.write(&request_header, sizeof(EchoRequestHeader));
        client.write(pattern, strlen(pattern));

        std::vector<char> response_container;
        response_container.resize(sizeof(EchoResponse));

        client.read(response_container.data(), sizeof(EchoResponse));
        auto& response = *reinterpret_cast<EchoResponse*>(response_container.data());

        auto& response_header = response.header;
        EXPECT_EQ(response_header.response_no, request_header.request_no);
        EXPECT_EQ(response_header.request_type, request_header.request_type);

        printConcurrent("[Server -> Client %d] : %s\n", client_no, response.data);
        EXPECT_STREQ(response.data, pattern);
    }

}


TEST(TCPRequestEcho, runFourClient)
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

    EchoRequestHandler handler;
    RequestServer server{ config };

    auto result = server.open(&handler, 4);
    ASSERT_EQ(NetworkResultCode::SUCCESS, result.code());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::thread client1{ runEchoClient, 1 };
    std::thread client2{ runEchoClient, 2 };
    std::thread client3{ runEchoClient, 3 };
    std::thread client4{ runEchoClient, 4 };

    client1.join();
    client2.join();
    client3.join();
    client4.join();
    
    server.close();
    
    releaseSecuritySocket();
    return;
}