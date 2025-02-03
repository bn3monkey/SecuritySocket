#if !defined(__BN3MONKEY_SOCKET_HELPER__)
#define __BN3MONKEY_SOCKET_HELPER__

#include <cstdint>
#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#endif

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

#endif // __BN3MONKEY_SOCKET_HELPER__