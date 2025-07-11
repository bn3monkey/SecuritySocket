#include <gtest/gtest.h>

#include <SecuritySocket.hpp>
#include <thread>
#include <random>
#include <utility>

#include "securitysockettest_helper.hpp"

static std::mutex print_mtx;
template<class... Args>
void printConcurrent(const char* format, Args&&... args)
{
    {
        std::lock_guard<std::mutex> lock(print_mtx);
        printf(format, std::forward<Args>(args)...);
    }
}
template<>
void printConcurrent(const char* format)
{
    {
        std::lock_guard<std::mutex> lock(print_mtx);
        printf("%s", format);
    }
}

static void echoServerRoutine(SimpleEvent* event_obj)
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


    class EchoRequestHandler : public SocketRequestHandler
    {
        virtual void onClientConnected(const char* ip, int port) override {
            printConcurrent("[Sever] Client connected (%s, %d)\n", ip, port);
        }
        virtual void onClientDisconnected(const char* ip, int port) override {
            printConcurrent("[Sever] Client disconnected (%s, %d)\n", ip, port);
        }
        virtual ProcessState onDataReceived(const void* input_buffer, size_t offset, size_t read_size) override
        {            
            return ProcessState::READY;
        }
        virtual bool onProcessed(
            const void* input_buffer,
            size_t input_size,
            void* output_buffer,
            size_t& output_size) override
        {

            printConcurrent("[Server] Received Data : %s (%zu)\n", (char *)input_buffer, input_size);
            
            output_size = snprintf((char*)output_buffer, 8192, "echo) %s", (char*)input_buffer);
            return true;
        }
        virtual void onProcessedWithoutResponse(
            const void* input_buffer,
            size_t input_size
        )
        {

            printConcurrent("[Server] Received Data : %s (%zu)\n", (char *)input_buffer, input_size);
            return;
        }
    };
    EchoRequestHandler handler;

    SocketRequestServer server{ config};
    {
        auto result = server.open(&handler, 4);
        ASSERT_EQ(SocketCode::SUCCESS, result.code());
    }

    event_obj->sleep();

    server.close();
    return;
}

static void echoClientRoutine(int idx)
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
        auto result = client.open();
        ASSERT_EQ(SocketCode::SUCCESS, result.code());
    }

    auto result = client.connect();
    ASSERT_EQ(SocketCode::SUCCESS, result.code());

    char input_buffer[8192]{ 0 };
    char output_buffer[8192]{ 0 };
    switch (idx)
    {
        case 1:
        {
            {
                constexpr const char* input_data = "Do you know kimchi?";
                constexpr const char* expected_output_data = "echo) Do you know kimchi?";
                const size_t prefix_size = sizeof("echo) ") - 1;


                auto input_length = sprintf(input_buffer, "%s", input_data);
                printConcurrent("[Client %d] Sent Data : %s (%d)\n", idx, input_data, input_length);
                client.write(input_buffer, input_length);


                client.read(output_buffer, prefix_size);
                ASSERT_STREQ(output_buffer, "echo) ");
                printConcurrent("[Client %d] Received Data : %s (%zu)\n", idx, output_buffer, prefix_size);

                client.read(output_buffer + prefix_size, input_length);
                ASSERT_STREQ(output_buffer + prefix_size, input_data);
                printConcurrent("[Client %d] Received Data : %s (%d)\n", idx, output_buffer, input_length);

                memset(input_buffer, 0, 256);
                memset(output_buffer, 0, 256);
            }

            {
                constexpr const char* input_data = "Do you know psy?";
                constexpr const char* expected_output_data = "echo) Do you know psy?";
                const size_t expected_output_data_size = strlen(expected_output_data);
                const size_t prefix_size = sizeof("echo) ") - 1;


                auto input_length = sprintf(input_buffer, "%s", input_data);
                printConcurrent("[Client %d] Sent Data : %s (%d)\n", idx, input_data, input_length);
                client.write(input_buffer, input_length);


                client.read(output_buffer, expected_output_data_size);
                ASSERT_STREQ(output_buffer, expected_output_data);
                printConcurrent("[Client %d] Received Data : %s (%zu)\n", idx, output_buffer, expected_output_data_size);

                memset(input_buffer, 0, 256);
                memset(output_buffer, 0, 256);
            }
        }
        break;
        case 2:
        {
            {
                constexpr const char* input_data = "Do you know kimchi?";
                constexpr const char* expected_output_data = "echo) Do you know kimchi?";
                const size_t output_data_size = strlen(expected_output_data);


                auto input_length = sprintf(input_buffer, "%s", input_data);
                printConcurrent("[Client %d] Sent Data : %s (%d)\n", idx, input_data, input_length);
                client.write(input_buffer, input_length);

                client.read(output_buffer, output_data_size);
                ASSERT_STREQ(output_buffer, expected_output_data);
                printConcurrent("[Client %d] Received Data : %s (%d)\n", idx, output_buffer, input_length);

                memset(input_buffer, 0, 256);
                memset(output_buffer, 0, 256);
            }

            {
                char input_data[4097]{ 0 };
                for (size_t i = 0; i < 4096; i++)
                {
                    input_data[i] = (char)('A' + (i % 16));
                }
                constexpr const char* prefix = "echo) ";
                size_t prefix_length = strlen(prefix);


                auto input_length = sprintf(input_buffer, "%s", input_data);
                printConcurrent("[Client %d] Sent Data : %s (%d)\n", idx, input_data, input_length);
                client.write(input_buffer, input_length);

                client.read(output_buffer, prefix_length);
                ASSERT_STREQ(output_buffer, "echo) ");
                printConcurrent("[Client %d] Received Data : %s (%zu)\n", idx, output_buffer, prefix_length);

                client.read(output_buffer + prefix_length, 4096);
                ASSERT_STREQ(output_buffer + prefix_length, input_buffer);
                printConcurrent("[Client %d] Received Data : %s (%zu)\n", idx, output_buffer, prefix_length + 4096);

                memset(input_buffer, 0, 256);
                memset(output_buffer, 0, 256);
            }
        }
        break;
        case 3:
        {
            for (int i=0;i<30;i++)
            {
                constexpr const char* input_data = "Do you know kimchi?";
                constexpr const char* expected_output_data = "echo) Do you know kimchi?";
                const size_t output_data_size = strlen(expected_output_data);


                auto input_length = sprintf(input_buffer, "%s", input_data);
                printConcurrent("[Client %d] Sent Data : %s (%d)\n", idx, input_data, input_length);
                client.write(input_buffer, input_length);

                client.read(output_buffer, output_data_size);
                ASSERT_STREQ(output_buffer, expected_output_data);
                printConcurrent("[Client %d] Received Data : %s (%d)\n", idx, output_buffer, input_length);

                memset(input_buffer, 0, 256);
                memset(output_buffer, 0, 256);
            }
        }
        break;
        case 4:
        {
            for (int i=0;i<20;i++)
            {
                constexpr const char* input_data = "Do you know heungmin Son?";
                constexpr const char* expected_output_data = "echo) Do you know heungmin Son?";
                const size_t expected_output_data_size = strlen(expected_output_data);
                const size_t prefix_size = sizeof("echo) ") - 1;


                auto input_length = sprintf(input_buffer, "%s",  input_data);
                printConcurrent("[Client %d] Sent Data : %s (%d)\n", idx, input_data, input_length);
                client.write(input_buffer, input_length);


                client.read(output_buffer, expected_output_data_size);
                ASSERT_STREQ(output_buffer, expected_output_data);
                printConcurrent("[Client %d] Received Data : %s (%zu)\n", idx, output_buffer, expected_output_data_size);

                memset(input_buffer, 0, 256);
                memset(output_buffer, 0, 256);
            }
        }
        break;
        default:
            break;
    }

    client.close();
}

