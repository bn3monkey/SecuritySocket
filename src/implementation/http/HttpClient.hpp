#if !defined(__BN3MONKEY_HTTP_CLIENT__)
#define __BN3MONKEY_HTTP_CLIENT__

#include "../../SecuritySocket.hpp"
#include "HttpClientRequest.hpp"
#include "HttpClientResponse.hpp"

#include <cstddef>
#include <vector>

namespace Bn3Monkey
{
    // Backing store for HttpClient. Holds the precomputed Host header value and
    // the receive scratch buffers, and drives the request/response exchange over
    // the Client transport handed in by the public facade. Connection state is
    // tracked here so calls reuse one keep-alive connection.
    class HttpClientImpl
    {
    public:
        explicit HttpClientImpl(const NetworkConfiguration& cfg);

        // Send 'req' over 'transport' and read the response. 'transport' is the
        // HttpClient itself (IS-A Client); perform() uses its public
        // open/connect/write/read. Never throws; transport failures surface
        // through the returned resultCode().
        HttpClientResponseImpl perform(Client& transport,
                                       const HttpClientRequestImpl& req);

    private:
        // Returns SUCCESS when a live connection is ready (opening/connecting
        // lazily). On failure the caller turns it into a response resultCode.
        NetworkResult ensureConnected(Client& transport);
        void drop(Client& transport);

        char _host[256];
        bool _opened    = false;
        bool _connected = false;

        std::vector<char> _recv;       // accumulates the current response
        std::vector<char> _leftover;   // bytes past one response (pipelined)
    };
}

#endif
