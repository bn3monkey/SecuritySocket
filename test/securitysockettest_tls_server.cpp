#define WIN32_LEAN_AND_MEAN

#include <SecuritySocket.hpp>

#include <gtest/gtest.h>
#include "securitysockettest.hpp"
#include "securitysockettest_tls_server_certs.hpp"

#include <cstdio>
#include <fstream>
#include <string>

using namespace Bn3Monkey;
using namespace Bn3MonkeyTest;

// =============================================================================
// Server-side TLS: SSL_CTX construction (Phase 2)
//
// Two contracts are pinned here.
//
// 1. open() NEVER throws. It used to `throw std::runtime_error("Not
//    Implemented")` from TLSPassiveSocket's constructor, which escaped through
//    the public RequestServer::open() shim (no try/catch on that path) and left
//    callers with an unhandled exception instead of a diagnosis.
//
// 2. open() says WHY it failed. Every way of misconfiguring the server
//    certificate maps to a distinct NetworkResultCode, so a misconfigured
//    deployment is debuggable from the return value alone.
//
// The accept path is not wired yet (Phase 3), so a *successful* open() here
// means "the listening socket and SSL_CTX are healthy", not "clients can
// connect". TLSPassiveSocket::accept() currently accepts and immediately closes.
// =============================================================================

namespace
{
    // Writes a PEM literal to a scratch file and cleans it up on scope exit, so
    // the tests need neither openssl nor the remote command harness.
    class TempPem
    {
    public:
        TempPem(const char* name, const char* pem)
        {
            _path = std::string(::testing::TempDir()) + name;
            std::ofstream out(_path, std::ios::binary | std::ios::trunc);
            out << pem;
        }
        ~TempPem() { std::remove(_path.c_str()); }

        const char* path() const { return _path.c_str(); }

    private:
        std::string _path;
    };

    class NoopCustomHandler : public CustomProtocolRequestHandler
    {
    public:
        size_t headerSize() override { return 4; }
        size_t payloadSize(const void*) override { return 0; }
        RequestProcessingMode classifyMode(const void*) override
        {
            return RequestProcessingMode::FAST;
        }
        void process(const ClientConnection&,
                     const CustomProtocolRequest&,
                     CustomProtocolResponse&) override {}
    };

    TlsServerConfiguration serverTlsConfig(
        const char* cert, const char* key,
        TlsClientAuthMode auth = TlsClientAuthMode::AUTH_MODE_NONE,
        const char* client_trust_store = nullptr,
        const char* key_password = nullptr)
    {
        return TlsServerConfiguration{
            { TlsVersion::TLS1_2, TlsVersion::TLS1_3 },
            {}, {},
            cert, key, key_password,
            auth, client_trust_store
        };
    }

    // Bring a TLS RequestServer up once and report open()'s verdict. Each test
    // uses its own port so a lingering listener never bleeds across tests.
    NetworkResultCode openTlsServer(uint32_t port, const TlsServerConfiguration& tls)
    {
        NetworkConfiguration config{ "127.0.0.1", port };
        NoopCustomHandler handler;

        RequestServer server{ config, tls };
        NetworkResult result = server.open(&handler, 4);   // NetworkResult::code() is non-const
        server.close();
        return result.code();
    }

    // The library only compiles OpenSSL in when built with SECURITYSOCKET_TLS.
    // That define is PRIVATE to the securitysocket target, so this test target
    // cannot #if on it — probe the behaviour instead. Without TLS, SSL_CTX_new
    // is a stub returning nullptr and everything collapses to one code.
    bool tlsCompiledOut(NetworkResultCode code)
    {
        return code == NetworkResultCode::TLS_CONTEXT_INITIALIZATION_FAIL;
    }
}

// The regression guard for the unhandled std::runtime_error that surfaced
// downstream as a hung / aborted process rather than an error return.
TEST(TlsServer, OpenNeverThrows)
{
    TempPem cert("ss_ok.crt", kServerCertPem);
    TempPem key("ss_ok.key", kServerKeyPem);

    NetworkConfiguration config{ "127.0.0.1", 29441 };
    NoopCustomHandler handler;
    RequestServer server{ config, serverTlsConfig(cert.path(), key.path()) };

    EXPECT_NO_THROW({
        NetworkResult result = server.open(&handler, 4);
        (void)result;
        server.close();
    });
}

