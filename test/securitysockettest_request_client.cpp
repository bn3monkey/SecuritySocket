// SecuritySocket RequestClient regression tests (Phase 8).
//
// Replaces the libcurl-driven WebSocket server tests
// (securitysockettest_websocket_server.cpp) with our own Bn3Monkey::RequestClient.
// The point of Phase 8 is that ONE client type covers both transports — raw
// Custom-over-TCP and Custom-tunnelled-over-WebSocket — selected only by the
// ctor's WebSocketConfiguration. So the test routines (runEcho / runFileClient)
// take a RequestClient& and are driven once in each mode against the SAME
// handler logic, proving raw/WS parity.
//
// Not gated by SECURITYSOCKET_TEST_USE_CURL — uses our own client.

#include <gtest/gtest.h>

#include <SecuritySocket.hpp>

#include "securitysockettest_file_protocol.hpp"   // FileRequestHandler + structs

#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

namespace {

using namespace Bn3Monkey;

// Fixed 8-byte Custom header carried in each message (mirrors the WS curl test).
struct StubHeader {
    int32_t mode_tag{ 0 };       // 0 == FAST
    int32_t payload_len{ 0 };
};

// Echo handler: a Custom Protocol handler that echoes header + payload. The
// optional ws ctor flips supportWebSocket() on so the same logic serves both
// transports. process() runs FAST.
struct EchoHandler : public CustomProtocolRequestHandler {
    EchoHandler() = default;
    explicit EchoHandler(const WebSocketConfiguration& ws)
        : CustomProtocolRequestHandler(ws) {}

