# Security Socket

Security Socket is a simple socket c++ library for supporting TCP and TLS.
It is compatible for Windows(MSVC, MinGW Compiler), Android (Clang), Linux (gcc)

- [Security Socket](#security-socket)
  - [Build](#build)
    - [Option](#option)
  - [Example](#example)
    - [Using Client](#using-client)
    - [Using Request Server](#using-request-server)
    - [Using Notification Server](#using-notification-server)
  - [Speicifcation](#speicifcation)
    - [Recommanded C++ Version](#recommanded-c-version)
    - [Supported Compiler](#supported-compiler)
  - [Revision History](#revision-history)
    - [1.0.0 / 2024.6.16](#100--2024616)
    - [2.0.0 / 2025.02.17](#200--20250217)

## Build

This project is using CMake as main build system.  
You can import this project into your CMake project by utilizing FetchContent.  
So, Please use **CMake version 3.16** or higher

```cmake
cmake_minimum_required (VERSION 3.16)
...

include(FetchContent)
FetchContent_Declear(SecuritySocket
    GIT_REPOSITORY https://github.com/bn3monkey/securitysocket
    GIT_TAG v2.0.0)
FetchContent_MakeAvailable(SecuritySocket)

...

target_include_directories(YourLibrary PRIVATE ${securitysocket_BINARY_DIR}/include)
target_link_libraries(YourLibrary PRIAVATE securitysocket)

```

### Option

You can add option before importing security socket

- **SECURITY_USING_TLS**
  - Include TLS functionality into security socket library.
  - You can lighten this project by switching this option off.
  - If the option is off, OpenSSL resource for supporting TLS is not included in this project.
  - Default Value is *ON*

- **BUILD_SECURITYSOCKET_SHARED**
  - Build security socket as shared library.
  - You can build security socket as static library by switching this option off.
  - Default value is *ON*

- **BUILD_SECURITYSOCKET_TEST**
  - Include security socket project into the whold cmake project. 
  - Default value is *ON*


```cmake
cmake_minimum_required (VERSION 3.16)
...

include(FetchContent)

set(SECURITYSOCKET_USING_TLS OFF CACHE BOOL "Letting Security Socket support TLS functionality" FORCE)
option(BUILD_SECURITYSOCKET_SHARED OFF CACHE BOOL "Build Security socket as shared library" FORCE)
option(BUILD_SECURITYSOCKET_TEST OFF CACHE BOOL "Build Security socket test" FORCE)

FetchContent_Declear(SecuritySocket
    GIT_REPOSITORY https://github.com/bn3monkey/securitysocket
    GIT_TAG v2.0.0)

FetchContent_MakeAvailable(SecuritySocket)

...
```

## Example

### Using Client

```cpp
#include <SecuritySocket.hpp>
#include <cstring>

int main()
{
    initializeSecuritySocket();

    using namespace Bn3Monkey;
    auto configuration = SocketConfiguration("127.0.0.1", 5000, false);
    SocketClient client { configuration };

    {
        auto result = client.open();
        if (result.code() != SocketCode::SUCCESS)
        {
            printf(result.message());
            return -1;
        }
    }

    {
        auto result = client.connect();
        if (result.code() != SocketCode::SUCCESS)
        {
            printf(result.message());
            return -1;
        }
    }

    {
        char buffer[4096] {0};
        strncpy(buffer, "Hello, World!", 4096);
        size_t size = strlen(buffer); 
        auto result = client.write(buffer, size);
        if (result.code() != SocketCode::SUCCESS)
        {
            printf(result.message());
            return -1;
        }
    }    
    {
        char buffer[4096] {0};
        auto result = client.read(buffer, 14);
        if (result.code() != SocketCode::SUCCESS)
        {
            printf(result.messsage());
            return -1;
        }
    }

    client.close();

    releaseSecuritySocket();
    return 0;
}

```

### Using Request Server



```cpp
#include <SecuritySocket.hpp>
#include <cstdio>

int main()
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
        virtual void onClientConnected(const char* ip, int port) override {
            printf("[Sever] Client connected (%s, %d)\n", ip, port);
        }
        virtual void onClientDisconnected(const char* ip, int port) override {
            printf("[Sever] Client disconnected (%s, %d)\n", ip, port);
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

            printf("[Server] Received Data : %s (%zu)\n", (char *)input_buffer, input_size);
            output_size = snprintf((char*)output_buffer, 8192, "echo) %s", (char*)input_buffer);
            return true;
        }
    };
    EchoRequestHandler handler;

    SocketRequestServer server{ config};
    auto result = server.open(&handler, 4);
    if (result.code() != SocketCode::SUCCESS)
        {
            printf(result.message());
            return -1;
        }

    // Sleep Thread

    server.close();
    return;
}
```

### Using Notification Server

```cpp
#include <SecuritySocket.hpp>
#include <cstdio>

int main()
{
    using namespace Bn3Monkey;
    using namespace Bn3Monkey;

    SocketConfiguration config{
        "127.0.0.1",
        20000,
        false
    };

    SocketBroadcastServer server{ config };

    {
        {
            auto result = server.open(1);
            if(SocketCode::SUCCESS != result.code())
            {
                printf("%s", result.message());
            }
        }

        {
            auto result = server.enumerate();
            if(SocketCode::SUCCESS != result.code())
            {
                printf("%s", result.message());
            }
        }

        for (size_t i = 0; i < 20; i++)
        {
            server.write("Event", strlen("Event"));
        }
        server.close();
    }
    return 0;
}
```


## Speicifcation

### Recommanded C++ Version

C++ 14

### Supported Compiler

- Microsoft Visual C++ 2022
- MinGW
- Clang

## Revision History

### 1.0.0 / 2024.6.16

- Initial Release
- Add client supporting both TCP and TLS

### 2.0.0 / 2025.02.17

- Change existing interfaces
- Add request server (it supports only TLS functiionality. not TLS)
- Add notification server (it supports only TLS functiionality. not TLS)
