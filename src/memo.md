# Memo

## Timeout Setup for blocking mode

```
#ifdef _WIN32
	constexpr size_t size = sizeof(uint32_t);
	const char* read_timeout_ref = reinterpret_cast<const char*>(&_read_timeout);
	const char* write_timeout_ref = reinterpret_cast<const char*>(&_write_timeout);
#else
	constexpr size_t size = sizeof(timeval);
	timeval read_timeout_value;
	read_timeout_value.tv_sec = (time_t)_read_timeout / (time_t)1000;
	read_timeout_value.tv_usec = (suseconds_t)_read_timeout * (suseconds_t)1000 - (suseconds_t)(read_timeout_value.tv_sec * (suseconds_t)1000000);
	timeval* read_timeout_ref = &read_timeout_value;

	timeval write_timeout_value;
	write_timeout_value.tv_sec = (time_t)_write_timeout / (time_t)1000;
	write_timeout_value.tv_usec = (suseconds_t)_write_timeout * (suseconds_t)1000 - (suseconds_t)(write_timeout_value.tv_sec * (suseconds_t)1000000);
	timeval* write_timeout_ref = &write_timeout_value;
#endif

	if (setsockopt(_socket, SOL_SOCKET, SO_RCVTIMEO, read_timeout_ref, size) < 0)
	{
		return SocketResult(SocketCode::SOCKET_OPTION_ERROR, "cannot get socket receive timeout");
	}
	if (setsockopt(_socket, SOL_SOCKET, SO_SNDTIMEO, write_timeout_ref, size) < 0)
	{
		return SocketResult(SocketCode::SOCKET_OPTION_ERROR, "cannot get socket send timeout");
	}
```

## Nonblocking Mode

```
#ifdef _WIN32
	unsigned long mode{ 1 };
	ioctlsocket(_socket, FIONBIO, &mode);
#else
	_flags = fcntl(_socket, F_GETFL, 0);
	fcntl(_socket, F_SETFL, _flags | O_NONBLOCK);
#endif
}
PassiveSocket::NonBlockMode::~NonBlockMode() {
#ifdef _WIN32
	unsigned long mode{ 0 };
	ioctlsocket(_socket, FIONBIO, &mode);
#else
	fcntl(_socket, F_SETFL, _flags);
#endif
```