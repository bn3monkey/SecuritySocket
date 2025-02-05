#if !defined(__BN3MONKEY_SOCKET_HELPER__)
#define __BN3MONKEY_SOCKET_HELPER__

#include <cstdint>
#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>

inline void setNonBlockingMode(int32_t socket)
{
#ifdef _WIN32
	unsigned long mode{ 1 };
	ioctlsocket(socket, FIONBIO, &mode);
#else
	_flags = fcntl(socket, F_GETFL, 0);
	fcntl(socket, F_SETFL, _flags | O_NONBLOCK);
#endif
}

inline void initializeSSL()
{
	static bool is_initialized {false};
	if (!is_initialized)
	{
		SSL_library_init();
		OpenSSL_add_all_algorithms();
		SSL_load_error_strings();
		is_initialized = true;
	}
}

#endif // __BN3MONKEY_SOCKET_HELPER__