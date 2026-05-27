# Security Socket

Security Socket is a simple socket c++ library for supporting TCP and TLS.
It is compatible for Windows(MSVC, MinGW Compiler), Android (Clang), Linux (gcc)

- [Security Socket](#security-socket)
  - [Build](#build)
    - [Option](#option)
  - [Third-Party Dependencies](#third-party-dependencies)
  - [Example](#example)
    - [Using Client](#using-client)
    - [Using TLS Client](#using-tls-client)
    - [Using Request Server](#using-request-server)
    - [Using TLS Request Server](#using-tls-request-server)
    - [Using Notification Server](#using-notification-server)
    - [Using TLS Notification Server](#using-tls-notification-server)
  - [TLS Configuration](#tls-configuration)
    - [TlsVersion](#tlsversion)
    - [TlsV12CipherSuite](#tlsv12ciphersuite)
    - [TlsV13CipherSuite](#tlsv13ciphersuite)
    - [TlsClientAuthMode](#tlsclientauthmode)
    - [TlsClientConfiguration](#tlsclientconfiguration)
      - [TLS Event Callback](#tls-event-callback)
    - [TlsServerConfiguration](#tlsserverconfiguration)
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
    - [2.2.1 / 2026.04.01](#221--20260401)
    - [2.3.0 / 2026.04.29](#230--20260429)
    - [2.3.1 / 2026.04.30](#231--20260430)
    - [2.3.2 / 2026.05.04](#232--20260504)

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
    GIT_TAG v2.3.2)
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

- **SECURITYSOCKET_TEST_USE_CURL**
  - Fetch libcurl and compile the HTTP / WebSocket server regression tests
    into the `securitysockettest` library. When ON, the macro
    `SECURITYSOCKET_TEST_USE_CURL` is also defined so the test sources
    `securitysockettest_http_server.cpp` and
    `securitysockettest_websocket_server.cpp` include their libcurl-dependent
    content.
  - When _OFF_, libcurl is never downloaded, configured, or linked. The two
    HTTP/WebSocket test sources still belong to the test library but
    compile to empty translation units (no libcurl headers, no test cases).
    The default Custom-Protocol regression tests are unaffected.
  - Requires `BUILD_SECURITYSOCKET_TEST=ON`.
  - Default value is _OFF_

```cmake
cmake_minimum_required (VERSION 3.16)
...

include(FetchContent)

set(SECURITYSOCKET_USING_TLS OFF CACHE BOOL "Letting Security Socket support TLS functionality" FORCE)
option(BUILD_SECURITYSOCKET_SHARED OFF CACHE BOOL "Build Security socket as shared library" FORCE)
option(BUILD_SECURITYSOCKET_TEST OFF CACHE BOOL "Build Security socket test" FORCE)

FetchContent_Declear(SecuritySocket
    GIT_REPOSITORY https://github.com/bn3monkey/securitysocket
    GIT_TAG v2.3.2)

FetchContent_MakeAvailable(SecuritySocket)

...
```

## Third-Party Dependencies

All third-party libraries are fetched and built from source via CMake's
`FetchContent`. None of them are vendored into this repository.

| Library | Version / Tag | License | Scope | Notes |
| --- | --- | --- | --- | --- |
| [OpenSSL (cmake fork)](https://github.com/bn3monkey/openssl-cmake) | branch `proto` | Apache-2.0 | runtime (TLS) | Pulled in only when `SECURITYSOCKET_USING_TLS=ON`. Provides `OpenSSL::Crypto` and `OpenSSL::SSL`. |
| [GoogleTest](https://github.com/google/googletest) | `release-1.12.1` | BSD-3-Clause | test | Pulled in only when `BUILD_SECURITYSOCKET_TEST=ON`. |
| [remote-command](https://github.com/bn3monkey/remote-command) | `1.2.4` | n/a (internal) | test | Pulled in only when `BUILD_SECURITYSOCKET_TEST=ON`. |
| [picohttpparser](https://github.com/h2o/picohttpparser) | `f4d94b48` (master) | MIT or Perl (dual-licensed) | runtime (HTTP) | **Vendored**, not fetched at build time. Source lives under [src/implementation/thirdparty/picohttpparser/](src/implementation/thirdparty/picohttpparser/) and is compiled into both `securitysocket` and `securitysockettest`. Powers the HTTP/1.1 request-line + header parser introduced in v3. |
| [libcurl](https://github.com/curl/curl) | `curl-8_11_0` (≥ 8.11.0) | curl (MIT-like) | test | Pulled in only when `SECURITYSOCKET_TEST_USE_CURL=ON`. Built statically, HTTP-only protocol surface, WebSocket support (`CURL_ENABLE_WEBSOCKETS=ON`). Linked into `securitysockettest` and used as the test client for the HTTP / WebSocket server regression tests (`securitysockettest_http_server.cpp`, `securitysockettest_websocket_server.cpp`), which gate their content with `#if defined(SECURITYSOCKET_TEST_USE_CURL)`. The build verifies `curl_ws_send` / `curl_ws_recv` are exported via `check_symbol_exists` at configure time. |

## Example

### Using Client

```cpp
#include <SecuritySocket.hpp>
#include <cstring>

int main()
{
    initializeSecuritySocket();

    using namespace Bn3Monkey;
    auto configuration = NetworkConfiguration("127.0.0.1", 5000, false);
    Client client { configuration };

    {
        auto result = client.open();
        if (result.code() != NetworkResultCode::SUCCESS)
        {
            printf(result.message());
            return -1;
        }
    }

    {
        auto result = client.connect();
        if (result.code() != NetworkResultCode::SUCCESS)
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
        if (result.code() != NetworkResultCode::SUCCESS)
        {
            printf(result.message());
            return -1;
        }
    }
    {
        char buffer[4096] {0};
        auto result = client.read(buffer, 14);
        if (result.code() != NetworkResultCode::SUCCESS)
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

    NetworkConfiguration config{ "127.0.0.1", 5000 };

    // TLS 1.2/1.3 with server certificate verification and mutual TLS (mTLS)
    TlsClientConfiguration tls_config{
        { TlsVersion::TLS1_2, TlsVersion::TLS1_3 },    // supported TLS versions
        { TlsV12CipherSuite::ECDHE_RSA_AES256_GCM_SHA384 },  // TLS 1.2 cipher suites
        { TlsV13CipherSuite::TLS_AES_256_GCM_SHA384 },       // TLS 1.3 cipher suites
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

    Client client{ config, tls_config };

    {
        auto result = client.open();
        if (result.code() != NetworkResultCode::SUCCESS)
        {
            printf(result.message());
            return -1;
        }
    }

    {
        auto result = client.connect();
        if (result.code() != NetworkResultCode::SUCCESS)
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
        if (result.code() != NetworkResultCode::SUCCESS)
        {
            printf(result.message());
            return -1;
        }
    }

    {
        char buffer[4096] {0};
        auto result = client.read(buffer, 14);
        if (result.code() != NetworkResultCode::SUCCESS)
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

    NetworkConfiguration config{
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

    struct EchoRequestHandler : public Bn3Monkey::CustomProtocolRequestHandler
    {
        size_t getHeaderSize() override {
            return sizeof(EchoRequestHeader);
        }
        size_t getPayloadSize(const char* header) override {
            return reinterpret_cast<const EchoResponseHeader*>(header)->payload_size;
        }
        Bn3Monkey::RequestProcessingMode onModeClassified(const char* header) override {
            auto* derived_header = reinterpret_cast<const EchoRequestHeader*>(header);
            switch (derived_header->request_type) {
            case 0:
                return Bn3Monkey::RequestProcessingMode::FAST;
            }
            return Bn3Monkey::RequestProcessingMode::FAST;
        }

        void onConnected(const Bn3Monkey::ClientConnection& conn) override {
            printConcurrent("Client connected (ip : %s port : %u)\n", conn.ip(), conn.port());
        }

        void onDisconnected(const Bn3Monkey::ClientConnection& conn) override {
            printConcurrent("Client disconnected (ip : %s port : %u)\n", conn.ip(), conn.port());
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

    RequestServer server{ config};
    auto result = server.open(&handler, 4);
    if (result.code() != NetworkResultCode::SUCCESS)
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

    NetworkConfiguration config{ "127.0.0.1", 20000 };

    // TLS server with optional client certificate authentication (mTLS)
    TlsServerConfiguration tls_config{
        { TlsVersion::TLS1_2, TlsVersion::TLS1_3 },   // supported TLS versions
        { TlsV12CipherSuite::ECDHE_RSA_AES256_GCM_SHA384 }, // TLS 1.2 cipher suites
        { TlsV13CipherSuite::TLS_AES_256_GCM_SHA384 },      // TLS 1.3 cipher suites
        "/path/to/server.crt",                                     // server certificate path
        "/path/to/server.key",                                     // server private key path
        "keypassword",                                             // private key password (nullptr if not encrypted)
        TlsClientAuthMode::AUTH_MODE_OPTIONAL,               // client auth: AUTH_MODE_NONE / AUTH_MODE_OPTIONAL / AUTH_MODE_REQUIRED
        "/path/to/ca.crt"                                          // CA certificate path for verifying clients
    };

    // Optional: register a callback to receive TLS handshake event messages
    tls_config.setOnTLSEvent([](const char* message) {
        printf("[TLS] %s\n", message);
    });

    struct EchoRequestHandler : public Bn3Monkey::CustomProtocolRequestHandler
    {
        // ... (same as non-TLS example above)
    };

    EchoRequestHandler handler;

    RequestServer server{ config, tls_config };
    auto result = server.open(&handler, 4);
    if (result.code() != NetworkResultCode::SUCCESS)
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

    NetworkConfiguration config{
        "127.0.0.1",
        20000,
        false
    };

    // Optional: implement BroadcastHandler to observe connect/disconnect
    struct PrintingHandler : public BroadcastHandler {
        void onClientConnected(const char* ip, int port) override {
            printf("client connected    %s:%d\n", ip, port);
        }
        void onClientDisconnected(const char* ip, int port) override {
            printf("client disconnected %s:%d\n", ip, port);
        }
    };
    PrintingHandler handler;

    BroadcastServer server{ config };

    {
        {
            auto result = server.open(&handler, 1);  // pass nullptr if you don't need callbacks
            if(NetworkResultCode::SUCCESS != result.code())
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

    NetworkConfiguration config{ "127.0.0.1", 20000 };

    // TLS server requiring client certificate authentication (mTLS)
    TlsServerConfiguration tls_config{
        { TlsVersion::TLS1_2, TlsVersion::TLS1_3 },
        {},                                                        // use default TLS 1.2 cipher suites
        {},                                                        // use default TLS 1.3 cipher suites
        "/path/to/server.crt",
        "/path/to/server.key",
        nullptr,                                                   // no key password
        TlsClientAuthMode::REQUIRED,               // require client certificate
        "/path/to/ca.crt"
    };

    BroadcastServer server{ config, tls_config };

    {
        auto result = server.open(nullptr, 4);  // optional BroadcastHandler*
        if (NetworkResultCode::SUCCESS != result.code())
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

### TlsVersion

Specifies the TLS protocol versions the socket should support.

| Value    | Description |
| -------- | ----------- |
| `TLS1_2` | TLS 1.2     |
| `TLS1_3` | TLS 1.3     |

Multiple versions can be combined using an initializer list: `{ TlsVersion::TLS1_2, TlsVersion::TLS1_3 }`

### TlsV12CipherSuite

Cipher suites available for TLS 1.2.
If no cipher suites are specified, OpenSSL default cipher suites are used.

| Value                            | Cipher Suite                   |
| -------------------------------- | ------------------------------ |
| `ECDHE_ECDSA_AES256_GCM_SHA384`  | ECDHE-ECDSA-AES256-GCM-SHA384  |
| `ECDHE_RSA_AES256_GCM_SHA384`    | ECDHE-RSA-AES256-GCM-SHA384    |
| `ECDHE_ECDSA_CHACHA20_POLY1305`  | ECDHE-ECDSA-CHACHA20-POLY1305  |
| `ECDHE_RSA_CHACHA20_POLY1305`    | ECDHE-RSA-CHACHA20-POLY1305    |

### TlsV13CipherSuite

Cipher suites available for TLS 1.3.
If no cipher suites are specified, OpenSSL default cipher suites are used.

| Value                          | Cipher Suite                  |
| ------------------------------ | ----------------------------- |
| `TLS_AES_128_GCM_SHA256`       | TLS-AES-128-GCM-SHA256        |
| `TLS_AES_256_GCM_SHA384`       | TLS-AES-256-GCM-SHA384        |
| `TLS_CHACHA20_POLY1305_SHA256` | TLS-CHACHA20-POLY1305-SHA256  |
| `TLS_AES_128_CCM_SHA256`       | TLS-AES-128-CCM-SHA256        |
| `TLS_AES_128_CCM8_SHA256`      | TLS-AES-128-CCM8-SHA256       |

### TlsClientAuthMode

Controls whether the server requires a certificate from connecting clients (used in `TlsServerConfiguration`).

| Value      | Description                                                                |
| ---------- | -------------------------------------------------------------------------- |
| `AUTH_MODE_NONE`     | No client certificate requested                                            |
| `AUTH_MODE_OPTIONAL` | Request a client certificate but allow connection even if none is provided |
| `AUTH_MODE_REQUIRED` | Reject the connection if the client does not provide a valid certificate   |

### TlsClientConfiguration

Configuration for the TLS client. Passed as the second argument to `Client`.

```cpp
TlsClientConfiguration tls_config{
    std::initializer_list<TlsVersion> support_versions,      // required: TLS versions to support
    std::initializer_list<TlsV12CipherSuite> tls_1_2_cipher_suites = {},  // optional
    std::initializer_list<TlsV13CipherSuite> tls_1_3_cipher_suites = {},  // optional
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

### TlsServerConfiguration

Configuration for the TLS server. Passed as the second argument to `RequestServer` or `BroadcastServer`.

```cpp
TlsServerConfiguration tls_config{
    std::initializer_list<TlsVersion> support_versions,      // required: TLS versions to support
    std::initializer_list<TlsV12CipherSuite> tls_1_2_cipher_suites = {},  // optional
    std::initializer_list<TlsV13CipherSuite> tls_1_3_cipher_suites = {},  // optional
    const char* server_cert_file_path = nullptr,   // server certificate path (.crt / .pem)
    const char* server_key_file_path = nullptr,    // server private key path (.key / .pem)
    const char* server_key_password = nullptr,     // private key password (nullptr if not encrypted)
    TlsClientAuthMode client_authentication_mode = TlsClientAuthMode::AUTH_MODE_NONE,
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

- add isConnected function in Client

### 2.0.6 / 2025.09.04

- Fixed the unix_domain support mechanism.

### 2.1.0 / 2025.09.09

- Fix request server
  1. Add Header Class
  2. Fix Request Handler class
     1. Users can interpret a custom-defined header through the onModeClassified function to determine the nature of the request:
        - Bn3Monkey::RequestProcessingMode::FAST: tasks that can be processed quickly
        - Bn3Monkey::RequestProcessingMode::SLOW: tasks that take longer, such as I/O
        - Bn3Monkey::RequestProcessingMode::WRITE_STREAM: requests that continuously send data to the server
        - Bn3Monkey::RequestProcessingMode::READ_STREAM: requests that continuously receive data from the server
     2. Users must implement tasks that should be processed quickly in onProcessedWithoutResponse, and tasks that require a response in onProcessed.

### 2.1.1 / 2025.09.09

- Remove Request Header class

### 2.1.2 / 2026.01.02

- fix warnings in gcc and msvc

### 2.2.0 / 2026.02.25

- Add TLS support to request server and notification (broadcast) server
- Add `TlsClientConfiguration` and `TlsServerConfiguration` with explicit control over:
  - TLS version selection (TLS 1.2, TLS 1.3, or both)
  - TLS 1.2 / TLS 1.3 cipher suite selection
  - Server certificate verification (`verify_server`, `verify_hostname`)
  - Mutual TLS (mTLS): client certificate authentication
  - Encrypted private key support (password-protected `.key` files)
  - TLS event callback (`setOnTLSEvent`) for handshake diagnostics
- Add `TlsClientAuthMode` (`AUTH_MODE_NONE` / `AUTH_MODE_OPTIONAL` / `AUTH_MODE_REQUIRED`) for server-side client authentication control

### 2.2.1 / 2026.04.01

- Fix `setBlockingMode()` using wrong bitwise operator (`|` → `&`) with `~O_NONBLOCK`, which corrupted socket flags and prevented blocking mode restoration
- Fix `setTimeout()` copy-paste bug where write timeout values were assigned to `read_timeout` instead of `write_timeout`
- Fix `SocketConnection::routine()` crash when accessing empty task queue on worker thread shutdown
- Fix `TLSClientActiveSocket` null pointer dereference when `SSL_new()` fails but TLS event callback is set
- Fix `TLSServerActiveSocket` destructor double scope resolution causing compilation errors
- Fix `ClientActiveSocket` destructor not calling `close()`, causing socket file descriptor leaks
- Fix `SocketConnection::state` member variable left uninitialized
- Fix `ObjectPool::release()` off-by-one error preventing the last pooled object from being reused

### 2.3.0 / 2026.04.29

- Fix data race in `BroadcastServer` between the accept-monitor thread and the broadcast caller. Replaced the unsynchronized double-buffer client list with a single-producer / single-consumer pending queue that the broadcast caller drains under a brief lock; the actual `write()` loop holds no lock during network I/O.
- Auto-remove disconnected clients from the broadcast list during `write()` (detect `SOCKET_CLOSED` from listener / `send`, close the socket, and erase from the active list).
- Make `BroadcastServer::close()` safe to call before / after `open()` (null-guarded `_socket->close()`, idempotent monitor-thread shutdown).
- Remove `BroadcastServer::enumerate()` from the public API (breaking change — the call was previously declared but never implemented).

### 2.3.1 / 2026.04.30

- Add `BroadcastServer::await(uint64_t timeout_ms)` — block until at least one client is connected.
- Add `BroadcastServer::awaitClose(uint64_t timeout_ms)` — block until every currently-active client has closed (peer FIN received). Use as an explicit barrier between broadcast rounds: after writing a batch, calling `awaitClose` ensures the round's clients have finished consuming and disconnected before the next `await()` runs, eliminating the cross-round race where a still-open previous client receives the next round's messages.
- Add `BroadcastHandler` interface with `onClientConnected(ip, port)` / `onClientDisconnected(ip, port)` callbacks for observing connection events on the broadcast server.
- **Breaking**: `BroadcastServer::open()` signature changed — it now takes a `BroadcastHandler*` as its first argument: `open(BroadcastHandler* handler, size_t num_of_clients)`. Pass `nullptr` if you don't need connection callbacks.
- Rewrite `BroadcastServer`'s accept-monitor on a single `SocketMultiEventListener` (mirrors the `RequestServer` pattern) that owns both the accept fd and every accepted client fd. Peer-close is now detected by the kernel via `POLLHUP` / `POLLERR` and surfaced as a `DISCONNECTED` event — the previous pending-queue and `recv(MSG_PEEK)` health-check polling have been removed.
- Auto-disable Nagle's algorithm (`TCP_NODELAY`) on accepted broadcast clients so each `write()` reaches the wire immediately. New `ServerActiveSocket::setNoDelay()` and free `setNoDelay()` helper in `SocketHelper.hpp` (Win32 + POSIX; silently no-op on AF_UNIX).
- Internal: `await()` / `awaitClose()` are now simple `condition_variable::wait_for` predicates against the single active-client list. Broadcast `write()` snapshots that list under lock then streams bytes lock-free; `shared_ptr<BroadcastClient>` keeps each client alive across mid-broadcast `DISCONNECTED` removal.

### 2.3.2 / 2026.05.04

- Add `BroadcastServer::dropAll()` — forcibly disconnect every currently-active client. Closes each client socket, fires `onClientDisconnected` for each, and clears the active list. Use when a peer abandons its socket without sending FIN (e.g., reconnecting via a fresh socket without closing the old one); the kernel never reports `POLLHUP` for those, so the accept-monitor has no signal to clean them up on its own.
- Treat `POLLNVAL` as `DISCONNECTED` in `SocketMultiEventListener::wait()` on both Linux and Windows. Without this, an fd closed under the listener kept firing the same `revents` on every subsequent `poll()` / `WSAPoll()` and the cleanup path never ran.
- Internal: promote the broadcast server's `SocketMultiEventListener` and accept `SocketEventContext` from monitor-thread locals to members so `dropAll()` can call `removeEvent()` from the broadcast caller's thread. Add a `_pending_destruction` list that holds dropped clients until the monitor's next loop iteration — releasing the strong refs synchronously would race the in-flight wait+dispatch step that still dereferences context pointers.
- Internal: tighten `BroadcastServer::await()` to re-check `_is_monitoring` and `_active_clients.empty()` under the lock after `wait_for`, so a `close()` or `dropAll()` racing the wake returns the correct result code instead of a stale success.
