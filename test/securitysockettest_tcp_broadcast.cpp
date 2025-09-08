#include <gtest/gtest.h>

#include <SecuritySocket.hpp>
#include <thread>
#include <random>
#include <utility>

#include "securitysockettest_helper.hpp"



struct BroadcastEventPatterns
{
    static constexpr size_t pattern_length{ 16 };

    std::vector<char> patterns[20];

    BroadcastEventPatterns() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dis('a', 'z');
        for (auto& pattern : patterns)
        {
            pattern.resize(pattern_length);
            for (size_t i = 0; i < pattern_length - 1; i++)
            {
                pattern[i] = (char)dis(gen);
            }
            pattern[pattern_length - 1] = 0;
        }
    }
};


void broadcastServerRoutine(BroadcastEventPatterns* patterns)
{
    using namespace Bn3Monkey;

    SocketConfiguration config{
        "127.0.0.1",
        20000,
        false,
        5,
        1000,
        1000,
        100,
        8192
    };



    SocketBroadcastServer server{ config };

    {
        {
            auto result = server.open(1);
            ASSERT_EQ(SocketCode::SUCCESS, result.code());
        }

        {
            auto result = server.enumerate();
            ASSERT_EQ(SocketCode::SUCCESS, result.code());
        }

        for (size_t i = 0; i < 20; i++)
        {
            auto* buffer = patterns->patterns[i].data();
            printConcurrent("[Server 1 (%zu)] %s\n\n", i, buffer);
            server.write(buffer, BroadcastEventPatterns::pattern_length);
        }
        server.close();
    }

    {
        server.open(1);
        server.enumerate();

        for (size_t i = 0; i < 20; i++)
        {
            auto* buffer = patterns->patterns[i].data();
            printConcurrent("[Server 2 (%zu)] %s\n\n", i, buffer);
            server.write(buffer, BroadcastEventPatterns::pattern_length);
        }
        server.close();
    }

    {
        server.open(1);
        server.enumerate();

        for (size_t i = 0; i < 15; i++)
        {
            auto* buffer = patterns->patterns[i].data();
            printConcurrent("[Server 3 (%zu)] %s\n\n", i, buffer);
            server.write(buffer, BroadcastEventPatterns::pattern_length);
        }
        server.close();
    }
}
void broadcastSingleClientRoutine(BroadcastEventPatterns* patterns)
{
    using namespace Bn3Monkey;

    std::this_thread::sleep_for(std::chrono::seconds(1));

    SocketConfiguration config{
        "127.0.0.1",
        20000,
        false,
        5,
        1000,
        1000,
        100,
        8192
    };

    SocketClient client{ config };

    {
        {
            auto result = client.open();
            ASSERT_EQ(SocketCode::SUCCESS, result.code());
        }
        {
            auto result = client.connect();
            ASSERT_EQ(SocketCode::SUCCESS, result.code());
        }
        for (size_t i = 0; i < 20; i++)
        {
            char buffer[8192]{ 0 };
            auto* expected = patterns->patterns[i].data();
            auto res = client.read(buffer, BroadcastEventPatterns::pattern_length);
            printConcurrent("                    [Client 1 (%zu)] %s\n\n", i, buffer);
            if (res.code() == SocketCode::SUCCESS)
            {
                EXPECT_EQ(SocketCode::SUCCESS, res.code());
                EXPECT_STREQ(expected, buffer);
            }
            else {
                printConcurrent("Error : %s\n", res.message());
            }
        }
        client.close();
    }

    {
        {
            auto result = client.open();
            ASSERT_EQ(SocketCode::SUCCESS, result.code());
        }
        {
            auto result = client.connect();
            ASSERT_EQ(SocketCode::SUCCESS, result.code());
        }
        for (size_t i = 0; i < 15; i++)
        {
            char buffer[8192]{ 0 };
            auto* expected = patterns->patterns[i].data();
            auto res = client.read(buffer, BroadcastEventPatterns::pattern_length);
            printConcurrent("                    [Client 2 (%zu)] %s\n\n", i, buffer);
            if (res.code() == SocketCode::SUCCESS)
            {
                EXPECT_EQ(SocketCode::SUCCESS, res.code());
                EXPECT_STREQ(expected, buffer);
            }
            else {
                printConcurrent("Error : %s\n", res.message());
            }
        }
        client.close();
    }

    {
        {
            auto result = client.open();
            ASSERT_EQ(SocketCode::SUCCESS, result.code());
        }
        {
            auto result = client.connect();
            ASSERT_EQ(SocketCode::SUCCESS, result.code());
        }
        for (size_t i = 0; i < 20; i++)
        {
            char buffer[8192]{ 0 };
            auto* expected = patterns->patterns[i].data();
            auto res = client.read(buffer, BroadcastEventPatterns::pattern_length);
            printConcurrent("                    [Client 3(%zu)] %s\n\n", i, buffer);

            if (res.code() == SocketCode::SUCCESS)
            {

                EXPECT_EQ(SocketCode::SUCCESS, res.code());
                EXPECT_STREQ(expected, buffer);
            }
            else {
                if (i >= 15)
                {
                    printConcurrent("Server disconnected\n");
                    EXPECT_NE(SocketCode::SUCCESS, res.code());
                    break;
                }
                else {
                    printConcurrent("Error : %s\n", res.message());
                }
            }

        }
        client.close();
    }
}



TEST(SecuritySocket, OneClientBroadcastTest)
{

    Bn3Monkey::initializeSecuritySocket();

    BroadcastEventPatterns patterns;
    std::thread server_thread{ broadcastServerRoutine, &patterns };
    std::thread client_thread1{ broadcastSingleClientRoutine, &patterns };

    client_thread1.join();

    server_thread.join();

    Bn3Monkey::releaseSecuritySocket();
}