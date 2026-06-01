// SecuritySocket WebSocket server regression tests (driven by libcurl's
// curl_ws_send / curl_ws_recv API).
//
// A real RequestServer is started with a handler that supports BOTH HTTP (so
// the Upgrade GET is handled by HttpPhase) and Custom Protocol with a WebSocket
// pattern (so tunnelled BINARY messages dispatch through WebSocketPhase).
// libcurl performs the ws:// handshake, then sends a Custom message in a BINARY
// frame and reads back the framed response — proving the full
// sniff -> HTTP upgrade -> WebSocket -> Custom-dispatch path interoperates with
// a standard WebSocket client.
//
// Gated by SECURITYSOCKET_TEST_USE_CURL (see root CMakeLists.txt).

#if defined(SECURITYSOCKET_TEST_USE_CURL)

#include <gtest/gtest.h>

#include <curl/curl.h>
#include <curl/websockets.h>

#include <SecuritySocket.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr uint16_t kWsPort = 28782;

std::string wsUrl() {
    return std::string("ws://127.0.0.1:") + std::to_string(kWsPort) + "/ws";
}

// Fixed 8-byte Custom header carried inside each WS BINARY message.
struct WsStubHeader {
    int32_t mode_tag{ 0 };       // 0 == FAST
    int32_t payload_len{ 0 };
};

// Handler that is simultaneously an HTTP handler (empty route table — the
// Upgrade is matched by the WS pattern, not a route) and a Custom Protocol
// handler advertising the "/ws" WebSocket pattern. process() echoes the whole
// message (header + payload) back.
class WsEchoHandler : public Bn3Monkey::HttpRequestHandler,
                      public Bn3Monkey::CustomProtocolRequestHandler {
public:
    WsEchoHandler()
        : Bn3Monkey::CustomProtocolRequestHandler(
              Bn3Monkey::WebSocketConfiguration{ "/ws" }) {}

    void registerRoutes(Bn3Monkey::HttpRouter&) override {}

    size_t headerSize() override { return sizeof(WsStubHeader); }
    size_t payloadSize(const void* header) override {
        return static_cast<size_t>(
            reinterpret_cast<const WsStubHeader*>(header)->payload_len);
    }
    Bn3Monkey::RequestProcessingMode classifyMode(const void*) override {
        return Bn3Monkey::RequestProcessingMode::FAST;
    }
    void process(const Bn3Monkey::ClientConnection&,
                 const Bn3Monkey::CustomProtocolRequest& req,
                 Bn3Monkey::CustomProtocolResponse& res) override {
        const size_t hlen = req.headerLength();
        const size_t plen = req.payloadLength();
        char* out = static_cast<char*>(res.data());
        std::memcpy(out, req.header(), hlen);
        std::memcpy(out + hlen, req.payload(), plen);
        res.setLength(hlen + plen);
    }
    void processWithoutResponse(const Bn3Monkey::ClientConnection&,
                                const Bn3Monkey::CustomProtocolRequest&) override {}
};

class ServerRunner {
public:
    explicit ServerRunner(Bn3Monkey::RequestHandler& handler)
        : _server(Bn3Monkey::NetworkConfiguration{
              "127.0.0.1", kWsPort, false, 5, 1000, 1000, 100, 8192 }) {
        auto r = _server.open(&handler, 8);
        _ok = (r.code() == Bn3Monkey::NetworkResultCode::SUCCESS);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ~ServerRunner() { _server.close(); }
    bool ok() const { return _ok; }
private:
    Bn3Monkey::RequestServer _server;
    bool _ok{ false };
};

std::vector<char> makeMessage(int32_t payload_len) {
    std::vector<char> msg(sizeof(WsStubHeader) + static_cast<size_t>(payload_len));
    WsStubHeader h{ /*FAST*/ 0, payload_len };
    std::memcpy(msg.data(), &h, sizeof(h));
    for (int32_t i = 0; i < payload_len; ++i) {
        msg[sizeof(WsStubHeader) + static_cast<size_t>(i)] =
            static_cast<char>('a' + (i % 26));
    }
    return msg;
}

// Receive one full frame, retrying on CURLE_AGAIN up to ~3s.
bool wsRecvFull(CURL* h, void* buf, size_t cap, size_t* received) {
    const struct curl_ws_frame* meta = nullptr;
    for (int attempt = 0; attempt < 300; ++attempt) {
        const CURLcode rc = curl_ws_recv(h, buf, cap, received, &meta);
        if (rc == CURLE_OK) return true;
        if (rc == CURLE_AGAIN) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        return false;
    }
    return false;
}

}  // namespace