// A well-formed certificate/key pair brings the listening socket and SSL_CTX up.
TEST(TlsServer, ValidCertificateAndKeyOpens)
{
    TempPem cert("ss_valid.crt", kServerCertPem);
    TempPem key("ss_valid.key", kServerKeyPem);

    const auto code = openTlsServer(29442, serverTlsConfig(cert.path(), key.path()));
    if (tlsCompiledOut(code)) GTEST_SKIP() << "library built without SECURITYSOCKET_TLS";

    EXPECT_EQ(code, NetworkResultCode::SUCCESS);
}

TEST(TlsServer, MissingCertificateFileReportsCertLoadFail)
{
    TempPem key("ss_nocert.key", kServerKeyPem);

    const auto code = openTlsServer(
        29443, serverTlsConfig("./definitely_not_here.crt", key.path()));
    if (tlsCompiledOut(code)) GTEST_SKIP() << "library built without SECURITYSOCKET_TLS";

    EXPECT_EQ(code, NetworkResultCode::TLS_SERVER_CERT_LOAD_FAIL);
}

TEST(TlsServer, MissingKeyFileReportsKeyLoadFail)
{
    TempPem cert("ss_nokey.crt", kServerCertPem);

    const auto code = openTlsServer(
        29444, serverTlsConfig(cert.path(), "./definitely_not_here.key"));
    if (tlsCompiledOut(code)) GTEST_SKIP() << "library built without SECURITYSOCKET_TLS";

    EXPECT_EQ(code, NetworkResultCode::TLS_SERVER_KEY_LOAD_FAIL);
}

// Both files load fine on their own; they simply belong to different keypairs.
// Without SSL_CTX_check_private_key() this would only surface much later, as an
// opaque handshake failure against a real client.
TEST(TlsServer, CertificateAndKeyFromDifferentPairsReportsMismatch)
{
    TempPem cert("ss_mismatch.crt", kServerCertPem);
    TempPem key("ss_mismatch.key", kOtherKeyPem);

    const auto code = openTlsServer(29445, serverTlsConfig(cert.path(), key.path()));
    if (tlsCompiledOut(code)) GTEST_SKIP() << "library built without SECURITYSOCKET_TLS";

    EXPECT_EQ(code, NetworkResultCode::TLS_SERVER_KEY_MISMATCH);
}

// mTLS with no CA to verify clients against is unsatisfiable — say so at open()
// rather than rejecting every client later.
TEST(TlsServer, ClientAuthWithoutTrustStoreReportsTrustStoreFail)
{
    TempPem cert("ss_mtls.crt", kServerCertPem);
    TempPem key("ss_mtls.key", kServerKeyPem);

    const auto code = openTlsServer(
        29446,
        serverTlsConfig(cert.path(), key.path(),
                        TlsClientAuthMode::AUTH_MODE_REQUIRED, nullptr));
    if (tlsCompiledOut(code)) GTEST_SKIP() << "library built without SECURITYSOCKET_TLS";

    EXPECT_EQ(code, NetworkResultCode::TLS_CLIENT_TRUST_STORE_LOAD_FAIL);
}

TEST(TlsServer, ClientAuthWithTrustStoreOpens)
{
    TempPem cert("ss_mtls_ok.crt", kServerCertPem);
    TempPem key("ss_mtls_ok.key", kServerKeyPem);
    TempPem ca("ss_mtls_ok_ca.crt", kCaCertPem);

    const auto code = openTlsServer(
        29447,
        serverTlsConfig(cert.path(), key.path(),
                        TlsClientAuthMode::AUTH_MODE_REQUIRED, ca.path()));
    if (tlsCompiledOut(code)) GTEST_SKIP() << "library built without SECURITYSOCKET_TLS";

    EXPECT_EQ(code, NetworkResultCode::SUCCESS);
}

// Phase 0 rewrote the socket container both paths share; pin that plain still opens.
TEST(TlsServer, PlainServerStillOpens)
{
    NetworkConfiguration config{ "127.0.0.1", 29448 };
    NoopCustomHandler handler;

    RequestServer server{ config };
    NetworkResult result = server.open(&handler, 4);
    EXPECT_EQ(result.code(), NetworkResultCode::SUCCESS);
    server.close();
}

// BroadcastServer is plaintext by design — it has no TlsServerConfiguration
// overload at all (see the note on its declaration). Nothing to test here beyond
// the plain path, which securitysockettest_tcp_broadcast.cpp already covers.
