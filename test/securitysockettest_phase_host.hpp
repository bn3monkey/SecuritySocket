#if !defined(__BN3MONKEY_TEST_PHASE_HOST__)
#define __BN3MONKEY_TEST_PHASE_HOST__

// Shared test harness for the per-protocol ConnectionPhase unit tests
// (sniff / custom / http / websocket).
//
// A ConnectionPhase only talks to the host through the PhaseHost interface and
// returns the next ConnectionState. FakePhaseHost implements PhaseHost (and the
// ClientConnection view handed to user handlers) against two in-memory
// StagingBuffers, so a test can:
//   1. feed() raw bytes into the input buffer (recv-equivalent),
//   2. drive() the phase's onReadable and read back the ConnectionState,
//   3. completeSend() to mimic the host flushing output + onSendComplete.
//
// SLOW dispatch is run inline (dispatchSlow just calls the closure) so the
// response is produced synchronously and the same assertions apply as FAST.
// The SocketMultiEventListener is never open()'d: phases only forward it to
// dispatchSlow, which ignores it here.

#include <SecuritySocket.hpp>

#include "../src/implementation/connection/ConnectionPhase.hpp"   // PhaseHost, ConnectionPhase, StagingBuffer
#include "../src/implementation/connection/ConnectionState.hpp"
#include "../src/implementation/SocketEvent.hpp"                   // SocketMultiEventListener
#include "../src/implementation/protocol/WsFrameCodec.hpp"         // WsOpcode (frame builder)

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

namespace Bn3Monkey { class HttpRouterImpl; }

namespace PhaseTest
{
    class FakePhaseHost : public Bn3Monkey::ClientConnection,
                          public Bn3Monkey::PhaseHost
    {
    public:
        explicit FakePhaseHost(size_t capacity = 64 * 1024)
            : _in(capacity), _out(capacity) {}

        // ── ClientConnection (user-facing view) ──
        const char* ip()          const override { return "127.0.0.1"; }
        uint32_t    port()        const override { return 0; }
        bool        isSecure()    const override { return false; }
        bool        isWebSocket() const override { return _is_websocket; }

        // ── PhaseHost ──
        Bn3Monkey::StagingBuffer& input()  override { return _in; }
        Bn3Monkey::StagingBuffer& output() override { return _out; }
        Bn3Monkey::ClientConnection&             connection()    override { return *this; }
        Bn3Monkey::HttpRouterImpl*               router()        override { return _router; }
        Bn3Monkey::CustomProtocolRequestHandler* customHandler() override { return _custom; }
        const char*                              wsPattern() const override { return _ws_pattern; }
        void setWebSocket(bool on) override { _is_websocket = on; }
        void dispatchSlow(std::function<void()> call,
                          Bn3Monkey::SocketMultiEventListener&) override
        {
            _slow_used = true;
            call();   // run inline so the response lands in output() for asserts
        }

        // ── test wiring ──
        void setRouter   (Bn3Monkey::HttpRouterImpl* r)               { _router = r; }
        void setCustom   (Bn3Monkey::CustomProtocolRequestHandler* c) { _custom = c; }
        void setWsPattern(const char* p)                             { _ws_pattern = p; }

        // recv-equivalent: append n bytes at the tail. Resets the cursor to 0
        // when the buffer has been fully drained (mirrors the host's recvChunk),
        // so repeated feed/drain cycles don't grow the buffer unbounded.
        void feed(const void* data, size_t n)
        {
            if (_in.empty()) _in.clear();
            _in.reserve(n);
            std::memcpy(_in.tail(), data, n);
            _in.fill(n);
        }
        void feed(const std::vector<char>& bytes) { feed(bytes.data(), bytes.size()); }

        // One parse pass (host's per-READ-event entry into the active phase).
        Bn3Monkey::ConnectionState drive(Bn3Monkey::ConnectionPhase& phase)
        {
            _slow_used = false;
            return phase.onReadable(*this, _listener);
        }

        // The host flushed output() (we clear it to mimic the drained socket)
        // and now asks the phase for the post-send state.
        Bn3Monkey::ConnectionState completeSend(Bn3Monkey::ConnectionPhase& phase)
        {
            _out.clear();
            return phase.onSendComplete(*this);
        }

        // ── observation ──
        size_t      outputSize()   const { return _out.pending(); }
        const char* outputData()         { return static_cast<const char*>(_out.head()); }
        size_t      inputPending() const { return _in.pending(); }
        bool        slowUsed()     const { return _slow_used; }
        void        clearInput()         { _in.clear(); }

    private:
        Bn3Monkey::StagingBuffer _in;
        Bn3Monkey::StagingBuffer _out;
        Bn3Monkey::HttpRouterImpl*               _router{ nullptr };
        Bn3Monkey::CustomProtocolRequestHandler* _custom{ nullptr };
        const char*                              _ws_pattern{ nullptr };
        bool _is_websocket{ false };
        bool _slow_used{ false };
        Bn3Monkey::SocketMultiEventListener _listener;   // never open()'d
    };

