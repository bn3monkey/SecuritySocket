#include <gtest/gtest.h>

#include <SecuritySocket.hpp>
#include <thread>

#include "securitysockettest_helper.hpp"

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
        8192
    };


    class EchoRequestHandler : public SocketRequestHandler
    {
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

            printf("[Server] Received Data : %s (%llu)", (char *)input_buffer, input_size);
            
            output_size = snprintf((char *)output_buffer, 4096, "echo : %s", (char*)input_buffer);
            return true;
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
}

static void echoClientRoutine(int idx)
{
    using namespace Bn3Monkey;

    std::this_thread::sleep_for(std::chrono::seconds(5));

    SocketConfiguration config{
        "127.0.0.1",
        2000,
        false,
        5,
        5000,
        5000,
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
    switch (idx)
    {
        case 1:
        {
            {
                constexpr char* input_data = "Do you know kimchi?";
                constexpr char* expected_output_data = "echo) Do you know kimchi?";
                const size_t prefix_size = sizeof("echo) ") - 1;


                auto input_length = sprintf(input_buffer, input_data);
                printf("[Client %d] Sent Data : %s (%d)\n", idx, input_data, input_length);
                client.write(input_buffer, input_length);


                client.read(output_buffer, prefix_size);
                ASSERT_EQ(output_buffer, "echo) ");
                printf("[Client %d] Received Data : %s (%d)\n", idx, output_buffer, prefix_size);

                client.read(output_buffer + prefix_size, input_length);
                ASSERT_EQ(output_buffer + prefix_size, input_data);
                printf("[Client %d] Received Data : %s (%d)\n", idx, output_buffer, input_length);

                memset(input_buffer, 0, 256);
                memset(output_buffer, 0, 256);
            }

            {
                constexpr char* input_data = "Do you know psy?";
                constexpr char* expected_output_data = "echo) Do you know psy?";
                const size_t expected_output_data_size = strlen(expected_output_data);
                const size_t prefix_size = sizeof("echo) ") - 1;


                auto input_length = sprintf(input_buffer, input_data);
                printf("[Client %d] Sent Data : %s (%llu)\n", idx, input_data, input_length);
                client.write(input_buffer, input_length);


                client.read(output_buffer, expected_output_data_size);
                ASSERT_EQ(output_buffer, expected_output_data);
                printf("[Client %d] Received Data : %s (%llu)\n", idx, output_buffer, expected_output_data_size);

                memset(input_buffer, 0, 256);
                memset(output_buffer, 0, 256);
            }
        }
        break;
        case 2:
        {
        
        }
        break;
        case 3:
        {

        }
        break;
        case 4:
        {

        }
        break;
    }
}



TEST(SecuritySocket, EchoTest)
{
    SimpleEvent event_obj;

    Bn3Monkey::initializeSecuritySocket();


    std::thread server_thread { echoServerRoutine, &event_obj };

    std::thread client_thread1 { echoClientRoutine, 1 };
    // std::thread client_thread2 { echoClientRoutine, 2 };
    // std::thread client_thread3 { echoClientRoutine, 3 };
    // std::thread client_thread4{ echoClientRoutine, 4 };

    client_thread1.join();
    // client_thread2.join();
    // client_thread3.join();
    // client_thread4.join();

    event_obj.wake();
    server_thread.join();

    Bn3Monkey::releaseSecuritySocket();
}