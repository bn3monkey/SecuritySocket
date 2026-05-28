#ifndef __BN3MONKEY_TLS_HELPER__
#define __BN3MONKEY_TLS_HELPER__

#if defined(SECURITYSOCKET_TLS)
#include <openssl/ssl.h>
#include <openssl/err.h>

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
inline int32_t SSL_accept(SSL* ssl)
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
inline int32_t SSL_peek(SSL* ssl, void* buffer, size_t size)
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

// TLS version constants
static constexpr int TLS1_2_VERSION = 0x0303;
static constexpr int TLS1_3_VERSION = 0x0304;

// Verify mode constants
static constexpr int SSL_VERIFY_NONE = 0;
static constexpr int SSL_VERIFY_PEER = 1;

// Certificate file type
static constexpr int SSL_FILETYPE_PEM = 1;

// Password callback type
using pem_password_cb = int(*)(char*, int, int, void*);

// Version range
inline int SSL_CTX_set_min_proto_version(SSL_CTX*, int) { return 1; }
inline int SSL_CTX_set_max_proto_version(SSL_CTX*, int) { return 1; }

// Cipher suite configuration
inline int SSL_CTX_set_cipher_list(SSL_CTX*, const char*) { return 1; }
inline int SSL_CTX_set_ciphersuites(SSL_CTX*, const char*) { return 1; }

// Server certificate verification
inline void SSL_CTX_set_verify(SSL_CTX*, int, int(*)(int, void*)) {}
inline int  SSL_CTX_load_verify_locations(SSL_CTX*, const char*, const char*) { return 1; }
inline int  SSL_CTX_set_default_verify_paths(SSL_CTX*) { return 1; }

// Client certificate
inline void SSL_CTX_set_default_passwd_cb(SSL_CTX*, pem_password_cb) {}
inline void SSL_CTX_set_default_passwd_cb_userdata(SSL_CTX*, void*) {}
inline int  SSL_CTX_use_certificate_file(SSL_CTX*, const char*, int) { return 1; }
inline int  SSL_CTX_use_PrivateKey_file(SSL_CTX*, const char*, int) { return 1; }

// Hostname / SNI
inline int SSL_set_tlsext_host_name(SSL*, const char*) { return 1; }
inline int SSL_set1_host(SSL*, const char*) { return 1; }

// X.509 type stub (used only when SECURITYSOCKET_TLS is not defined)
using X509 = void;

// X.509 verify result codes
static constexpr long X509_V_OK                                 = 0;
static constexpr long X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT    = 18;
static constexpr long X509_V_ERR_HOSTNAME_MISMATCH              = 62;
// Returned when the peer's IP address does not match the certificate's IP SAN.
// (Distinct from X509_V_ERR_HOSTNAME_MISMATCH which covers DNS names.)
static constexpr long X509_V_ERR_IP_ADDRESS_MISMATCH            = 64;

// SSL reason codes (used with ERR_GET_REASON)
static constexpr int SSL_R_UNSUPPORTED_PROTOCOL                 = 258;
static constexpr int SSL_R_NO_PROTOCOLS_AVAILABLE               = 191;
static constexpr int SSL_R_NO_CIPHERS_AVAILABLE                 = 181;
static constexpr int SSL_R_NO_SHARED_CIPHER                     = 193;
static constexpr int SSL_R_SSLV3_ALERT_HANDSHAKE_FAILURE        = 1040;
static constexpr int SSL_R_TLSV13_ALERT_CERTIFICATE_REQUIRED    = 1116;
static constexpr int SSL_R_TLSV1_ALERT_UNKNOWN_CA               = 1048;
static constexpr int SSL_R_TLSV1_ALERT_PROTOCOL_VERSION = 1070;

// Error queue
inline long         SSL_get_verify_result(SSL*)      { return X509_V_OK; }
inline unsigned long ERR_peek_error()                { return 0; }
inline int          ERR_GET_REASON(unsigned long e)  { return static_cast<int>(e & 0xFFF); }
inline void         ERR_clear_error()                {}

// Returns the verify mode set on this SSL session (e.g., SSL_VERIFY_NONE or SSL_VERIFY_PEER).
inline int          SSL_get_verify_mode(const SSL*)  { return SSL_VERIFY_NONE; }

// Returns the negotiated protocol version (e.g., TLS1_2_VERSION or TLS1_3_VERSION).
inline int          SSL_version(const SSL*)          { return 0; }

// Returns the negotiated cipher for this SSL session, or nullptr if not yet negotiated.
inline void*        SSL_get_current_cipher(const SSL*) { return nullptr; }

// Returns the certificate configured for this SSL session, or nullptr if none.
inline X509*        SSL_get_certificate(const SSL*)  { return nullptr; }

// ---- Handshake info-callback support -------------------------------------
// These constants/functions back the OpenSSL handshake-progress callback that
// the project registers via SSL_CTX_set_info_callback. With TLS disabled the
// callback never fires, but the symbols must still resolve so the code that
// references them compiles.

// State machine flags (the high bits of `where` passed to the info callback).
static constexpr int SSL_ST_CONNECT          = 0x1000;
static constexpr int SSL_ST_ACCEPT           = 0x2000;
// Mask used to strip the callback-event bits from `where` and recover the
// pure state-machine bits (e.g., `where & ~SSL_ST_MASK`).
static constexpr int SSL_ST_MASK             = 0x0FFF;

// Callback-event flags (the low bits of `where`).
static constexpr int SSL_CB_LOOP             = 0x01;
static constexpr int SSL_CB_EXIT             = 0x02;
static constexpr int SSL_CB_READ             = 0x04;
static constexpr int SSL_CB_WRITE            = 0x08;
static constexpr int SSL_CB_ALERT            = 0x4000;
static constexpr int SSL_CB_HANDSHAKE_START  = 0x10;
static constexpr int SSL_CB_HANDSHAKE_DONE   = 0x20;

// SSL_CTX info callback signature (matches OpenSSL).
using SSL_info_callback_fn = void (*)(const SSL*, int, int);
inline void SSL_CTX_set_info_callback(SSL_CTX*, SSL_info_callback_fn) {}

// Per-session diagnostic strings.
inline const char* SSL_state_string_long(const SSL*)       { return ""; }
inline const char* SSL_alert_type_string_long(int)         { return ""; }
inline const char* SSL_alert_desc_string_long(int)         { return ""; }

// Per-session user data slots used by the info callback to recover the
// project's TlsEventCallback pointer at runtime.
inline int   SSL_set_ex_data(SSL*, int, void*)             { return 1; }
inline void* SSL_get_ex_data(const SSL*, int)              { return nullptr; }

#endif // USING_TLS

#endif // __BN3MONKEY_TLS_HELPER__