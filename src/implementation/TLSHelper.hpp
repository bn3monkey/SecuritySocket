#ifndef __BN3MONKEY_TLS_HELPER__
#define __BN3MONKEY_TLS_HELPER__

#if defined(SECURITYSOCKET_TLS)
#include <openssl/ssl.h>
#include <openssl/err.h>

inline void initializeTLS()
{
    static bool is_initialized{ false };
    if (!is_initialized)
    {
        SSL_library_init();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();
        is_initialized = true;
    }
}

#else

inline void initializeTLS()
{
    return;
}

using SSL_CTX = void;
using SSL = void;
using METHOD = void;

inline METHOD* TLS_client_method()
{
    return nullptr;
}

inline SSL_CTX* SSL_CTX_new(METHOD* method)
{
    return nullptr;
}

inline SSL* SSL_new(SSL_CTX* context)
{
    return nullptr;
}
inline void SSL_CTX_free(SSL_CTX* context)
{
    return;
}

inline int32_t SSL_connect(SSL* ssl)
{
    return 0;
}
inline void SSL_shutdown(SSL* ssl)
{
    return;
}
inline void SSL_free(SSL* ssl)
{
    return;
}
inline int32_t SSL_set_fd(SSL* ssl, int32_t socket)
{
    return 0;
}
inline int32_t SSL_write(SSL* ssl, const void* buffer, size_t size)
{
    return 0;
}
inline int32_t SSL_read(SSL* ssl, void* buffer, size_t size)
{
    return 0;
}
inline int32_t SSL_get_error(SSL* ssl, int32_t operation_return)
{
    return 0;
}

static constexpr int32_t SSL_ERROR_SSL = 1;
static constexpr int32_t SSL_ERROR_SYSCALL = 2;
static constexpr int32_t SSL_ERROR_ZERO_RETURN = 3;
static constexpr int32_t SSL_ERROR_WANT_READ = 4;
static constexpr int32_t SSL_ERROR_WANT_WRITE = 5;

#endif // USING_TLS

#endif // __BN3MONKEY_TLS_HELPER__