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
        ServerActiveSocket(int32_t sock, void* addr, void* ssl_context = nullptr);
        virtual ~ServerActiveSocket();

        inline SocketResult result() { return _result; }
        virtual void close();
		virtual SocketResult read(void* buffer, size_t size);
        virtual SocketResult write(const void* buffer, size_t size);

        inline const char* ip() const { return _client_ip; }
        inline int port() const { return _client_port; }
    
    protected:
        char _client_ip[22]{ 0 };
        int _client_port = 0;
    };

    class TLSServerActiveSocket : public ServerActiveSocket
    {
    public:
        TLSServerActiveSocket() {}
        TLSServerActiveSocket(int32_t sock, void* addr, void* ssl_context);
        virtual ~TLSServerActiveSocket();
        
        virtual void close();
		virtual SocketResult read(void* buffer, size_t size);
        virtual SocketResult write(const void* buffer, size_t size);
    private:
        SSL* ssl {nullptr};
    };

    using ServerActiveSocketContainer = SocketContainer<ServerActiveSocket, TLSServerActiveSocket>;

}


#endif // __BN3MONKEY__SERVERACTIVESOCKET__