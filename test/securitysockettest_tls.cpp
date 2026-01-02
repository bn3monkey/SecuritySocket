#include <gtest/gtest.h>

#include "securitysockettest.hpp"
#include <SecuritySocket.hpp>
#include <cstring>

TEST(SecuritySocket, TLSClient)
{
	return;
    
    using namespace Bn3Monkey;

    Bn3Monkey::initializeSecuritySocket();

    SocketConfiguration config{
        "192.168.0.98",
        443,
        true,
        false,
        3,
        3000,
        3000,
        8192
    };

    SocketClient client{ config };
    {
        auto result = client.open();
        ASSERT_EQ(SocketCode::SUCCESS, result.code());
    }
    {
        auto result = client.connect();
        ASSERT_EQ(SocketCode::SUCCESS, result.code());
    }

    char input_buffer[256]{ 0 };
    char output_buffer[256]{ 0 };

    {
        constexpr const char* input_data = "Do you know kimchi?";


        auto input_length = sprintf(input_buffer, "%s", input_data);
        printf("[Client %d] Sent Data : %s (%d)\n", 1, input_data, input_length);
        client.write(input_buffer, input_length);


        client.read(output_buffer, input_length);
        printf("[Client %d] Received Data : %s (%d)\n", 1, output_buffer, input_length);
        ASSERT_STREQ(output_buffer, input_data);
        memset(input_buffer, 0, 256);
        memset(output_buffer, 0, 256);
    }

    {
        constexpr const char* input_data = "Do you know psy?";


        auto input_length = sprintf(input_buffer, "%s", input_data);
        printf("[Client %d] Sent Data : %s (%d)\n", 1, input_data, input_length);
        client.write(input_buffer, input_length);


        client.read(output_buffer, input_length);
        printf("[Client %d] Received Data : %s (%d)\n", 1, output_buffer, input_length);
        ASSERT_STREQ(output_buffer, input_data);
        memset(input_buffer, 0, 256);
        memset(output_buffer, 0, 256);
    }

    {
        constexpr const char* input_data = "Can you speak english";


        auto input_length = sprintf(input_buffer, "%s", input_data);
        printf("[Client %d] Sent Data : %s (%d)\n", 1, input_data, input_length);
        client.write(input_buffer, input_length);


        client.read(output_buffer, input_length);
        printf("[Client %d] Received Data : %s (%d)\n", 1, output_buffer, input_length);
        ASSERT_STREQ(output_buffer, input_data);
        memset(input_buffer, 0, 256);
        memset(output_buffer, 0, 256);
    }

    Bn3Monkey::releaseSecuritySocket();
}