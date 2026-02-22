#ifndef __BN3MONKEY_TLS_HELPER__
#define __BN3MONKEY_TLS_HELPER__

#if defined(SECURITYSOCKET_TLS)
#include <openssl/ssl.h>
#include <openssl/err.h>

void checkTLSFunction() {
    TLS1_2_VERSION
}
#else


using SSL_CTX = void;
using SSL = void;
using METHOD = void;

inline METHOD* TLS_client_method()
{
    return nullptr;
}

inline SSL_CTX* SSL_CTX_new(METHOD* method)
{
    (void)method;
    return nullptr;
}

inline SSL* SSL_new(SSL_CTX* context)
{
    (void)context;
    return nullptr;
}
inline void SSL_CTX_free(SSL_CTX* context)
{
    (void)context;
    return;
}

inline int32_t SSL_connect(SSL* ssl)
{
    (void)ssl;
    return 0;
}
inline void SSL_shutdown(SSL* ssl)
{
    (void)ssl;
    return;
}
inline void SSL_free(SSL* ssl)
{
    (void)ssl;
    return;
}
inline int32_t SSL_set_fd(SSL* ssl, int32_t socket)
{
    (void)ssl;
	(void)socket;
    return 0;
}
inline int32_t SSL_write(SSL* ssl, const void* buffer, size_t size)
{
	(void)ssl;
	(void)buffer;
	(void)size;
    return 0;
}
inline int32_t SSL_read(SSL* ssl, void* buffer, size_t size)
{
    (void)ssl;
    (void)buffer;
    (void)size;
    return 0;
}
inline int32_t SSL_get_error(SSL* ssl, int32_t operation_return)
{
    (void)ssl;
	(void)operation_return;
    return 0;
}

static constexpr int32_t SSL_ERROR_SSL = 1;
static constexpr int32_t SSL_ERROR_SYSCALL = 2;
static constexpr int32_t SSL_ERROR_ZERO_RETURN = 3;
static constexpr int32_t SSL_ERROR_WANT_READ = 4;
static constexpr int32_t SSL_ERROR_WANT_WRITE = 5;

#endif // USING_TLS

#endif // __BN3MONKEY_TLS_HELPER__