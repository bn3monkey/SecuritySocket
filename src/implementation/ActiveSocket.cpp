#include "ActiveSocket.hpp"
#include "SocketResult.hpp"

using namespace Bn3Monkey;

SocketResult ActiveSocket::open(const SocketAddress& address)
{

}
void ActiveSocket::close()
{

}
SocketResult ActiveSocket::listen(size_t num_of_clients)
{

}
SocketResult ActiveSocket::poll()
{

}
int32_t ActiveSocket::accept()
{

}
int32_t ActiveSocket::read(int32_t client_idx, void* buffer, size_t size)
{

}
int32_t ActiveSocket::write(int32_t client_idx, const void* buffer, size_t size) 
{

}
void ActiveSocket::drop(int32_t client_idx)
{

}


SocketResult TLSActiveSocket::open(const SocketAddress& address)
{

}
void TLSActiveSocket::close()
{
    
}
SocketResult TLSActiveSocket::listen(size_t num_of_clients)
{

}
SocketResult TLSActiveSocket::poll()
{

}
int32_t TLSActiveSocket::accept()
{

}
int32_t TLSActiveSocket::read(int32_t client_idx, void* buffer, size_t size)
{

}
int32_t TLSActiveSocket::write(int32_t client_idx, const void* buffer, size_t size) 
{

}
void TLSActiveSocket::drop(int32_t client_idx)
{

}