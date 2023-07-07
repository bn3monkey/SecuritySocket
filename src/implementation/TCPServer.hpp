#if !defined(__BN3MONKEY__TCPSERVER__)
#define __BN3MONKEY__TCPSERVER__
#include "../SecuritySocket.hpp"
#include "TCPSocket.hpp"
#include "TCPStream.hpp"

#include <type_traits>
#include <atomic>

#include <mutex>
#include <thread>
#include <condition_variable>

namespace Bn3Monkey
{
	class TCPServerImpl : public TCPStream
	{
	public:
		TCPServerImpl(const TCPConfiguration& configuration, TCPEventHandler& handler);
		virtual ~TCPServerImpl();

		void close();

	private:
				
		static constexpr size_t container_size = sizeof(TCPSocket) > sizeof(TLSSocket) ? sizeof(TCPSocket) : sizeof(TLSSocket);
		char _container[container_size];
		TCPSocket* _socket{ nullptr };

		TCPEventHandler& _handler;
	};
}


#endif