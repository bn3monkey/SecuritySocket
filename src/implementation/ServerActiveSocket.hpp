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
    class ServerActiveSocket : public BaseSocket
    {
    public:
        inline bool isOpened() { return _is_opened; }

        SocketResult open(int32_t sock);

        virtual void close();
		virtual int read(void* buffer, size_t size);
        virtual int write(const void* buffer, size_t size);
    private:
        bool _is_opened {false};
    };

    class TLSServerActiveSocket : public ServerActiveSocket
    {
        SocketResult open(SSL_CTX* ctx, int32_t sock);
        
        virtual void close();
		virtual int read(void* buffer, size_t size);
        virtual int write(const void* buffer, size_t size);
    private:
        SSL* ssl;
    };
}

#endif // __BN3MONKEY__SERVERACTIVESOCKET__