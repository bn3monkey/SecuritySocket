#if !defined(__BN3MONKEY_REQUEST_CLIENT__)
#define __BN3MONKEY_REQUEST_CLIENT__

#include "../../SecuritySocket.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Bn3Monkey
{
    // Backing store for RequestClient. In raw mode send()/receive() forward to
    // the Client transport's write()/read(). In WS-tunnelled mode connect()
    // performs the HTTP Upgrade handshake and send()/receive() (un)frame masked
    // binary messages, handling PING/PONG/CLOSE control frames internally.
    class RequestClientImpl
    {
    public:
        RequestClientImpl(const NetworkConfiguration& cfg,
                          const WebSocketConfiguration& ws);   // ws.valid()==false => raw

        bool isWebSocket() const { return _is_ws; }

        NetworkResult connect(Client& transport);
        NetworkResult send   (Client& transport, const void* data, size_t len);
        NetworkResult receive(Client& transport, void* buf, size_t buf_size,
                              size_t* received, uint64_t timeout_ms);

    private:
        NetworkResult handshake(Client& transport);
        // Build + send a masked control/data frame. opcode per WsOpcode.
        NetworkResult sendFrame(Client& transport, uint8_t opcode,
                                const char* payload, size_t len);
        void fillMaskKey(unsigned char out[4]);

        bool _is_ws    = false;
        bool _opened   = false;
        bool _connected = false;
        bool _ws_closed = false;

        char _host[256];
        char _ws_path[256];

        std::vector<char> _recv;       // undecoded server bytes (WS)
        std::vector<char> _assembly;   // reassembled fragmented message (WS)

        // Lightweight xorshift64 for WS masking keys (seeded once from
        // std::random_device). Kept tiny on purpose: std::mt19937's state is
        // ~5 KB on libstdc++ (uint_fast32_t == 8 bytes) and would not fit the
        // inline container.
        uint64_t _rng_state = 0;
    };
}

#endif
