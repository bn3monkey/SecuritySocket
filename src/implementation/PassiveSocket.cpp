#include "PassiveSocket.hpp"

#include "NetworkResult.hpp"
#include "SocketHelper.hpp"
#include "TlsTrace.hpp"

#include <cstring>

#ifdef _WIN32
#include <Winsock2.h>
#include <WS2tcpip.h>
#include <mswsock.h>
#else
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#endif

using namespace Bn3Monkey;

#if defined(_WIN32)
static inline void loadAcceptExFunction(int32_t socket, LPFN_ACCEPTEX* function_ref)
{
    GUID guid_acceptEx = WSAID_ACCEPTEX;
    DWORD bytesReturned;

    WSAIoctl(socket, 
        SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guid_acceptEx,
        sizeof(guid_acceptEx),
        function_ref,
        sizeof(LPFN_ACCEPTEX),
        &bytesReturned,
        NULL,
        NULL);
}
#endif

PassiveSocket::PassiveSocket(bool is_unix_domain, const TlsServerConfiguration& tls_configuration)
{
    (void)tls_configuration;
    if (is_unix_domain) {
        auto temp_socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
        _socket = static_cast<int32_t>(temp_socket);
    }
    else {
        auto temp_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        _socket = static_cast<int32_t>(temp_socket);
    }
    if (_socket < 0)
	{
		_result = createResult(_socket);
		return;
	}
    
	setNonBlockingMode(_socket);
}
void PassiveSocket::close()
{
#ifdef _WIN32
    shutdown(_socket, SD_BOTH);
#else
    shutdown(_socket, SHUT_RDWR);
#endif
#ifdef _WIN32
	::closesocket(_socket);
#else
	::close(_socket);
#endif
	_socket = -1;	
}

NetworkResult PassiveSocket::bind(const SocketAddress& address)
{
    NetworkResult res;


    int opt = 1;
    setsockopt(_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    int ret = ::bind(_socket, address.address(), address.size());
    if (ret < 0) {
        res = createResult(ret);
    }
    return res;
}
NetworkResult PassiveSocket::listen()
{
    NetworkResult res;

    auto ret = ::listen(_socket, SOMAXCONN);
    if (ret < 0)
    {
        res = createResult(ret);
    }
    return res;
}



ServerActiveSocketContainer PassiveSocket::accept()
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int sock = static_cast<int32_t>(::accept(_socket, (struct sockaddr*)&client_addr, &client_len));
    if (sock == 0) {
        // Do nothing
    }
    ServerActiveSocketContainer container{false, sock, (void*)&client_addr, nullptr};
    return container;   // NRVO; move ctor otherwise (the container is move-only)
}


// ── TLS listening socket ─────────────────────────────────────────────────────
//
// Never throws (Phase 1). Every failure to bring server-side TLS up is reported
// through _result, which RequestServerImpl::open() / BroadcastServerImpl::open()
// already read via _socket->valid(). Throwing from here escaped through the
// public RequestServer::open() shim (no try/catch) and gave callers no way to
// tell what went wrong.
//
// The SSL_CTX built here is shared by every accepted connection. SSL_new() takes
// a reference on it, so close()ing this socket while sessions are still live is
// safe.

namespace
{
    // OpenSSL hands us the buffer to fill with the private key's password.
    // userdata is the NUL-terminated password owned by TlsServerConfiguration.
    int serverKeyPasswordCallback(char* buf, int size, int /*rwflag*/, void* userdata)
    {
        const char* pw = static_cast<const char*>(userdata);
        int len = static_cast<int>(strlen(pw));
        if (len > size) len = size;
        memcpy(buf, pw, static_cast<size_t>(len));
        return len;
    }
}

