#if !defined(__SECURITY_SOCKET_TEST_FILE_PROTOCOL__)
#define __SECURITY_SOCKET_TEST_FILE_PROTOCOL__

// Shared "file service" Custom Protocol fixture.
//
// Extracted from securitysockettest_tcp_request_file.cpp so the exact same
// handler can be driven over BOTH transports:
//   - raw TCP Custom Protocol  (securitysockettest_tcp_request_file.cpp)
//   - WebSocket-tunnelled Custom Protocol (securitysockettest_websocket_server.cpp)
// proving the handler does the same thing regardless of how the message
// arrives. The struct layouts are shared between the in-process "client" and
// the server, and FILE* handles round-trip as raw bytes — valid only because
// both ends live in the same address space (true for these unit tests).
//
// Constructing FileRequestHandler with a WebSocketConfiguration flips its
// supportWebSocket() on so it can be combined with an HttpRequestHandler (for
// the Upgrade) to serve the protocol over ws://.

#include <SecuritySocket.hpp>

#include <cstdio>
#include <cstdint>

#include "securitysockettest_helper.hpp"   // printConcurrent

enum class FileRequestType : int32_t {
    CREATE_HANDLE,
    CREATE_FILE,
    OPEN_FILE,
    READ_FILE,
    WRITE_FILE,
    CLOSE_FILE,
};

struct FileRequestHeader
{
    FileRequestType request_type{ FileRequestType::CREATE_FILE };
    int32_t request_no{ 0 };
    size_t payload_size{ 0 };
    int32_t client_no{ 0 };

    FileRequestHeader(FileRequestType request_type, int32_t request_no, size_t payload_size, int32_t client_no) :
        request_type(request_type),
        request_no(request_no),
        payload_size(payload_size),
        client_no(client_no) {
    }
};
struct FileOpenRequestPayload
{
    char filename[256]{ 0 };
};
struct FileWriteRequestPayload
{
    FILE* fp{ nullptr };
    size_t length{ 0 };
    char data[4096]{ 0 };
};
struct FileReadRequestPayload
{
    FILE* fp{ nullptr };
    size_t length;
};
struct FileCloseRequestPayload
{
    FILE* fp;
};

struct FileResponseHeader {
    FileRequestType request_type{ FileRequestType::CREATE_FILE };
    int32_t response_no{ 0 };
    size_t payload_size{ 0 };
};

struct FileOpenResponse {
    FileResponseHeader header;
    FILE* fp;
};
struct FileReadResponse
{
    FileResponseHeader header;
    size_t length;
    char data[4096]{ 0 };

    FileReadResponse() {

    }

    FileReadResponse(FileResponseHeader header) : header(header) {
    }
};
struct FileCloseResponse {
    FileResponseHeader header;
};


struct FileRequestHandler : public Bn3Monkey::CustomProtocolRequestHandler
{
    FileRequestHandler() = default;
    // Supplying a WebSocketConfiguration flips supportWebSocket() to true so the
    // very same handler can be tunnelled over ws:// (see the WebSocket server
    // test). Raw-TCP callers keep using the default constructor.
    explicit FileRequestHandler(const Bn3Monkey::WebSocketConfiguration& ws)
        : Bn3Monkey::CustomProtocolRequestHandler(ws) {}

    size_t headerSize() override {
        return sizeof(FileRequestHeader);
    }
    size_t payloadSize(const void* buffer) override {
        return reinterpret_cast<const FileRequestHeader*>(buffer)->payload_size;
    }


    Bn3Monkey::RequestProcessingMode classifyMode(const void* header) override {
        auto* derived_header = reinterpret_cast<const FileRequestHeader*>(header);
        switch (derived_header->request_type) {
        case FileRequestType::CREATE_HANDLE:
        case FileRequestType::CREATE_FILE:
        case FileRequestType::OPEN_FILE:
        case FileRequestType::CLOSE_FILE:
            return Bn3Monkey::RequestProcessingMode::FAST;
        case FileRequestType::READ_FILE:
            return Bn3Monkey::RequestProcessingMode::READ_STREAM;
        case FileRequestType::WRITE_FILE:
            return Bn3Monkey::RequestProcessingMode::WRITE_STREAM;
        }
        return Bn3Monkey::RequestProcessingMode::FAST;
    }

    void onConnected(const Bn3Monkey::ClientConnection& conn) override {
        printConcurrent("Client connected (ip : %s port : %u)\n", conn.ip(), conn.port());
    }

