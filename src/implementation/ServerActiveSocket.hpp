#if !defined(__BN3MONKEY__SERVERACTIVESOCKET__)
#define __BN3MONKEY__SERVERACTIVESOCKET__

#include "../SecuritySocket.hpp"
#include "BaseSocket.hpp"
#include "SocketHelper.hpp"

#include <cstdint>

#include "TLSHelper.hpp"

namespace Bn3Monkey
{
    
    class ServerActiveSocket : public BaseSocket
    {
    public:
        ServerActiveSocket() {}
        ServerActiveSocket(int32_t sock, struct sockaddr_in* addr);
        virtual ~ServerActiveSocket();

        inline SocketResult result() { return _result; }
        virtual void close();
		virtual int read(void* buffer, size_t size);
        virtual int write(const void* buffer, size_t size);

        inline const char* ip() const { return _client_ip; }
        inline int port() const { return _client_port; }
    
    protected:
        char _client_ip[INET_ADDRSTRLEN]{ 0 };
        int _client_port = 0;
    };

    class TLSServerActiveSocket : public ServerActiveSocket
    {
    public:
        TLSServerActiveSocket() {}
        TLSServerActiveSocket(int32_t socket, struct sockaddr_in* addr) {}
        TLSServerActiveSocket(SSL_CTX* ctx, int32_t sock);
        virtual ~TLSServerActiveSocket();
        
        virtual void close();
		virtual int read(void* buffer, size_t size);
        virtual int write(const void* buffer, size_t size);
    private:
        SSL* ssl {nullptr};
    };

    using ServerActiveSocketContainer = SocketContainer<ServerActiveSocket, TLSServerActiveSocket>;

}


#endif // __BN3MONKEY__SERVERACTIVESOCKET__