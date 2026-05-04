#include <gtest/gtest.h>

#include <SecuritySocket.hpp>
#include <thread>
#include <random>
#include <utility>
#include <chrono>

#include "securitysockettest_helper.hpp"



struct BroadcastEventPatterns
{
    static constexpr size_t NUM_OF_PATTERNS{ 200 };
    static constexpr size_t LENGTH_OF_PATTERN{ 1024 };

    std::vector<char> patterns[NUM_OF_PATTERNS];

    BroadcastEventPatterns() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dis('a', 'z');
        for (auto& pattern : patterns)
        {
            pattern.resize(LENGTH_OF_PATTERN + 1);
            for (size_t i = 0; i < LENGTH_OF_PATTERN; i++)
            {
                pattern[i] = (char)dis(gen);
            }
            pattern[LENGTH_OF_PATTERN] = 0;
        }
    }
};

// Prints connect/disconnect events from the broadcast server's accept-monitor,
// and (optionally) records the same events into a shared TimeWatch so they
// line up with the server/client write/read marks in the final timeline.
struct PrintingBroadcastHandler : public Bn3Monkey::SocketBroadcastHandler
{
    TimeWatch* tw{ nullptr };
    explicit PrintingBroadcastHandler(TimeWatch* tw_ = nullptr) : tw(tw_) {}

    void onClientConnected(const char* ip, int port) override {
        if (tw) tw->markf("[H] onClientConnected %s:%d", ip, port);
        printConcurrent("[Server] client connected    %s:%d\n", ip, port);
    }
    void onClientDisconnected(const char* ip, int port) override {
        if (tw) tw->markf("[H] onClientDisconnected %s:%d", ip, port);
        printConcurrent("[Server] client disconnected %s:%d\n", ip, port);
    }
};


// Repeated connect / disconnect cycles. write() skips when no client is
// connected, so the test uses SimpleEvent to make sure the client has finished
// connect() before the server starts writing each trial.
TEST(TCPBroadcast, shouldHandleRepeatedClientConnectionsAndDisconnections)
{
    using namespace Bn3Monkey;

    BroadcastEventPatterns patterns;
    TimeWatch tw;

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
    // Hoisted out of the inner brace so the accept-monitor doesn't dereference
    // a destroyed handler when a client connects later in the test.
    PrintingBroadcastHandler handler{ &tw };

    {
        auto result = server.open(&handler, 1);
        ASSERT_EQ(SocketCode::SUCCESS, result.code());
    }

    SimpleEvent event;
    std::thread _client([&event, &patterns, &tw]() {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        tw.mark("[C] startup sleep done");

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
            tw.markf("[C] T%zu open begin", trial);
            {
                auto result = client.open();
                ASSERT_EQ(SocketCode::SUCCESS, result.code());
            }
            tw.markf("[C] T%zu connect begin", trial);
            {
                auto result = client.connect();
                ASSERT_EQ(SocketCode::SUCCESS, result.code());
            }
            tw.markf("[C] T%zu connect end", trial);
            event.wake();

            for (size_t i = 0; i < BroadcastEventPatterns::NUM_OF_PATTERNS; i++)
            {
                char buffer[8192]{ 0 };
                auto* expected = patterns.patterns[i].data();
                auto res = client.read(buffer, BroadcastEventPatterns::LENGTH_OF_PATTERN);
                tw.markf("[C] T%zu R%03zu done", trial, i);
                if (res.code() == SocketCode::SUCCESS)
                {
                    EXPECT_STREQ(expected, buffer);
                }
                else {
                    printConcurrent("Error : %s\n", res.message());
                }
            }
            tw.markf("[C] T%zu close begin", trial);
            client.close();
            tw.markf("[C] T%zu close end", trial);
        }
    });


    for (size_t trial = 0; trial < 3; trial++) {
        tw.markf("[S] T%zu event.sleep begin", trial);
        event.sleep();
        tw.markf("[S] T%zu event.sleep end", trial);

        for (size_t i = 0; i < BroadcastEventPatterns::NUM_OF_PATTERNS; i++)
        {
            auto* buffer = patterns.patterns[i].data();
            auto t0 = std::chrono::steady_clock::now();
            server.write(buffer, BroadcastEventPatterns::LENGTH_OF_PATTERN);
            auto t1 = std::chrono::steady_clock::now();
            auto dur_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            tw.markf("[S] T%zu W%03zu done (%.3fms)", trial, i, dur_ms);
        }
    }

    _client.join();
    server.close();
    tw.dump("shouldHandleRepeatedClientConnectionsAndDisconnections");
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
    TimeWatch tw;

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
    PrintingBroadcastHandler handler{ &tw };

    {
        auto result = server.open(&handler, 1);
        ASSERT_EQ(SocketCode::SUCCESS, result.code());
    }

    std::thread _client([&patterns, &tw]() {
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
            tw.markf("[C] T%zu open begin", trial);
            {
                auto result = client.open();
                ASSERT_EQ(SocketCode::SUCCESS, result.code());
            }
            tw.markf("[C] T%zu connect begin", trial);
            {
                auto result = client.connect();
                ASSERT_EQ(SocketCode::SUCCESS, result.code());
            }
            tw.markf("[C] T%zu connect end", trial);

            for (size_t i = 0; i < BroadcastEventPatterns::NUM_OF_PATTERNS; i++)
            {
                char buffer[8192]{ 0 };
                auto* expected = patterns.patterns[i].data();
                auto res = client.read(buffer, BroadcastEventPatterns::LENGTH_OF_PATTERN);
                tw.markf("[C] T%zu R%03zu done", trial, i);
                if (res.code() == SocketCode::SUCCESS)
                {
                    EXPECT_STREQ(expected, buffer);
                }
                else {
                    printConcurrent("Error : %s\n", res.message());
                }
            }
            tw.markf("[C] T%zu close begin", trial);
            client.close();
            tw.markf("[C] T%zu close end", trial);
        }
    });


    for (size_t trial = 0; trial < 3; trial++) {
        tw.markf("[S] T%zu await begin", trial);
        auto await_res = server.await(5000);
        // std::this_thread::sleep_for(std::chrono::milliseconds(50));
        tw.markf("[S] T%zu await end (code=%d)", trial, (int)await_res.code());
        ASSERT_EQ(SocketCode::SUCCESS, await_res.code());

        for (size_t i = 0; i < BroadcastEventPatterns::NUM_OF_PATTERNS; i++)
        {
            auto* buffer = patterns.patterns[i].data();
            auto t0 = std::chrono::steady_clock::now();
            server.write(buffer, BroadcastEventPatterns::LENGTH_OF_PATTERN);
            auto t1 = std::chrono::steady_clock::now();
            auto dur_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            tw.markf("[S] T%zu W%03zu done (%.3fms)", trial, i, dur_ms);
        }

        tw.markf("[S] T%zu awaitClose begin", trial);
        auto close_res = server.awaitClose(5000);
        tw.markf("[S] T%zu awaitClose end (code=%d)", trial, (int)close_res.code());
        EXPECT_EQ(SocketCode::SUCCESS, close_res.code());
    }

    _client.join();
    server.close();
    tw.dump("shouldSynchronizeViaAwaitAndAwaitClose");
    Bn3Monkey::releaseSecuritySocket();
}
