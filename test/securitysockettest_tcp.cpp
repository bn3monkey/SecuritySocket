#include <gtest/gtest.h>

#include <SecuritySocket.hpp>
#include <thread>

static void serverRoutine()
{
    using namespace Bn3Monkey;

    SocketConfiguration config{
        "127.0.0.1",
        25204,
        false,
        3,
        2000,
        2000,
        8192
    };
    SocketServer server{ config};

    class CustomEventHandler : public SocketEventHandler
    {

    };
}

static void clientRoutine()
{

}



TEST(SecuritySocket, CommunicationTest)
{
    std::thread server_thread { serverRoutine };
    std::thread client_thread { clientRoutine };

    server_thread.join();
    client_thread.join();
}