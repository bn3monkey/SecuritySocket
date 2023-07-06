#if !defined(__BN3MONKEY__TCPCLIENT__)
#define __BN3MONKEY__TCPCLIENT__

#include "../SecuritySocket.hpp"
#include "TCPSocket.hpp"

#include <type_traits>
#include <atomic>

#include <mutex>
#include <thread>
#include <condition_variable>

namespace Bn3Monkey
{
	class TCPClientImpl
	{
	public:
		TCPClientImpl(const TCPClientConfiguration& configuration, TCPEventHandler& handler);
		virtual ~TCPClientImpl();

		inline ConnectionResult getLastError() {
			return _last_error.load();
		}
	private:
		inline void setLastError(const ConnectionResult& result) {
			_last_error.store(result);
		}
		std::atomic<ConnectionResult> _last_error;
		
				
		static constexpr size_t container_size = sizeof(TCPSocket) > sizeof(TLSSocket) ? sizeof(TCPSocket) : sizeof(TLSSocket);
		char _container[container_size];
		TLSSocket& _socket{ *reinterpret_cast<TLSSocket*>(_container)};

		TCPEventHandler& _handler;

		std::thread _reader;
		std::thread _writer;
		std::mutex _mtx;
	};
}

#endif