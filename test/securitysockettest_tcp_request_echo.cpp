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

struct EchoRequestHeader : public Bn3Monkey::SocketRequestHeader
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

    size_t payloadSize() override { return payload_size;  }
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


struct EchoRequestHandler : public Bn3Monkey::SocketRequestHandler
{
    size_t headerSize() override {
        return sizeof(EchoRequestHeader);
    }
    Bn3Monkey::SocketRequestMode onModeClassified(Bn3Monkey::SocketRequestHeader* header) override {
        auto* derived_header = reinterpret_cast<EchoRequestHeader*>(header);
        switch (derived_header->request_type) {
        case 0:
            return Bn3Monkey::SocketRequestMode::FAST;
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

        auto* derived_header = reinterpret_cast<EchoRequestHeader*>(header);
                
        switch (derived_header->request_type) {
        case 0:
            printConcurrent("[Client %d -> Server] : %s\n", derived_header->client_no, input_buffer);
            
            auto* response = new (output_buffer) EchoResponse{ {derived_header->request_type, derived_header->request_no, sizeof(EchoResponse)}, input_buffer, input_size };
            *output_size = sizeof(EchoResponse);
            
            break;
        }
    }

    void onProcessedWithoutResponse(
        Bn3Monkey::SocketRequestHeader* header,
        const char* input_buffer,
        size_t input_size
    ) override {
        return;
    }
};

void runEchoClient(int32_t client_no)
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

    EchoRequestHandler handler;
    SocketRequestServer server{ config };

    std::thread client1{ runEchoClient, 1 };
    std::thread client2{ runEchoClient, 2 };
    std::thread client3{ runEchoClient, 3 };
    std::thread client4{ runEchoClient, 4 };


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