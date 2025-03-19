#if !defined(__BN3MONKEY_SOCKET_HELPER__)
#define __BN3MONKEY_SOCKET_HELPER__

#include <cstdint>
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <sys/socket.h> // SOL_SOCKET
#include <sys/time.h> // timeval
#endif



inline void setNonBlockingMode(int32_t socket)
{
#ifdef _WIN32
	unsigned long mode{ 1 };
	ioctlsocket(socket, FIONBIO, &mode);
#else
	int _flags = fcntl(socket, F_GETFL, 0);
	fcntl(socket, F_SETFL, _flags | O_NONBLOCK);
#endif
}

inline void setBlockingMode(int32_t socket)
{
#ifdef _WIN32
	unsigned long mode{ 0 };
	ioctlsocket(socket, FIONBIO, &mode);
#else
	int _flags = fcntl(socket, F_GETFL, 0);
	fcntl(socket, F_SETFL, _flags | ~O_NONBLOCK);

#endif
}

inline void setTimeout(int32_t socket, uint32_t read_timeout_ms, uint32_t write_timeout_ms)
{
#ifdef _WIN32
	setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&read_timeout_ms, sizeof(read_timeout_ms));
	setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&write_timeout_ms, sizeof(write_timeout_ms));

#else
	timeval read_timeout;
	read_timeout.tv_sec = (time_t)read_timeout_ms / (time_t)1000;
	read_timeout.tv_usec = (suseconds_t)read_timeout_ms * (suseconds_t)1000 - (suseconds_t)(read_timeout.tv_sec * (suseconds_t)1000000);
	setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout));

	timeval write_timeout;
	read_timeout.tv_sec = (time_t)write_timeout_ms / (time_t)1000;
	read_timeout.tv_usec = (suseconds_t)write_timeout_ms * (suseconds_t)1000 - (suseconds_t)(write_timeout.tv_sec * (suseconds_t)1000000);

	setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &write_timeout, sizeof(write_timeout));

#endif
}


#endif // __BN3MONKEY_SOCKET_HELPER__