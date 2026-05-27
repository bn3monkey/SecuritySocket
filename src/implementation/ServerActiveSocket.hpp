#if !defined(__BN3MONKEY__SERVERACTIVESOCKET__)
#define __BN3MONKEY__SERVERACTIVESOCKET__

#include "../SecuritySocket.hpp"
#include "BaseSocket.hpp"
#include "SocketHelper.hpp"

#include <cstdint>

#include "TlsHelper.hpp"

namespace Bn3Monkey
{
    
    class ServerActiveSocket : public BaseSocket
    {
    public:
        ServerActiveSocket() {}
        ServerActiveSocket(int32_t sock, void* addr, void* ssl_context = nullptr);
        virtual ~ServerActiveSocket();

        inline NetworkResult result() { return _result; }
        virtual void close();
		virtual NetworkResult read(void* buffer, size_t size);
        virtual NetworkResult write(const void* buffer, size_t size);

        inline const char* ip() const { return _client_ip; }
        inline int port() const { return _client_port; }

        void setSocketBufferSize(size_t size);
        // Disable Nagle's algorithm on this connection. Call right after accept()
        // for latency-sensitive servers (e.g. broadcast/event delivery) so each
        // write() flushes immediately instead of being coalesced by the kernel.
        void setNoDelay();

    protected:
        char _client_ip[22]{ 0 };
        int _client_port = 0;
    };

    class TlsServerActiveSocket : public ServerActiveSocket
    {
    public:
        TlsServerActiveSocket() {}
        TlsServerActiveSocket(int32_t sock, void* addr, void* ssl_context);
        virtual ~TlsServerActiveSocket();
        
        virtual void close();
		virtual NetworkResult read(void* buffer, size_t size);
        virtual NetworkResult write(const void* buffer, size_t size);
    private:
        SSL* ssl {nullptr};
    };

    using ServerActiveSocketContainer = SocketContainer<ServerActiveSocket, TlsServerActiveSocket>;

}


#endif // __BN3MONKEY__SERVERACTIVESOCKET__