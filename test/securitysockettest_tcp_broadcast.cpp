#include <gtest/gtest.h>

#include <SecuritySocket.hpp>
#include <thread>
#include <random>
#include <utility>
#include <chrono>
#include <future>

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


// dropAll() forcibly disconnects every active broadcast client. The case it
// exists for: a peer abandons its socket without sending FIN (process exits
// without close, NIC is yanked, peer reconnects on a fresh socket without
// closing the old one). The kernel reports no POLLHUP for those, so the
// accept-monitor cannot reclaim them on its own — only an explicit dropAll()
// frees the slot.
//
// This test simulates the abandoned-socket scenario by intentionally leaking
// the SocketClient instance every round (its destructor would otherwise call
// close() and FIN the server, which is exactly the cleanup signal we need to
// be absent). Each round the client thread calls server.dropAll() to clean
// up server-side state, then opens a fresh SocketClient on the same port to
// verify the server can accept and broadcast to it.
TEST(TCPBroadcast, shouldRecoverViaDropAllWhenClientsAbandonSockets)
{
    using namespace Bn3Monkey;

    BroadcastEventPatterns patterns;
    TimeWatch tw;

    Bn3Monkey::initializeSecuritySocket();

    constexpr uint32_t kPort = 21347;
    constexpr size_t kTrials = 3;

    SocketConfiguration config{
       "127.0.0.1",
       kPort,
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

    // Per-round "received all" signal. SimpleEvent isn't usable here:
    // sleep() resets _is_sleeping=true on entry, so a wake() that arrives
    // before sleep() within a round deadlocks (no further wake will flip
    // the flag back). The existing tests dodge this because client work
    // between rounds (close + open + connect) heavily outweighs server
    // loop overhead, so server reaches sleep() first. In this test the
    // signal fires *at the end* of each round after both threads do
    // ~milliseconds of parallel work (write 200 vs. read 200), so either
    // ordering is possible. std::promise is race-free for single-shot
    // signaling and serves the user-spec'd "client tells server it
    // received everything" purpose.
    std::vector<std::promise<void>> received(kTrials);
    std::vector<std::future<void>> received_f;
    received_f.reserve(kTrials);
    for (auto& p : received) received_f.push_back(p.get_future());

    std::thread _client([&server, &received, &patterns, &tw, kPort, kTrials]() {
        using namespace Bn3Monkey;

        SocketConfiguration config{
            "127.0.0.1",
            kPort,
            false,
            5,
            1000,
            1000,
            100,
            8192
        };

        // Heap-allocate every client and intentionally leak. Letting the
        // SocketClient destructor run would call close() (FIN the server),
        // which is exactly the cleanup signal this test must NOT have —
        // we're verifying that dropAll() can recover the server when peers
        // walk away from their sockets without sending FIN.
        std::vector<SocketClient*> leaked_clients;

        for (size_t trial = 0; trial < kTrials; trial++)
        {
            auto* client = new SocketClient{ config };
            leaked_clients.push_back(client);

            tw.markf("[C] T%zu open begin", trial);
            {
                auto result = client->open();
                ASSERT_EQ(SocketCode::SUCCESS, result.code());
            }
            tw.markf("[C] T%zu connect begin", trial);
            {
                auto result = client->connect();
                ASSERT_EQ(SocketCode::SUCCESS, result.code());
            }
            tw.markf("[C] T%zu connect end", trial);

            for (size_t i = 0; i < BroadcastEventPatterns::NUM_OF_PATTERNS; i++)
            {
                char buffer[8192]{ 0 };
                auto* expected = patterns.patterns[i].data();
                auto res = client->read(buffer, BroadcastEventPatterns::LENGTH_OF_PATTERN);
                tw.markf("[C] T%zu R%03zu done", trial, i);
                if (res.code() == SocketCode::SUCCESS)
                {
                    EXPECT_STREQ(expected, buffer);
                }
                else {
                    printConcurrent("Error : %s\n", res.message());
                }
            }

            tw.markf("[C] T%zu signal received", trial);
            received[trial].set_value();

            // Forcibly disconnect the still-open (no-FIN) client from the
            // server's active list before looping. Without this the server's
            // next await() would return SUCCESS instantly with the stale
            // connection and the next round's writes would target a dead fd.
            tw.markf("[C] T%zu dropAll begin", trial);
            server.dropAll();
            tw.markf("[C] T%zu dropAll end", trial);

            // 'client' is intentionally not deleted; its destructor would
            // close() (FIN) and defeat the abandoned-socket scenario.
        }
    });

    for (size_t trial = 0; trial < kTrials; trial++) {
        tw.markf("[S] T%zu await begin", trial);
        auto await_res = server.await(5000);
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

        tw.markf("[S] T%zu wait received begin", trial);
        received_f[trial].wait();
        tw.markf("[S] T%zu wait received end", trial);

        // Backstop sync: dropAll() runs on the client thread immediately
        // after the received signal. Wait for the active list to actually
        // drain before looping to await(); otherwise await() may race ahead
        // and return SUCCESS on the about-to-be-dropped stale connection.
        tw.markf("[S] T%zu awaitClose begin", trial);
        auto close_res = server.awaitClose(5000);
        tw.markf("[S] T%zu awaitClose end (code=%d)", trial, (int)close_res.code());
        EXPECT_EQ(SocketCode::SUCCESS, close_res.code());
    }

    _client.join();
    server.close();
    tw.dump("shouldRecoverViaDropAllWhenClientsAbandonSockets");
    Bn3Monkey::releaseSecuritySocket();
}
