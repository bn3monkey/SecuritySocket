#if !defined(__BN3MONKEY__SERVERACTIVESOCKET__)
#define __BN3MONKEY__SERVERACTIVESOCKET__

#include "../SecuritySocket.hpp"
#include "BaseSocket.hpp"
#include "SocketHelper.hpp"

#include <cstdint>


#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#include <Winsock2.h>
#include <WS2tcpip.h>
#else
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#endif

namespace Bn3Monkey
{
    using ServerActiveSocketContainer = SocketContainer<ServerActiveSocket, TLSServerActiveSocket>;

    class ServerActiveSocket : public BaseSocket
    {
    public:
        ServerActiveSocket(int32_t sock);
        virtual ~ServerActiveSocket();

        inline SocketResult result() { return _result; }
        virtual void close();
		virtual int read(void* buffer, size_t size);
        virtual int write(const void* buffer, size_t size);
    
    protected:
        SocketResult _result;
    };

    class TLSServerActiveSocket : public ServerActiveSocket
    {
        TLSServerActiveSocket(SSL_CTX* ctx, int32_t sock);
        virtual ~TLSServerActiveSocket();
        
        virtual void close();
		virtual int read(void* buffer, size_t size);
        virtual int write(const void* buffer, size_t size);
    private:
        SSL* ssl {nullptr};
    };
}

#endif // __BN3MONKEY__SERVERACTIVESOCKET__