// ── smoke (kept from Phase 1) ──
TEST(WebSocketServerSmoke, LibCurlWebSocketSymbolsAreLinked) {
    void* const send_ptr = reinterpret_cast<void*>(&curl_ws_send);
    void* const recv_ptr = reinterpret_cast<void*>(&curl_ws_recv);
    void* const meta_ptr = reinterpret_cast<void*>(&curl_ws_meta);
    ASSERT_NE(nullptr, send_ptr);
    ASSERT_NE(nullptr, recv_ptr);
    ASSERT_NE(nullptr, meta_ptr);
}

TEST(WebSocketServerSmoke, LoopbackWebSocketConnectFailsCleanly) {
    CURL* handle = curl_easy_init();
    ASSERT_NE(nullptr, handle);
    curl_easy_setopt(handle, CURLOPT_URL, "ws://127.0.0.1:1/");
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 1000L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS,        2000L);
    curl_easy_setopt(handle, CURLOPT_CONNECT_ONLY,      2L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL,          1L);
    const CURLcode rc = curl_easy_perform(handle);
    EXPECT_NE(CURLE_OK, rc) << "expected ws://127.0.0.1:1 to fail, got: "
                            << curl_easy_strerror(rc);
    curl_easy_cleanup(handle);
}

// ── real server regression ──

// Handshake then a single Custom-over-WS round-trip echoes the message back.
TEST(WebSocketServer, BinaryMessageRoundTrips) {
    WsEchoHandler handler;
    ServerRunner srv(handler);
    ASSERT_TRUE(srv.ok());

    CURL* h = curl_easy_init();
    ASSERT_NE(nullptr, h);
    curl_easy_setopt(h, CURLOPT_URL, wsUrl().c_str());
    curl_easy_setopt(h, CURLOPT_CONNECT_ONLY, 2L);   // WS handshake then hand over
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, 5000L);
    ASSERT_EQ(CURLE_OK, curl_easy_perform(h)) << "ws handshake failed";

    const std::vector<char> msg = makeMessage(16);
    size_t sent = 0;
    ASSERT_EQ(CURLE_OK,
              curl_ws_send(h, msg.data(), msg.size(), &sent, 0, CURLWS_BINARY));
    EXPECT_EQ(msg.size(), sent);

    char buf[256];
    size_t got = 0;
    ASSERT_TRUE(wsRecvFull(h, buf, sizeof(buf), &got)) << "ws recv failed";
    ASSERT_EQ(msg.size(), got);
    EXPECT_EQ(0, std::memcmp(buf, msg.data(), msg.size()));

    curl_easy_cleanup(h);
}

// Many round-trips on one upgraded connection (WaitingForNextWebSocketMessage
// recycle path).
TEST(WebSocketServer, ManyRoundTripsOnOneConnection) {
    WsEchoHandler handler;
    ServerRunner srv(handler);
    ASSERT_TRUE(srv.ok());

    CURL* h = curl_easy_init();
    ASSERT_NE(nullptr, h);
    curl_easy_setopt(h, CURLOPT_URL, wsUrl().c_str());
    curl_easy_setopt(h, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, 5000L);
    ASSERT_EQ(CURLE_OK, curl_easy_perform(h)) << "ws handshake failed";

    for (int i = 0; i < 120; ++i) {
        const std::vector<char> msg = makeMessage(8 + (i % 32));
        size_t sent = 0;
        ASSERT_EQ(CURLE_OK,
                  curl_ws_send(h, msg.data(), msg.size(), &sent, 0, CURLWS_BINARY))
            << "send " << i;
        ASSERT_EQ(msg.size(), sent) << "send " << i;

        char buf[256];
        size_t got = 0;
        ASSERT_TRUE(wsRecvFull(h, buf, sizeof(buf), &got)) << "recv " << i;
        ASSERT_EQ(msg.size(), got) << "round-trip " << i;
        EXPECT_EQ(0, std::memcmp(buf, msg.data(), msg.size())) << "round-trip " << i;
    }

    curl_easy_cleanup(h);
}

#endif  // SECURITYSOCKET_TEST_USE_CURL
