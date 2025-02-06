#include "SocketBroadcastServer.hpp"
#include <stdexcept>

using namespace Bn3Monkey;

SocketBroadcastServerImpl::~SocketBroadcastServerImpl()
{
    throw std::runtime_error("Not Implemented");
}
SocketResult SocketBroadcastServerImpl::open(size_t num_of_clients)
{
    throw std::runtime_error("Not Implemented");
}

inline void removeDisconnectedSocket()
{
        throw std::runtime_error("Not Implemented");
}
inline void findNewSocket()
{
    throw std::runtime_error("Not Implemented");
}

SocketResult SocketBroadcastServerImpl::write(const void* buffer, size_t size)
{
    throw std::runtime_error("Not Implemented");
}

void SocketBroadcastServerImpl::close()
{
    throw std::runtime_error("Not Implemented");
}