TEST(SecuritySocket, EchoClient)
{
    
    using namespace Bn3Monkey;

    return;

    Bn3Monkey::initializeSecuritySocket();

    SocketConfiguration config{
        "127.0.0.1",
        3000,
        false,
        3,
        1000,
        1000,
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
        printConcurrent("[Client %d] Sent Data : %s (%d)\n", 1, input_data, input_length);
        client.write(input_buffer, input_length);


        client.read(output_buffer, input_length);
        printConcurrent("[Client %d] Received Data : %s (%d)\n", 1, output_buffer, input_length);
        ASSERT_STREQ(output_buffer, input_data);
        memset(input_buffer, 0, 256);
        memset(output_buffer, 0, 256);
    }

    {
        constexpr const char* input_data = "Do you know psy?";


        auto input_length = sprintf(input_buffer, "%s", input_data);
        printConcurrent("[Client %d] Sent Data : %s (%d)\n", 1, input_data, input_length);
        client.write(input_buffer, input_length);


        client.read(output_buffer, input_length);
        printConcurrent("[Client %d] Received Data : %s (%d)\n", 1, output_buffer, input_length);
        ASSERT_STREQ(output_buffer, input_data);
        memset(input_buffer, 0, 256);
        memset(output_buffer, 0, 256);
    }

    {
        constexpr const char* input_data = "Can you speak english";


        auto input_length = sprintf(input_buffer, "%s", input_data);
        printConcurrent("[Client %d] Sent Data : %s (%d)\n", 1, input_data, input_length);
        client.write(input_buffer, input_length);


        client.read(output_buffer, input_length);
        printConcurrent("[Client %d] Received Data : %s (%d)\n", 1, output_buffer, input_length);
        ASSERT_STREQ(output_buffer, input_data);
        memset(input_buffer, 0, 256);
        memset(output_buffer, 0, 256);
    }

    Bn3Monkey::releaseSecuritySocket();
}

TEST(SecuritySocket, EchoTest)
{

    SimpleEvent event_obj;

    Bn3Monkey::initializeSecuritySocket();

    std::thread server_thread{ echoServerRoutine, &event_obj };
    std::thread client_thread1{ echoClientRoutine, 1 };
    std::thread client_thread2 { echoClientRoutine, 2 };
    std::thread client_thread3 { echoClientRoutine, 3 };
    std::thread client_thread4{ echoClientRoutine, 4 };

    client_thread1.join();
    client_thread2.join();
    client_thread3.join();
    client_thread4.join();

    event_obj.wake();
    server_thread.join();

    Bn3Monkey::releaseSecuritySocket();
}


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