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


// Repeated connect / disconnect cycles. write() skips when no client is
// connected, so the test uses SimpleEvent to make sure the client has finished
// connect() before the server starts writing each trial.
TEST(TCPBroadcast, shouldHandleRepeatedClientConnectionsAndDisconnections)
{
    using namespace Bn3Monkey;

    BroadcastEventPatterns patterns;

    Bn3Monkey::initializeSecuritySocket();

    SocketConfiguration config{
       "127.0.0.1",
       21345,
       false,
       5,
       1000,
       1000,
       100,
       8192
    };

    SocketBroadcastServer server{ config };

    {
        auto result = server.open(1);
        ASSERT_EQ(SocketCode::SUCCESS, result.code());
    }

    SimpleEvent event;
    std::thread _client([&event, &patterns]() {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        SocketConfiguration config{
            "127.0.0.1",
            21345,
            false,
            5,
            1000,
            1000,
            100,
            8192
        };

        SocketClient client{ config };

        for (size_t trial = 0; trial < 3; trial++)
        {
            {
                auto result = client.open();
                ASSERT_EQ(SocketCode::SUCCESS, result.code());
            }
            {
                auto result = client.connect();
                ASSERT_EQ(SocketCode::SUCCESS, result.code());
            }
            event.wake();

            for (size_t i = 0; i < 20; i++)
            {
                char buffer[8192]{ 0 };
                auto* expected = patterns.patterns[i].data();
                auto res = client.read(buffer, BroadcastEventPatterns::pattern_length);
                printConcurrent("                    [Client %zu (%zu)] %s\n\n", trial, i, buffer);
                if (res.code() == SocketCode::SUCCESS)
                {
                    EXPECT_STREQ(expected, buffer);
                }
                else {
                    printConcurrent("Error : %s\n", res.message());
                }
            }
            client.close();
        }
    });


    for (size_t trial = 0; trial < 3; trial++) {
        event.sleep();

        for (size_t i = 0; i < 20; i++)
        {
            auto* buffer = patterns.patterns[i].data();
            printConcurrent("[Server %zu (%zu)] %s\n\n", trial, i, buffer);
            server.write(buffer, BroadcastEventPatterns::pattern_length);
        }
    }

    _client.join();
    server.close();
    Bn3Monkey::releaseSecuritySocket();
}


// Same connect / disconnect cycle, but synchronization is owned by the server:
//   server.await(t)      blocks until a healthy client is connected.
//   server.write(...)    sends to currently-active clients.
//   server.awaitClose(t) blocks until the round's clients have all closed,
//                        forming an explicit barrier before the next await().
// The client side has zero knowledge of trial boundaries — it just opens /
// connects / reads / closes in a loop. SimpleEvent is not used.
TEST(TCPBroadcast, shouldSynchronizeViaAwaitAndAwaitClose)
{
    using namespace Bn3Monkey;

    BroadcastEventPatterns patterns;

    Bn3Monkey::initializeSecuritySocket();

    SocketConfiguration config{
       "127.0.0.1",
       21346,
       false,
       5,
       1000,
       1000,
       100,
       8192
    };

    SocketBroadcastServer server{ config };

    {
        auto result = server.open(1);
        ASSERT_EQ(SocketCode::SUCCESS, result.code());
    }

    std::thread _client([&patterns]() {
        SocketConfiguration config{
            "127.0.0.1",
            21346,
            false,
            5,
            1000,
            1000,
            100,
            8192
        };

        SocketClient client{ config };

        for (size_t trial = 0; trial < 3; trial++)
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
                auto* expected = patterns.patterns[i].data();
                auto res = client.read(buffer, BroadcastEventPatterns::pattern_length);
                printConcurrent("                    [Client %zu (%zu)] %s\n\n", trial, i, buffer);
                if (res.code() == SocketCode::SUCCESS)
                {
                    EXPECT_STREQ(expected, buffer);
                }
                else {
                    printConcurrent("Error : %s\n", res.message());
                }
            }
            client.close();
        }
    });


    for (size_t trial = 0; trial < 3; trial++) {
        auto await_res = server.await(5000);
        ASSERT_EQ(SocketCode::SUCCESS, await_res.code());

        for (size_t i = 0; i < 20; i++)
        {
            auto* buffer = patterns.patterns[i].data();
            printConcurrent("[Server %zu (%zu)] %s\n\n", trial, i, buffer);
            server.write(buffer, BroadcastEventPatterns::pattern_length);
        }

        auto close_res = server.awaitClose(5000);
        EXPECT_EQ(SocketCode::SUCCESS, close_res.code());
    }

    _client.join();
    server.close();
    Bn3Monkey::releaseSecuritySocket();
}