    size_t headerSize() override { return sizeof(StubHeader); }
    size_t payloadSize(const void* header) override {
        return static_cast<size_t>(
            reinterpret_cast<const StubHeader*>(header)->payload_len);
    }
    RequestProcessingMode classifyMode(const void*) override {
        return RequestProcessingMode::FAST;
    }
    void process(const ClientConnection&, const CustomProtocolRequest& req,
                 CustomProtocolResponse& res) override {
        const size_t hlen = req.headerLength();
        const size_t plen = req.payloadLength();
        char* out = static_cast<char*>(res.data());
        std::memcpy(out, req.header(), hlen);
        std::memcpy(out + hlen, req.payload(), plen);
        res.setLength(hlen + plen);
    }
};

// WS variant: also an HttpRequestHandler so the Upgrade GET is accepted, and
// constructed with the "/ws" pattern so tunnelled BINARY frames dispatch to the
// Custom path.
class WsEchoHandler : public HttpRequestHandler, public EchoHandler {
public:
    WsEchoHandler() : EchoHandler(WebSocketConfiguration{ "/ws" }) {}
    void registerRoutes(HttpRouter&) override {}
};

// WS variant of the file handler (same FileRequestHandler logic, plus HTTP for
// the Upgrade and a WS pattern).
class WsFileRequestHandler : public HttpRequestHandler, public FileRequestHandler {
public:
    WsFileRequestHandler() : FileRequestHandler(WebSocketConfiguration{ "/ws" }) {}
    void registerRoutes(HttpRouter&) override {}
};

class ServerRunner {
public:
    ServerRunner(RequestHandler& handler, uint16_t port)
        : _server(NetworkConfiguration{ "127.0.0.1", port, false, 5, 1000, 1000, 100, 8192 }) {
        auto r = _server.open(&handler, 8);
        _ok = (r.code() == NetworkResultCode::SUCCESS);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ~ServerRunner() { _server.close(); }
    bool ok() const { return _ok; }
private:
    RequestServer _server;
    bool _ok{ false };
};

NetworkConfiguration clientConfig(uint16_t port) {
    return NetworkConfiguration{ "127.0.0.1", port, false, 5, 2000, 2000, 50, 8192 };
}

// Send one Custom message (header + payload concatenated) as a single unit.
// Raw mode: one Client::write. WS mode: one BINARY frame. Identical call site.
bool sendMsg(RequestClient& c, const void* hdr, size_t hlen,
             const void* payload, size_t plen) {
    std::vector<char> msg(hlen + plen);
    std::memcpy(msg.data(), hdr, hlen);
    if (plen) std::memcpy(msg.data() + hlen, payload, plen);
    return c.send(msg.data(), msg.size()).code() == NetworkResultCode::SUCCESS;
}

// Accumulate exactly `want` bytes. Raw mode: each receive() is one recv()
// (partial reads loop here). WS mode: each receive() returns one frame's
// payload (multi-frame responses, e.g. READ_STREAM, loop here). Same loop.
bool recvExact(RequestClient& c, void* buf, size_t want) {
    size_t total = 0;
    while (total < want) {
        size_t got = 0;
        auto r = c.receive(static_cast<char*>(buf) + total, want - total, &got, 5000);
        if (r.code() != NetworkResultCode::SUCCESS) return false;
        if (got == 0) return false;
        total += got;
    }
    return true;
}

// ── shared test routines (driven in BOTH raw and WS modes) ──

void runEcho(RequestClient& client, int32_t payload_len) {
    std::vector<char> msg(sizeof(StubHeader) + static_cast<size_t>(payload_len));
    StubHeader h{ 0, payload_len };
    std::memcpy(msg.data(), &h, sizeof(h));
    for (int32_t i = 0; i < payload_len; ++i)
        msg[sizeof(StubHeader) + static_cast<size_t>(i)] =
            static_cast<char>('a' + (i % 26));

    ASSERT_TRUE(sendMsg(client, msg.data(), msg.size(), nullptr, 0));

    std::vector<char> got(msg.size());
    ASSERT_TRUE(recvExact(client, got.data(), got.size()));
    EXPECT_EQ(0, std::memcmp(got.data(), msg.data(), msg.size()));
}

// Full file-service round-trip (mirrors the WS curl FileHandler test), driven
// by RequestClient in whichever mode it was constructed.
void runFileClient(RequestClient& client, const char* filename) {
    const int32_t client_no = 7;
    const size_t kChunks = 16;
    const size_t kChunkBytes = sizeof(FileWriteRequestPayload{}.data);   // 4096
    const size_t kTotal = kChunks * kChunkBytes;
    int32_t request_no = 0;
    FILE* fp = nullptr;

    // CREATE_FILE (FAST → FileOpenResponse carrying the FILE*)
    {
        FileRequestHeader req{ FileRequestType::CREATE_FILE, ++request_no,
                               sizeof(FileOpenRequestPayload), client_no };
        FileOpenRequestPayload pl{};
        snprintf(pl.filename, sizeof(pl.filename), "%s", filename);
        ASSERT_TRUE(sendMsg(client, &req, sizeof(req), &pl, sizeof(pl))) << "send CREATE_FILE";
        FileOpenResponse resp{};
        ASSERT_TRUE(recvExact(client, &resp, sizeof(resp))) << "recv CREATE_FILE";
        EXPECT_EQ(resp.header.response_no, req.request_no);
        EXPECT_EQ(resp.header.request_type, req.request_type);
        ASSERT_NE(nullptr, resp.fp);
        fp = resp.fp;
    }

    // WRITE_BEGIN (WRITE_STREAM entry, no response)
    {
        FileRequestHeader req{ FileRequestType::WRITE_BEGIN, ++request_no,
                               sizeof(WriteBeginPayload), client_no };
        WriteBeginPayload pl{ fp };
        ASSERT_TRUE(sendMsg(client, &req, sizeof(req), &pl, sizeof(pl))) << "send WRITE_BEGIN";
    }

    // WRITE_CHUNK × N (upload: no per-chunk response)
    for (size_t i = 0; i < kChunks; ++i) {
        const int32_t last = (i == kChunks - 1) ? 1 : 0;
        FileRequestHeader req{ FileRequestType::WRITE_CHUNK, ++request_no,
                               sizeof(FileWriteRequestPayload), client_no, last };
        FileWriteRequestPayload pl{};
        pl.fp = fp;
        pl.length = sizeof(pl.data);
        std::memset(pl.data, static_cast<int>('a' + (i % 26)), sizeof(pl.data));
        ASSERT_TRUE(sendMsg(client, &req, sizeof(req), &pl, sizeof(pl))) << "send WRITE " << i;
    }

    // CLOSE_FILE (FAST → FileCloseResponse; flushes the writes)
    {
        FileRequestHeader req{ FileRequestType::CLOSE_FILE, ++request_no,
                               sizeof(FileCloseRequestPayload), client_no };
        FileCloseRequestPayload pl{}; pl.fp = fp;
        ASSERT_TRUE(sendMsg(client, &req, sizeof(req), &pl, sizeof(pl))) << "send CLOSE(write)";
        FileCloseResponse resp{};
        ASSERT_TRUE(recvExact(client, &resp, sizeof(resp))) << "recv CLOSE(write)";
        EXPECT_EQ(resp.header.response_no, req.request_no);
        EXPECT_EQ(resp.header.request_type, req.request_type);
    }

    // OPEN_FILE (FAST → FileOpenResponse with total size)
    {
        FileRequestHeader req{ FileRequestType::OPEN_FILE, ++request_no,
                               sizeof(FileOpenRequestPayload), client_no };
        FileOpenRequestPayload pl{};
        snprintf(pl.filename, sizeof(pl.filename), "%s", filename);
        ASSERT_TRUE(sendMsg(client, &req, sizeof(req), &pl, sizeof(pl))) << "send OPEN_FILE";
        FileOpenResponse resp{};
        ASSERT_TRUE(recvExact(client, &resp, sizeof(resp))) << "recv OPEN_FILE";
        EXPECT_EQ(resp.header.response_no, req.request_no);
        EXPECT_EQ(resp.header.request_type, req.request_type);
        ASSERT_NE(nullptr, resp.fp);
        EXPECT_EQ(kTotal, resp.total_size);
        fp = resp.fp;
    }

    // READ_BEGIN (READ_STREAM): server streams kTotal raw bytes back across N
    // frames; recvExact reassembles by byte count.
    {
        FileRequestHeader req{ FileRequestType::READ_BEGIN, ++request_no,
                               sizeof(ReadBeginPayload), client_no };
        ReadBeginPayload pl{ fp, kChunkBytes, kTotal };
        ASSERT_TRUE(sendMsg(client, &req, sizeof(req), &pl, sizeof(pl))) << "send READ_BEGIN";

        std::vector<char> got(kTotal);
        ASSERT_TRUE(recvExact(client, got.data(), kTotal)) << "recv read stream";
        for (size_t i = 0; i < kChunks; ++i) {
            char expected[4096];
            std::memset(expected, static_cast<int>('a' + (i % 26)), sizeof(expected));
            EXPECT_EQ(0, std::memcmp(got.data() + i * kChunkBytes, expected, sizeof(expected)))
                << "read chunk " << i;
        }
    }

    // CLOSE_FILE again
    {
        FileRequestHeader req{ FileRequestType::CLOSE_FILE, ++request_no,
                               sizeof(FileCloseRequestPayload), client_no };
        FileCloseRequestPayload pl{}; pl.fp = fp;
        ASSERT_TRUE(sendMsg(client, &req, sizeof(req), &pl, sizeof(pl))) << "send CLOSE(read)";
        FileCloseResponse resp{};
        ASSERT_TRUE(recvExact(client, &resp, sizeof(resp))) << "recv CLOSE(read)";
        EXPECT_EQ(resp.header.response_no, req.request_no);
        EXPECT_EQ(resp.header.request_type, req.request_type);
    }
}

class RequestClientTest : public ::testing::Test {
protected:
    void SetUp() override    { initializeSecuritySocket(); }
    void TearDown() override  { releaseSecuritySocket(); }
};

constexpr uint16_t kRawEchoPort  = 28861;
constexpr uint16_t kWsEchoPort   = 28862;
constexpr uint16_t kRawFilePort  = 28863;
constexpr uint16_t kWsFilePort   = 28864;

}  // namespace

// ── raw mode ──

TEST_F(RequestClientTest, RawEchoRoundTrip) {
    EchoHandler handler;
    ServerRunner srv(handler, kRawEchoPort);
    ASSERT_TRUE(srv.ok());

    RequestClient client{ clientConfig(kRawEchoPort) };
    EXPECT_FALSE(client.isWebSocket());
    ASSERT_EQ(NetworkResultCode::SUCCESS, client.connect().code());

    runEcho(client, 16);
}

TEST_F(RequestClientTest, RawManyRoundTrips) {
    EchoHandler handler;
    ServerRunner srv(handler, kRawEchoPort);
    ASSERT_TRUE(srv.ok());

    RequestClient client{ clientConfig(kRawEchoPort) };
    ASSERT_EQ(NetworkResultCode::SUCCESS, client.connect().code());

    for (int i = 0; i < 120; ++i) {
        SCOPED_TRACE(testing::Message() << "round " << i);
        runEcho(client, 8 + (i % 32));
    }
}

TEST_F(RequestClientTest, RawFileHandler) {
    FileRequestHandler handler;
    ServerRunner srv(handler, kRawFilePort);
    ASSERT_TRUE(srv.ok());

    RequestClient client{ clientConfig(kRawFilePort) };
    ASSERT_EQ(NetworkResultCode::SUCCESS, client.connect().code());

    const char* kFile = "rc_raw_testfile.txt";
    runFileClient(client, kFile);
    std::remove(kFile);
}

// ── WebSocket-tunnelled mode (SAME runEcho / runFileClient routines) ──

TEST_F(RequestClientTest, WsEchoRoundTrip) {
    WsEchoHandler handler;
    ServerRunner srv(handler, kWsEchoPort);
    ASSERT_TRUE(srv.ok());

    RequestClient client{ clientConfig(kWsEchoPort), WebSocketConfiguration{ "/ws" } };
    EXPECT_TRUE(client.isWebSocket());
    ASSERT_EQ(NetworkResultCode::SUCCESS, client.connect().code()) << "ws handshake";

    runEcho(client, 16);
}

TEST_F(RequestClientTest, WsManyRoundTrips) {
    WsEchoHandler handler;
    ServerRunner srv(handler, kWsEchoPort);
    ASSERT_TRUE(srv.ok());

    RequestClient client{ clientConfig(kWsEchoPort), WebSocketConfiguration{ "/ws" } };
    ASSERT_EQ(NetworkResultCode::SUCCESS, client.connect().code()) << "ws handshake";

    for (int i = 0; i < 120; ++i) {
        SCOPED_TRACE(testing::Message() << "round " << i);
        runEcho(client, 8 + (i % 32));
    }
}

TEST_F(RequestClientTest, WsFileHandler) {
    WsFileRequestHandler handler;
    ServerRunner srv(handler, kWsFilePort);
    ASSERT_TRUE(srv.ok());

    RequestClient client{ clientConfig(kWsFilePort), WebSocketConfiguration{ "/ws" } };
    ASSERT_EQ(NetworkResultCode::SUCCESS, client.connect().code()) << "ws handshake";

    const char* kFile = "rc_ws_testfile.txt";
    runFileClient(client, kFile);
    std::remove(kFile);
}
