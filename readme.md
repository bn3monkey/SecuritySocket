# Security Socket

Security Socket is a simple socket c++ library for supporting TCP and TLS.
It is compatible for Windows(MSVC, MinGW Compiler), Android (Clang), Linux (gcc)

- [Security Socket](#security-socket)
  - [Build](#build)
    - [Option](#option)
  - [Example](#example)
    - [Using Client](#using-client)
    - [Using TLS Client](#using-tls-client)
    - [Using Request Server](#using-request-server)
    - [Using TLS Request Server](#using-tls-request-server)
    - [Using Notification Server](#using-notification-server)
    - [Using TLS Notification Server](#using-tls-notification-server)
  - [TLS Configuration](#tls-configuration)
    - [SocketTLSVersion](#sockettlsversion)
    - [SocketTLS1\_2CipherSuite](#sockettls1_2ciphersuite)
    - [SocketTLS1\_3CipherSuite](#sockettls1_3ciphersuite)
    - [SocketTLSClientAuthenticationMode](#sockettlsclientauthenticationmode)
    - [SocketTLSClientConfiguration](#sockettlsclientconfiguration)
      - [TLS Event Callback](#tls-event-callback)
    - [SocketTLSServerConfiguration](#sockettlsserverconfiguration)
  - [Specification](#specification)
    - [Recommended C++ Version](#recommended-c-version)
    - [Supported Compiler](#supported-compiler)
  - [Revision History](#revision-history)
    - [1.0.0 / 2024.6.16](#100--2024616)
    - [2.0.0 / 2025.02.17](#200--20250217)
    - [2.0.1 / 2025.02.18](#201--20250218)
    - [2.0.2 / 2025.03.19](#202--20250319)
    - [2.0.3 / 2025.07.02](#203--20250702)
    - [2.0.4 / 2025.08.11](#204--20250811)
    - [2.0.5 / 2025.08.11](#205--20250811)
    - [2.0.6 / 2025.09.04](#206--20250904)
    - [2.1.0 / 2025.09.09](#210--20250909)
    - [2.1.1 / 2025.09.09](#211--20250909)
    - [2.1.2 / 2026.01.02](#212--20260102)
    - [2.2.0 / 2026.02.25](#220--20260225)

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
    GIT_TAG v2.2.0)
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
  - Default Value is _ON_

- **BUILD_SECURITYSOCKET_SHARED**

  - Build security socket as shared library.
  - You can build security socket as static library by switching this option off.
  - Default value is _ON_

- **BUILD_SECURITYSOCKET_TEST**
  - Include security socket project into the whold cmake project.
  - Default value is _ON_

```cmake
cmake_minimum_required (VERSION 3.16)
...

include(FetchContent)

set(SECURITYSOCKET_USING_TLS OFF CACHE BOOL "Letting Security Socket support TLS functionality" FORCE)
option(BUILD_SECURITYSOCKET_SHARED OFF CACHE BOOL "Build Security socket as shared library" FORCE)
option(BUILD_SECURITYSOCKET_TEST OFF CACHE BOOL "Build Security socket test" FORCE)

FetchContent_Declear(SecuritySocket
    GIT_REPOSITORY https://github.com/bn3monkey/securitysocket
    GIT_TAG v2.2.0)

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
            printf(result.message());
            return -1;
        }
    }

    client.close();

    releaseSecuritySocket();
    return 0;
}

```

### Using TLS Client

```cpp
#include <SecuritySocket.hpp>
#include <cstring>

int main()
{
    initializeSecuritySocket();

    using namespace Bn3Monkey;

    SocketConfiguration config{ "127.0.0.1", 5000 };

    // TLS 1.2/1.3 with server certificate verification and mutual TLS (mTLS)
    SocketTLSClientConfiguration tls_config{
        { SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3 },    // supported TLS versions
        { SocketTLS1_2CipherSuite::ECDHE_RSA_AES256_GCM_SHA384 },  // TLS 1.2 cipher suites
        { SocketTLS1_3CipherSuite::TLS_AES_256_GCM_SHA384 },       // TLS 1.3 cipher suites
        true,                    // verify server certificate
        true,                    // verify hostname
        "/path/to/ca.crt",       // CA certificate (trust store) path
        true,                    // use client certificate (mTLS)
        "/path/to/client.crt",   // client certificate path
        "/path/to/client.key",   // client private key path
        "keypassword"            // private key password (nullptr if not encrypted)
    };

    // Optional: register a callback to receive TLS handshake event messages
    tls_config.setOnTLSEvent([](const char* message) {
        printf("[TLS] %s\n", message);
    });

    SocketClient client{ config, tls_config };

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
            printf(result.message());
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

    /*
    The Request Server is assumed to operate as follows:
        1. The client sends a Request Header containing the payload size.
        2. The client sends the Request Payload.
        3. The server parses the payload and sends a Response.
        4. The client receives the payload.
    */

    struct EchoRequestHeader
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

    struct EchoRequestHandler : public Bn3Monkey::SocketRequestHandler
    {
        size_t getHeaderSize() override {
            return sizeof(EchoRequestHeader);
        }
        size_t getPayloadSize(const char* header) override {
            return reinterpret_cast<const EchoResponseHeader*>(header)->payload_size;
        }
        Bn3Monkey::SocketRequestMode onModeClassified(const char* header) override {
            auto* derived_header = reinterpret_cast<const EchoRequestHeader*>(header);
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
            const char* header,
            const char* input_buffer,
            size_t input_size,
            char* output_buffer,
            size_t* output_size
        ) override {

            auto* derived_header = reinterpret_cast<const EchoRequestHeader*>(header);

            switch (derived_header->request_type) {
            case 0:
                printConcurrent("[Client %d -> Server] : %s\n", derived_header->client_no, input_buffer);

                auto* response = new (output_buffer) EchoResponse{ {derived_header->request_type, derived_header->request_no, sizeof(EchoResponse)}, input_buffer, input_size };
                *output_size = sizeof(EchoResponse);

                break;
            }
        }

        void onProcessedWithoutResponse(
            const char* header,
            const char* input_buffer,
            size_t input_size
        ) override {
            return;
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

### Using TLS Request Server

```cpp
#include <SecuritySocket.hpp>
#include <cstdio>

int main()
{
    using namespace Bn3Monkey;

    SocketConfiguration config{ "127.0.0.1", 20000 };

    // TLS server with optional client certificate authentication (mTLS)
    SocketTLSServerConfiguration tls_config{
        { SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3 },   // supported TLS versions
        { SocketTLS1_2CipherSuite::ECDHE_RSA_AES256_GCM_SHA384 }, // TLS 1.2 cipher suites
        { SocketTLS1_3CipherSuite::TLS_AES_256_GCM_SHA384 },      // TLS 1.3 cipher suites
        "/path/to/server.crt",                                     // server certificate path
        "/path/to/server.key",                                     // server private key path
        "keypassword",                                             // private key password (nullptr if not encrypted)
        SocketTLSClientAuthenticationMode::AUTH_MODE_OPTIONAL,               // client auth: AUTH_MODE_NONE / AUTH_MODE_OPTIONAL / AUTH_MODE_REQUIRED
        "/path/to/ca.crt"                                          // CA certificate path for verifying clients
    };

    // Optional: register a callback to receive TLS handshake event messages
    tls_config.setOnTLSEvent([](const char* message) {
        printf("[TLS] %s\n", message);
    });

    struct EchoRequestHandler : public Bn3Monkey::SocketRequestHandler
    {
        // ... (same as non-TLS example above)
    };

    EchoRequestHandler handler;

    SocketRequestServer server{ config, tls_config };
    auto result = server.open(&handler, 4);
    if (result.code() != SocketCode::SUCCESS)
    {
        printf(result.message());
        return -1;
    }

    // Sleep Thread

    server.close();
    return 0;
}
```

### Using Notification Server

```cpp
#include <SecuritySocket.hpp>
#include <cstdio>

int main()
{
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

### Using TLS Notification Server

```cpp
#include <SecuritySocket.hpp>
#include <cstdio>

int main()
{
    using namespace Bn3Monkey;

    SocketConfiguration config{ "127.0.0.1", 20000 };

    // TLS server requiring client certificate authentication (mTLS)
    SocketTLSServerConfiguration tls_config{
        { SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3 },
        {},                                                        // use default TLS 1.2 cipher suites
        {},                                                        // use default TLS 1.3 cipher suites
        "/path/to/server.crt",
        "/path/to/server.key",
        nullptr,                                                   // no key password
        SocketTLSClientAuthenticationMode::REQUIRED,               // require client certificate
        "/path/to/ca.crt"
    };

    SocketBroadcastServer server{ config, tls_config };

    {
        auto result = server.open(4);
        if (SocketCode::SUCCESS != result.code())
        {
            printf("%s", result.message());
            return -1;
        }
    }

    {
        auto result = server.enumerate();
        if (SocketCode::SUCCESS != result.code())
        {
            printf("%s", result.message());
            return -1;
        }
    }

    for (size_t i = 0; i < 20; i++)
    {
        server.write("Event", strlen("Event"));
    }

    server.close();
    return 0;
}
```

## TLS Configuration

### SocketTLSVersion

Specifies the TLS protocol versions the socket should support.

| Value    | Description |
| -------- | ----------- |
| `TLS1_2` | TLS 1.2     |
| `TLS1_3` | TLS 1.3     |

Multiple versions can be combined using an initializer list: `{ SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3 }`

### SocketTLS1_2CipherSuite

Cipher suites available for TLS 1.2.
If no cipher suites are specified, OpenSSL default cipher suites are used.

| Value                            | Cipher Suite                   |
| -------------------------------- | ------------------------------ |
| `ECDHE_ECDSA_AES256_GCM_SHA384`  | ECDHE-ECDSA-AES256-GCM-SHA384  |
| `ECDHE_RSA_AES256_GCM_SHA384`    | ECDHE-RSA-AES256-GCM-SHA384    |
| `ECDHE_ECDSA_CHACHA20_POLY1305`  | ECDHE-ECDSA-CHACHA20-POLY1305  |
| `ECDHE_RSA_CHACHA20_POLY1305`    | ECDHE-RSA-CHACHA20-POLY1305    |

### SocketTLS1_3CipherSuite

Cipher suites available for TLS 1.3.
If no cipher suites are specified, OpenSSL default cipher suites are used.

| Value                          | Cipher Suite                  |
| ------------------------------ | ----------------------------- |
| `TLS_AES_128_GCM_SHA256`       | TLS-AES-128-GCM-SHA256        |
| `TLS_AES_256_GCM_SHA384`       | TLS-AES-256-GCM-SHA384        |
| `TLS_CHACHA20_POLY1305_SHA256` | TLS-CHACHA20-POLY1305-SHA256  |
| `TLS_AES_128_CCM_SHA256`       | TLS-AES-128-CCM-SHA256        |
| `TLS_AES_128_CCM8_SHA256`      | TLS-AES-128-CCM8-SHA256       |

### SocketTLSClientAuthenticationMode

Controls whether the server requires a certificate from connecting clients (used in `SocketTLSServerConfiguration`).

| Value      | Description                                                                |
| ---------- | -------------------------------------------------------------------------- |
| `AUTH_MODE_NONE`     | No client certificate requested                                            |
| `AUTH_MODE_OPTIONAL` | Request a client certificate but allow connection even if none is provided |
| `AUTH_MODE_REQUIRED` | Reject the connection if the client does not provide a valid certificate   |

### SocketTLSClientConfiguration

Configuration for the TLS client. Passed as the second argument to `SocketClient`.

```cpp
SocketTLSClientConfiguration tls_config{
    std::initializer_list<SocketTLSVersion> support_versions,      // required: TLS versions to support
    std::initializer_list<SocketTLS1_2CipherSuite> tls_1_2_cipher_suites = {},  // optional
    std::initializer_list<SocketTLS1_3CipherSuite> tls_1_3_cipher_suites = {},  // optional
    bool verify_server = false,              // verify the server's certificate
    bool verify_hostname = false,            // verify that the server hostname matches the certificate CN/SAN
    const char* server_trust_store_path = nullptr,  // path to CA certificate file for server verification
    bool use_client_certificate = false,     // enable mTLS (send client certificate to server)
    const char* client_cert_file_path = nullptr,    // client certificate path (.crt / .pem)
    const char* client_key_file_path = nullptr,     // client private key path (.key / .pem)
    const char* client_key_password = nullptr       // private key password (nullptr if not encrypted)
};
```

#### TLS Event Callback

You can register a callback to receive diagnostic messages during the TLS handshake:

```cpp
tls_config.setOnTLSEvent([](const char* message) {
    printf("[TLS] %s\n", message);
});
```

### SocketTLSServerConfiguration

Configuration for the TLS server. Passed as the second argument to `SocketRequestServer` or `SocketBroadcastServer`.

```cpp
SocketTLSServerConfiguration tls_config{
    std::initializer_list<SocketTLSVersion> support_versions,      // required: TLS versions to support
    std::initializer_list<SocketTLS1_2CipherSuite> tls_1_2_cipher_suites = {},  // optional
    std::initializer_list<SocketTLS1_3CipherSuite> tls_1_3_cipher_suites = {},  // optional
    const char* server_cert_file_path = nullptr,   // server certificate path (.crt / .pem)
    const char* server_key_file_path = nullptr,    // server private key path (.key / .pem)
    const char* server_key_password = nullptr,     // private key password (nullptr if not encrypted)
    SocketTLSClientAuthenticationMode client_authentication_mode = SocketTLSClientAuthenticationMode::AUTH_MODE_NONE,
    const char* client_trust_store_path = nullptr  // path to CA certificate file for client verification
};
```

You can register a callback to receive diagnostic messages during the TLS handshake:

```cpp
tls_config.setOnTLSEvent([](const char* message) {
    printf("[TLS] %s\n", message);
});
```

## Specification

### Recommended C++ Version

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
- Add request server (it supports only TCP functionality, not TLS)
- Add notification server (it supports only TCP functionality, not TLS)

### 2.0.1 / 2025.02.18

- Fix socket result to contain read/write bytes
- Fix read function to only call once

### 2.0.2 / 2025.03.19

- Add time_between_retries to the configuration

### 2.0.3 / 2025.07.02

- Fixed the issue where resources were not freed when the client disconnected.

### 2.0.4 / 2025.08.11

- Fixed the compile warning issues in MSVC /W3

### 2.0.5 / 2025.08.11

- add isConnected function in SocketClient

### 2.0.6 / 2025.09.04

- Fixed the unix_domain support mechanism.

### 2.1.0 / 2025.09.09

- Fix request server
  1. Add Header Class
  2. Fix Request Handler class
     1. Users can interpret a custom-defined header through the onModeClassified function to determine the nature of the request:
        - Bn3Monkey::SocketRequestMode::FAST: tasks that can be processed quickly
        - Bn3Monkey::SocketRequestMode::SLOW: tasks that take longer, such as I/O
        - Bn3Monkey::SocketRequestMode::WRITE_STREAM: requests that continuously send data to the server
        - Bn3Monkey::SocketRequestMode::READ_STREAM: requests that continuously receive data from the server
     2. Users must implement tasks that should be processed quickly in onProcessedWithoutResponse, and tasks that require a response in onProcessed.

### 2.1.1 / 2025.09.09

- Remove Request Header class

### 2.1.2 / 2026.01.02

- fix warnings in gcc and msvc

### 2.2.0 / 2026.02.25

- Add TLS support to request server and notification (broadcast) server
- Add `SocketTLSClientConfiguration` and `SocketTLSServerConfiguration` with explicit control over:
  - TLS version selection (TLS 1.2, TLS 1.3, or both)
  - TLS 1.2 / TLS 1.3 cipher suite selection
  - Server certificate verification (`verify_server`, `verify_hostname`)
  - Mutual TLS (mTLS): client certificate authentication
  - Encrypted private key support (password-protected `.key` files)
  - TLS event callback (`setOnTLSEvent`) for handshake diagnostics
- Add `SocketTLSClientAuthenticationMode` (`AUTH_MODE_NONE` / `AUTH_MODE_OPTIONAL` / `AUTH_MODE_REQUIRED`) for server-side client authentication control
