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
			{
				std::lock_guard<std::mutex> lock(_last_error_lock);
				return _last_error;
			}
		}
	private:
		inline void setLastError(const ConnectionResult& result) {
			{
				std::lock_guard<std::mutex> lock(_last_error_lock);
				_last_error = result;
			}
		}

		std::mutex _last_error_lock;
		ConnectionResult _last_error;
		
				
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