    void onDisconnected(const Bn3Monkey::ClientConnection& conn) override {
        printConcurrent("Client disconnected (ip : %s port : %u)\n", conn.ip(), conn.port());
    }

    void process(
        const Bn3Monkey::ClientConnection& conn,
        const Bn3Monkey::CustomProtocolRequest& req,
        Bn3Monkey::CustomProtocolResponse& res
    ) override {
        (void)conn;

        auto* header        = reinterpret_cast<const char*>(req.header());
        auto* input_buffer  = reinterpret_cast<const char*>(req.payload());
        char* output_buffer = reinterpret_cast<char*>(res.data());

        auto* derived_header = reinterpret_cast<const FileRequestHeader*>(header);

        switch (derived_header->request_type) {
        case FileRequestType::CREATE_HANDLE: {
                printConcurrent("[Client %d -> Server] : Create Handle \n", derived_header->client_no);

                auto* response = new (output_buffer) FileResponseHeader{ derived_header->request_type, derived_header->request_no, sizeof(FileOpenResponse)};
                (void)response;
                res.setLength(sizeof(FileResponseHeader));
            }
            break;
        case FileRequestType::CREATE_FILE:
            {
                printConcurrent("[Client %d -> Server] : Create File\n", derived_header->client_no);

                auto* open_request_payload = reinterpret_cast<const FileOpenRequestPayload*>(input_buffer);
                auto fp = fopen(open_request_payload->filename, "wb");

                auto* response = new (output_buffer) FileOpenResponse{ {derived_header->request_type, derived_header->request_no, sizeof(FileOpenResponse)}, fp };
                (void)response;
                res.setLength(sizeof(FileOpenResponse));
            }
            break;
        case FileRequestType::OPEN_FILE:
            {
                printConcurrent("[Client %d -> Server] : Open File\n", derived_header->client_no);

                auto* open_request_payload = reinterpret_cast<const FileOpenRequestPayload*>(input_buffer);
                auto fp = fopen(open_request_payload->filename, "rb");

                auto* response = new (output_buffer) FileOpenResponse{ {derived_header->request_type, derived_header->request_no, sizeof(FileOpenResponse)}, fp };
                (void)response;
                res.setLength(sizeof(FileOpenResponse));
            }
            break;
        case FileRequestType::CLOSE_FILE:
            {
                printConcurrent("[Client %d -> Server] : Close File\n", derived_header->client_no);

                auto* close_request_payload = reinterpret_cast<const FileCloseRequestPayload*>(input_buffer);
                fclose(close_request_payload->fp);

                auto* response = new (output_buffer) FileCloseResponse{ {derived_header->request_type, derived_header->request_no, sizeof(FileCloseResponse)} };
                (void)response;
                res.setLength(sizeof(FileCloseResponse));
            }
            break;
        case FileRequestType::READ_FILE:
            {
                printConcurrent("[Client %d -> Server] : Read File\n", derived_header->client_no);

                auto* read_request_payload = reinterpret_cast<const FileReadRequestPayload*>(input_buffer);

                auto response = new (output_buffer) FileReadResponse{
                    {derived_header->request_type, derived_header->request_no, sizeof(FileReadResponse)},
                };
                res.setLength(sizeof(FileReadResponse));

                response->length = fread(response->data, 1, read_request_payload->length, read_request_payload->fp);

            }
            break;
        default:
            break;
        }
    }

    void processWithoutResponse(
        const Bn3Monkey::ClientConnection& conn,
        const Bn3Monkey::CustomProtocolRequest& req
    ) override {
        (void)conn;

        auto* header       = reinterpret_cast<const char*>(req.header());
        auto* input_buffer = reinterpret_cast<const char*>(req.payload());

        auto* derived_header = reinterpret_cast<const FileRequestHeader*>(header);
        switch (derived_header->request_type) {
        case FileRequestType::WRITE_FILE:
            {
                printConcurrent("[Client %d -> Server] : Write  File\n", derived_header->client_no);

                auto* write_request_payload = reinterpret_cast<const FileWriteRequestPayload*>(input_buffer);

                auto length = fwrite(write_request_payload->data, 1, write_request_payload->length, write_request_payload->fp);
                (void)length;
            }
            break;

        default:
            break;
        }
    }
};

#endif // __SECURITY_SOCKET_TEST_FILE_PROTOCOL__