    // ── stub Custom Protocol handler (used by custom + websocket tests) ──
    // 8-byte fixed header: { int32 mode_tag, int32 payload_len }. mode_tag
    // selects the dispatch mode so a single handler can exercise every path.
    struct StubHeader
    {
        int32_t mode_tag{ 0 };      // 0 FAST, 1 SLOW, 2 READ_STREAM, 3 WRITE_STREAM
        int32_t payload_len{ 0 };
    };

    class StubCustomHandler : public Bn3Monkey::CustomProtocolRequestHandler
    {
    public:
        using Mode = Bn3Monkey::RequestProcessingMode;

        size_t headerSize() override { return sizeof(StubHeader); }
        size_t payloadSize(const void* header) override
        {
            return static_cast<size_t>(
                reinterpret_cast<const StubHeader*>(header)->payload_len);
        }
        Mode classifyMode(const void* header) override
        {
            switch (reinterpret_cast<const StubHeader*>(header)->mode_tag) {
            case 1:  return Mode::SLOW;
            case 2:  return Mode::READ_STREAM;
            case 3:  return Mode::WRITE_STREAM;
            default: return Mode::FAST;
            }
        }
        void process(const Bn3Monkey::ClientConnection&,
                     const Bn3Monkey::CustomProtocolRequest& req,
                     Bn3Monkey::CustomProtocolResponse& res) override
        {
            ++process_count;
            // Echo the header back as a fixed 8-byte response.
            auto* h = reinterpret_cast<const StubHeader*>(req.header());
            auto* out = reinterpret_cast<StubHeader*>(res.data());
            out->mode_tag    = h->mode_tag;
            out->payload_len = h->payload_len;
            res.setLength(sizeof(StubHeader));
        }
        void processWithoutResponse(const Bn3Monkey::ClientConnection&,
                                    const Bn3Monkey::CustomProtocolRequest&) override
        {
            ++no_response_count;
        }

        int process_count{ 0 };
        int no_response_count{ 0 };
    };

    // Build one Custom Protocol message (StubHeader + payload bytes) into a
    // contiguous byte vector. payload is filled with a deterministic pattern.
    inline std::vector<char> makeCustomMessage(int32_t mode_tag, int32_t payload_len)
    {
        std::vector<char> msg(sizeof(StubHeader) + static_cast<size_t>(payload_len));
        StubHeader h{ mode_tag, payload_len };
        std::memcpy(msg.data(), &h, sizeof(h));
        for (int32_t i = 0; i < payload_len; ++i) {
            msg[sizeof(StubHeader) + static_cast<size_t>(i)] =
                static_cast<char>('A' + (i % 26));
        }
        return msg;
    }

    // ── RFC 6455 client->server frame builder (masked, as the codec requires) ──
    // opcode/fin per arg; payload masked with a fixed 4-byte key. Only the
    // <126-byte length form is emitted (test payloads are small).
    inline std::vector<char> makeClientFrame(Bn3Monkey::WsOpcode opcode,
                                             const char* payload, size_t len,
                                             bool fin)
    {
        const unsigned char key[4] = { 0x12, 0x34, 0x56, 0x78 };
        std::vector<char> f;
        f.push_back(static_cast<char>((fin ? 0x80 : 0x00) |
                                      static_cast<uint8_t>(opcode)));
        f.push_back(static_cast<char>(0x80 | static_cast<uint8_t>(len)));  // mask bit + len7
        for (int i = 0; i < 4; ++i) f.push_back(static_cast<char>(key[i]));
        for (size_t i = 0; i < len; ++i) {
            f.push_back(static_cast<char>(payload[i] ^ key[i % 4]));
        }
        return f;
    }

    inline std::vector<char> makeClientBinaryMessage(int32_t mode_tag, int32_t payload_len,
                                                     bool fin = true)
    {
        std::vector<char> msg = makeCustomMessage(mode_tag, payload_len);
        return makeClientFrame(Bn3Monkey::WsOpcode::BINARY, msg.data(), msg.size(), fin);
    }
}

#endif // __BN3MONKEY_TEST_PHASE_HOST__