TLSPassiveSocket::TLSPassiveSocket(bool is_unix_domain, const TlsServerConfiguration& tls_configuration)
    : PassiveSocket(is_unix_domain, tls_configuration)
{
    if (_result.code() != NetworkResultCode::SUCCESS)
        return;   // the base failed to create the socket; it owns that failure

    // Abandon construction with a specific diagnosis. The base constructor has
    // already opened a real listening fd, so it must be released here — open()
    // returns before anyone would call close(), and every rejected open() would
    // otherwise leak a descriptor.
    auto fail = [this](NetworkResultCode code) {
        if (_context) {
            SSL_CTX_free(_context);
            _context = nullptr;
        }
        PassiveSocket::close();
        _result = NetworkResult(code);
    };

    _context = SSL_CTX_new(TLS_server_method());
    if (!_context) {
        // Also the path taken when the library is built without SECURITYSOCKET_TLS:
        // the TlsHelper stub returns nullptr.
        fail(NetworkResultCode::TLS_CONTEXT_INITIALIZATION_FAIL);
        return;
    }

    // [1] Protocol version range. Mirrors TlsClientActiveSocket: if only one of
    //     TLS 1.2 / 1.3 is configured, both bounds collapse onto it.
    {
        const bool has12 = tls_configuration.isVersionSupported(TlsVersion::TLS1_2);
        const bool has13 = tls_configuration.isVersionSupported(TlsVersion::TLS1_3);
        const int min_ver = has12 ? TLS1_2_VERSION : TLS1_3_VERSION;
        const int max_ver = has13 ? TLS1_3_VERSION : TLS1_2_VERSION;
        SSL_CTX_set_min_proto_version(_context, min_ver);
        SSL_CTX_set_max_proto_version(_context, max_ver);
    }

    // [2] Cipher suites. TLS 1.2 and 1.3 use separate OpenSSL knobs.
    {
        char cipher_list[512]{ 0 };
        tls_configuration.generateTLS12CipherSuites(cipher_list);
        if (cipher_list[0] != '\0')
            SSL_CTX_set_cipher_list(_context, cipher_list);

        char ciphersuites[512]{ 0 };
        tls_configuration.generateTLS13CipherSuites(ciphersuites);
        if (ciphersuites[0] != '\0')
            SSL_CTX_set_ciphersuites(_context, ciphersuites);
    }

    // [3] Write-retry semantics. StagingBuffer may reallocate between a failed
    //     SSL_write() and its retry, so the moving-buffer mode is required;
    //     partial writes let the host drain what actually went out.
    SSL_CTX_set_mode(_context,
                     SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER | SSL_MODE_ENABLE_PARTIAL_WRITE);

    // [4] Server certificate and private key.
    {
        const char* key_password = tls_configuration.serverKeyPassword();
        if (key_password[0] != '\0') {
            SSL_CTX_set_default_passwd_cb(_context, serverKeyPasswordCallback);
            SSL_CTX_set_default_passwd_cb_userdata(_context, const_cast<char*>(key_password));
        }

        // _chain_file, not _file: sends intermediate CAs bundled in the PEM.
        // Without them a client holding only the root cannot build a path.
        if (SSL_CTX_use_certificate_chain_file(_context, tls_configuration.serverCertFilePath()) != 1) {
            fail(NetworkResultCode::TLS_SERVER_CERT_LOAD_FAIL);
            return;
        }
        if (SSL_CTX_use_PrivateKey_file(_context, tls_configuration.serverKeyFilePath(), SSL_FILETYPE_PEM) != 1) {
            // Two very different misconfigurations land here, and OpenSSL collapses
            // them into one failure: the key file is unusable (missing, not PEM,
            // wrong password), OR the key is perfectly fine but belongs to a
            // different keypair than the certificate we just loaded —
            // SSL_CTX_use_PrivateKey_file() cross-checks against the loaded cert
            // and rejects the key before SSL_CTX_check_private_key() ever runs.
            //
            // Tell them apart by loading the key alone into a certificate-less
            // context. Doing it this way rather than reading ERR_GET_REASON()
            // avoids depending on reason codes that differ across OpenSSL versions.
            ERR_clear_error();
            SSL_CTX* probe = SSL_CTX_new(TLS_server_method());
            const bool key_is_usable_on_its_own =
                probe && SSL_CTX_use_PrivateKey_file(probe, tls_configuration.serverKeyFilePath(),
                                                     SSL_FILETYPE_PEM) == 1;
            if (probe) SSL_CTX_free(probe);
            ERR_clear_error();

            fail(key_is_usable_on_its_own ? NetworkResultCode::TLS_SERVER_KEY_MISMATCH
                                          : NetworkResultCode::TLS_SERVER_KEY_LOAD_FAIL);
            return;
        }
        // Belt and braces: catches a mismatch that slipped past the check above
        // (e.g. a future OpenSSL that defers the cross-check).
        if (SSL_CTX_check_private_key(_context) != 1) {
            fail(NetworkResultCode::TLS_SERVER_KEY_MISMATCH);
            return;
        }
    }

    // [5] Client authentication (mTLS).
    {
        const TlsClientAuthMode mode = tls_configuration.clientAuthenticationMode();
        if (mode == TlsClientAuthMode::AUTH_MODE_NONE) {
            SSL_CTX_set_verify(_context, SSL_VERIFY_NONE, nullptr);
        }
        else {
            // OPTIONAL asks for a certificate and verifies it if offered;
            // REQUIRED additionally aborts the handshake when none is offered.
            int verify_mode = SSL_VERIFY_PEER;
            if (mode == TlsClientAuthMode::AUTH_MODE_REQUIRED)
                verify_mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
            SSL_CTX_set_verify(_context, verify_mode, nullptr);

            const char* trust_store = tls_configuration.clientTrustStorePath();
            if (trust_store[0] == '\0'
                || SSL_CTX_load_verify_locations(_context, trust_store, nullptr) != 1) {
                fail(NetworkResultCode::TLS_CLIENT_TRUST_STORE_LOAD_FAIL);
                return;
            }
            // Advertise the acceptable CAs in the CertificateRequest. Without this
            // a client cannot tell which of its certificates the server would take.
            SSL_CTX_set_client_CA_list(_context, SSL_load_client_CA_file(trust_store));
        }
    }

    // [6] Handshake diagnostics. The info callback lives on the context (shared),
    //     so the per-connection callback pointer rides along in the context's
    //     ex_data; each accepted SSL copies it onto its own slot.
    if (auto on_tls_event = tls_configuration.getOnTLSEvent()) {
        SSL_CTX_set_ex_data(_context, kTlsEventCallbackExDataIndex,
                            reinterpret_cast<void*>(on_tls_event));
        SSL_CTX_set_info_callback(_context, trackTLSInfo);
    }
}

void TLSPassiveSocket::close()
{
    if (_context) {
        SSL_CTX_free(_context);
        _context = nullptr;
    }
    PassiveSocket::close();
}

NetworkResult TLSPassiveSocket::bind(const SocketAddress& address)
{
    return PassiveSocket::bind(address);
}

NetworkResult TLSPassiveSocket::listen()
{
    return PassiveSocket::listen();
}

ServerActiveSocketContainer TLSPassiveSocket::accept()
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int sock = static_cast<int32_t>(::accept(_socket, (struct sockaddr*)&client_addr, &client_len));

    // `true` (not the hardcoded `false` of the plaintext override) so the container
    // constructs a TlsServerActiveSocket, and _context is threaded through so it can
    // create its SSL session. The handshake is NOT run here — the host drives it
    // from the event loop (see TlsServerActiveSocket::handshake).
    return ServerActiveSocketContainer{ true, sock, (void*)&client_addr, _context };
}