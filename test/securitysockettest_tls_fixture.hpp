#if !defined(__SECURITY_SOCKET_TEST_TLS_FIXTURE__)
#define __SECURITY_SOCKET_TEST_TLS_FIXTURE__

// Shared plumbing for the *server-side* TLS tests (tls_request_echo,
// tls_request_file, https_server).
//
// The pre-existing TLS suite (securitysockettest_tls.cpp) drives our Client
// against an `openssl s_server` spawned through a REMOTE command server, so it
// cannot run on a plain developer checkout. These suites are the mirror image:
// a real SecuritySocket RequestServer with TLS on one side, our own TLS Client
// on the other, entirely in-process. The certificates come from the embedded
// PEM literals so nothing has to be generated or fetched at run time.

#include <SecuritySocket.hpp>

#include "securitysockettest_tls_server_certs.hpp"
#include "securitysockettest_helper.hpp"   // printConcurrent

#include <cstdio>
#include <fstream>
#include <string>

namespace Bn3MonkeyTest
{
    // Writes a PEM literal to a scratch file and removes it on scope exit.
    class TempPemFile
    {
    public:
        TempPemFile(const char* name, const char* pem)
        {
            _path = std::string(::testing::TempDir()) + name;
            std::ofstream out(_path, std::ios::binary | std::ios::trunc);
            out << pem;
        }
        ~TempPemFile() { std::remove(_path.c_str()); }

        TempPemFile(const TempPemFile&)            = delete;
        TempPemFile& operator=(const TempPemFile&) = delete;

        const char* path() const { return _path.c_str(); }

    private:
        std::string _path;
    };

    // The server's certificate + key, on disk for the duration of one test.
    // CN=127.0.0.1 with an IP SAN, so hostname verification against 127.0.0.1
    // succeeds if a test opts into it.
    struct ServerCertFiles
    {
        TempPemFile cert{ "ss_srv.crt", kServerCertPem };
        TempPemFile key { "ss_srv.key", kServerKeyPem  };
    };

    // OpenSSL handshake-progress callbacks. Registered via setOnTLSEvent() on the
    // configuration; OpenSSL fires them at every state-machine step, alert, and
    // handshake start/finish, so the test output shows the negotiation happening
    // rather than just a silent pass. Free functions because the callback type is
    // a plain void(*)(const char*).
    inline void onServerTlsEvent(const char* message)
    {
        printConcurrent("    [server-tls] %s\n", message);
    }
    inline void onClientTlsEvent(const char* message)
    {
        printConcurrent("    [client-tls] %s\n", message);
    }

    inline Bn3Monkey::TlsServerConfiguration makeServerTls(const ServerCertFiles& files)
    {
        Bn3Monkey::TlsServerConfiguration cfg{
            { Bn3Monkey::TlsVersion::TLS1_2, Bn3Monkey::TlsVersion::TLS1_3 },
            {},                                   // default TLS 1.2 cipher suites
            {},                                   // default TLS 1.3 cipher suites
            files.cert.path(),
            files.key.path(),
            nullptr,                              // key is not password-protected
            Bn3Monkey::TlsClientAuthMode::AUTH_MODE_NONE,
            nullptr                               // no client trust store
        };
        cfg.setOnTLSEvent(onServerTlsEvent);
        return cfg;
    }

    // The certificate is self-signed, so the client does not verify it. What is
    // under test here is the server's TLS, not the client's trust decisions —
    // those already have coverage in securitysockettest_tls.cpp.
    inline Bn3Monkey::TlsClientConfiguration makeClientTls()
    {
        Bn3Monkey::TlsClientConfiguration cfg{
            { Bn3Monkey::TlsVersion::TLS1_2, Bn3Monkey::TlsVersion::TLS1_3 },
            {}, {},
            false,      // verify_server
            false,      // verify_hostname
            nullptr,    // server_trust_store_path
            false,      // use_client_certificate
            nullptr, nullptr, nullptr
        };
        cfg.setOnTLSEvent(onClientTlsEvent);
        return cfg;
    }
}

#endif // __SECURITY_SOCKET_TEST_TLS_FIXTURE__
