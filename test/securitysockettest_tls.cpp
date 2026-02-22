#define WIN32_LEAN_AND_MEAN

#include <gtest/gtest.h>
#include "securitysockettest.hpp"
#include <SecuritySocket.hpp>

#include <cstring>
#include <string>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#endif

using namespace Bn3Monkey;


// =============================================================================
// Certificate paths (relative to test working directory)
//
// Run the following to generate all required test certificates:
//
//   mkdir certs && cd certs
//   # CA
//   openssl genrsa -out ca.key 2048
//   openssl req -new -x509 -days 3650 -key ca.key -out ca.crt -subj "/CN=TestCA"
//   # Server cert signed by CA, SAN = IP:127.0.0.1
//   openssl genrsa -out server_ca.key 2048
//   openssl req -new -key server_ca.key -out server_ca.csr -subj "/CN=127.0.0.1"
//   echo "subjectAltName=IP:127.0.0.1" > san.ext
//   openssl x509 -req -days 3650 -in server_ca.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out server_ca.crt -extfile san.ext
//   # Self-signed server cert (no CA)
//   openssl req -x509 -newkey rsa:2048 -days 3650 -nodes -keyout server_self.key -out server_self.crt -subj "/CN=127.0.0.1"
//   # Server cert signed by CA but CN = wronghost (hostname mismatch)
//   openssl genrsa -out server_wrong_cn.key 2048
//   openssl req -new -key server_wrong_cn.key -out server_wrong_cn.csr -subj "/CN=wronghost"
//   openssl x509 -req -days 3650 -in server_wrong_cn.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out server_wrong_cn.crt
//   # Client cert signed by CA
//   openssl genrsa -out client.key 2048
//   openssl req -new -key client.key -out client.csr -subj "/CN=TestClient"
//   openssl x509 -req -days 3650 -in client.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out client.crt
//   # Encrypted client private key (password: test1234)
//   openssl rsa -in client.key -aes256 -passout pass:test1234 -out client_enc.key
//   # Untrusted CA and a client cert signed by it
//   openssl genrsa -out untrusted_ca.key 2048
//   openssl req -new -x509 -days 3650 -key untrusted_ca.key -out untrusted_ca.crt -subj "/CN=UntrustedCA"
//   openssl genrsa -out client_untrusted.key 2048
//   openssl req -new -key client_untrusted.key -out client_untrusted.csr -subj "/CN=UntrustedClient"
//   openssl x509 -req -days 3650 -in client_untrusted.csr -CA untrusted_ca.crt -CAkey untrusted_ca.key -CAcreateserial -out client_untrusted.crt
// =============================================================================

static const char* SERVER_SELF_CERT      = "certs/server_self.crt";
static const char* SERVER_SELF_KEY       = "certs/server_self.key";
static const char* CA_CERT               = "certs/ca.crt";
static const char* SERVER_CA_CERT        = "certs/server_ca.crt";
static const char* SERVER_CA_KEY         = "certs/server_ca.key";
static const char* SERVER_WRONG_CN_CERT  = "certs/server_wrong_cn.crt";
static const char* SERVER_WRONG_CN_KEY   = "certs/server_wrong_cn.key";
static const char* CLIENT_CERT_PATH      = "certs/client.crt";
static const char* CLIENT_KEY_PATH       = "certs/client.key";
static const char* CLIENT_ENC_KEY_PATH   = "certs/client_enc.key";
static const char* CLIENT_KEY_PASSWORD   = "test1234";
static const char* UNTRUSTED_CA_CERT     = "certs/untrusted_ca.crt";
static const char* CLIENT_UNTRUSTED_CERT = "certs/client_untrusted.crt";
static const char* CLIENT_UNTRUSTED_KEY  = "certs/client_untrusted.key";



// =============================================================================
// TLSServerProcess: RAII wrapper that launches 'openssl s_server'
// =============================================================================
class TLSServerProcess
{
public:
    // port       : TCP port to listen on
    // extra_args : flags appended after "openssl s_server -accept <port>"
    explicit TLSServerProcess(unsigned short port, const std::string& extra_args)
    {
        char cmd[256]{ 0 };
        snprintf(cmd, sizeof(cmd)-1, "openssl s_server -accept %d %s -quiet",
                  port, extra_args.c_str());

#ifdef _WIN32

        STARTUPINFOA si{};
        si.cb = sizeof(si);

        PROCESS_INFORMATION pi{};
        if (CreateProcessA(
            NULL,
            cmd,
            NULL,
            NULL,
            FALSE,              // handle 상속 불필요
            0,                  // CREATE_NO_WINDOW 제거
            NULL,
            NULL,
            &si,
            &pi))
        {
            _hProcess = pi.hProcess;
            CloseHandle(pi.hThread);
            _started = true;
        }
#else
        _pid = fork();
        if (_pid == 0)
        {
            freopen("/dev/null", "r", stdin);
            freopen("/dev/null", "w", stdout);
            freopen("/dev/null", "w", stderr);
            execl("/bin/sh", "sh", "-c", cmd, nullptr);
            _exit(1);
        }
        _started = (_pid > 0);
#endif
        if (_started)
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
    }

    ~TLSServerProcess()
    {
#ifdef _WIN32
        if (_started && _hProcess != NULL)
        {
            TerminateProcess(_hProcess, 0);
            WaitForSingleObject(_hProcess, 2000);
            CloseHandle(_hProcess);
        }
#else
        if (_started && _pid > 0)
        {
            kill(_pid, SIGTERM);
            waitpid(_pid, nullptr, 0);
        }
#endif
    }

    bool started() const { return _started; }

private:
    bool _started{ false };
#ifdef _WIN32
    HANDLE      _hProcess{ NULL };
#else
    pid_t _pid{ -1 };
#endif
};


// =============================================================================
// Helper: create a TLS SocketClient and attempt connect; returns SocketCode.
// max_retries=1 prevents reusing a broken SSL object on failure.
// =============================================================================
static SocketCode tryTLSConnect(unsigned short port,
                                const SocketTLSClientConfiguration& tls_cfg)
{
    SocketConfiguration config{
        "127.0.0.1",
        port,
        false,  // not unix domain
        1,      // max_retries = 1
        3000,   // read_timeout_ms
        3000,   // write_timeout_ms
        200,    // time_between_retries_ms
        4096    // pdu_size
    };

    SocketClient client{ config, tls_cfg };

    {
        SocketResult r = client.open();
        if (r.code() != SocketCode::SUCCESS)
        {
            printf("Open Fail : %s\n", r.message());
            return r.code();
        }
    }

    SocketResult r = client.connect();
    client.close();

    printf("Connect Code : %s\n", r.message());
    return r.code();
}


// =============================================================================
// GROUP 1: TLS Version Tests  (ports 14430–14435)
// =============================================================================

TEST(TLSConnection, ClientSupports_TLS12Only_ServerSupports_TLS12Only_ConnectionSucceeds)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14430,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{ {SocketTLSVersion::TLS1_2} };
    auto ret = tryTLSConnect(14430, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientSupports_TLS13Only_ServerSupports_TLS13Only_ConnectionSucceeds)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14431,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{ {SocketTLSVersion::TLS1_3} };
    auto ret = tryTLSConnect(14431, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientSupports_TLS12And13_ServerSupports_TLS12Only_ConnectionSucceeds)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14432,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{ {SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3} };
    auto ret = tryTLSConnect(14432, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientSupports_TLS12And13_ServerSupports_TLS13Only_ConnectionSucceeds)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14433,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{ {SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3} };
    auto ret = tryTLSConnect(14433, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientSupports_TLS12Only_ServerSupports_TLS13Only_ConnectionFails_TLSVersionNotSupported)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14434,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{ {SocketTLSVersion::TLS1_2} };
    auto ret = tryTLSConnect(14434, tls);
    EXPECT_EQ(SocketCode::TLS_VERSION_NOT_SUPPORTED, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientSupports_TLS13Only_ServerSupports_TLS12Only_ConnectionFails_TLSVersionNotSupported)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14435,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{ {SocketTLSVersion::TLS1_3} };
    auto ret = tryTLSConnect(14435, tls);
    EXPECT_EQ(SocketCode::TLS_VERSION_NOT_SUPPORTED, ret);
    Bn3Monkey::releaseSecuritySocket();
}


// =============================================================================
// GROUP 2: Cipher Suite Tests  (ports 14436–14439)
// =============================================================================

TEST(TLSConnection, ClientSpecifiesTLS12Cipher_ServerSupportsMatchingCipher_ConnectionSucceeds)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14436,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_3 -cipher ECDHE-RSA-AES256-GCM-SHA384" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {SocketTLS1_2CipherSuite::ECDHE_RSA_AES256_GCM_SHA384}
    };
	auto ret = tryTLSConnect(14436, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientSpecifiesTLS13Cipher_ServerSupportsMatchingCipher_ConnectionSucceeds)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14437,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_2 -ciphersuites TLS_AES_256_GCM_SHA384" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {},
        {SocketTLS1_3CipherSuite::TLS_AES256_GCM_SHA384}
    };
    auto ret = tryTLSConnect(14437, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientSpecifiesTLS12Cipher_ServerDoesNotSupportCipher_ConnectionFails_CipherSuiteMismatch)
{
    Bn3Monkey::initializeSecuritySocket();
    // Server: only ECDHE-RSA-AES128-GCM-SHA256  |  Client: only ECDHE-RSA-AES256-GCM-SHA384
    TLSServerProcess server{ 14438,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_3 -cipher ECDHE-RSA-AES128-GCM-SHA256" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {SocketTLS1_2CipherSuite::ECDHE_RSA_AES256_GCM_SHA384}
    };
	auto ret = tryTLSConnect(14438, tls);
    EXPECT_EQ(SocketCode::TLS_CIPHER_SUITE_MISMATCH, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientSpecifiesTLS13Cipher_ServerDoesNotSupportCipher_ConnectionFails_CipherSuiteMismatch)
{
    Bn3Monkey::initializeSecuritySocket();
    // Server: only TLS_AES_256_GCM_SHA384  |  Client: only TLS_AES_128_GCM_SHA256
    TLSServerProcess server{ 14439,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_2 -ciphersuites TLS_AES_256_GCM_SHA384" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {},
        {SocketTLS1_3CipherSuite::TLS_AES128_GCM_SHA256}
    };
    auto ret = tryTLSConnect(14439, tls);
    EXPECT_EQ(SocketCode::TLS_CIPHER_SUITE_MISMATCH, ret);
    Bn3Monkey::releaseSecuritySocket();
}


// =============================================================================
// GROUP 3: Server Certificate Verification Tests  (ports 14440–14443)
// =============================================================================

TEST(TLSConnection, ClientDoesNotVerifyServerCert_ServerHasSelfSignedCert_ConnectionSucceeds)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14440,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3},
        {}, {},
        false   // verify_server = false
    };
    auto ret = tryTLSConnect(14440, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesServerCert_ServerCertSignedByTrustedCA_ConnectionSucceeds)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14441,
        std::string("-cert ") + SERVER_CA_CERT + " -key " + SERVER_CA_KEY };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3},
        {}, {},
        true,    // verify_server
        false,   // verify_hostname
        CA_CERT  // trusted CA
    };
    auto ret = tryTLSConnect(14441, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesServerCert_ServerHasSelfSignedCert_ConnectionFails_ServerCertInvalid)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14442,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3},
        {}, {},
        true,    // verify_server
        false,   // verify_hostname
        CA_CERT  // self-signed cert is NOT in this CA chain
    };
    auto ret = tryTLSConnect(14442, tls);
    EXPECT_EQ(SocketCode::TLS_SERVER_CERT_INVALID, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesServerCert_ServerCertSignedByUntrustedCA_ConnectionFails_ServerCertInvalid)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14443,
        std::string("-cert ") + SERVER_CA_CERT + " -key " + SERVER_CA_KEY };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3},
        {}, {},
        true,              // verify_server
        false,             // verify_hostname
        UNTRUSTED_CA_CERT  // wrong CA — chain verification fails
    };
    auto ret = tryTLSConnect(14443, tls);
    EXPECT_EQ(SocketCode::TLS_SERVER_CERT_INVALID, ret);
    Bn3Monkey::releaseSecuritySocket();
}


// =============================================================================
// GROUP 4: Hostname Verification Tests  (ports 14444–14446)
// =============================================================================

TEST(TLSConnection, ClientVerifiesHostname_ServerCertMatchesHostname_ConnectionSucceeds)
{
    Bn3Monkey::initializeSecuritySocket();
    // server_ca.crt has CN=127.0.0.1 / SAN=IP:127.0.0.1
    TLSServerProcess server{ 14444,
        std::string("-cert ") + SERVER_CA_CERT + " -key " + SERVER_CA_KEY };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3},
        {}, {},
        true,   // verify_server
        true,   // verify_hostname (connects to 127.0.0.1, cert matches)
        CA_CERT
    };
    auto ret = tryTLSConnect(14444, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesHostname_ServerCertDoesNotMatchHostname_ConnectionFails_HostnameMismatch)
{
    Bn3Monkey::initializeSecuritySocket();
    // server_wrong_cn.crt has CN=wronghost; client connects to 127.0.0.1
    TLSServerProcess server{ 14445,
        std::string("-cert ") + SERVER_WRONG_CN_CERT + " -key " + SERVER_WRONG_CN_KEY };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3},
        {}, {},
        true,   // verify_server
        true,   // verify_hostname
        CA_CERT
    };
    auto ret = tryTLSConnect(14445, tls);
    EXPECT_EQ(SocketCode::TLS_HOSTNAME_MISMATCH, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientDoesNotVerifyHostname_ServerCertDoesNotMatchHostname_ConnectionSucceeds)
{
    Bn3Monkey::initializeSecuritySocket();
    // CN=wronghost but verify_hostname is false — chain check still passes
    TLSServerProcess server{ 14446,
        std::string("-cert ") + SERVER_WRONG_CN_CERT + " -key " + SERVER_WRONG_CN_KEY };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3},
        {}, {},
        true,   // verify_server (cert chain is valid — signed by CA)
        false,  // verify_hostname = false
        CA_CERT
    };
    auto ret = tryTLSConnect(14446, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}


// =============================================================================
// GROUP 5: Client Certificate (mTLS) Tests  (ports 14447–14452)
// =============================================================================

TEST(TLSConnection, ServerRequiresClientCert_ClientProvidesValidCert_ConnectionSucceeds)
{
    Bn3Monkey::initializeSecuritySocket();
    // -Verify (capital V) = require client certificate
    TLSServerProcess server{ 14447,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        true,              // use_client_certificate
        CLIENT_CERT_PATH,
        CLIENT_KEY_PATH
    };
    auto ret = tryTLSConnect(14447, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequiresClientCert_ClientDoesNotProvideAnyClientCert_ConnectionFails_ClientCertRejected)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14448,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        false  // no client certificate
    };
    auto ret = tryTLSConnect(14448, tls);
    EXPECT_EQ(SocketCode::TLS_CLIENT_CERT_REJECTED, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequiresClientCert_ClientProvidesCertSignedByUntrustedCA_ConnectionFails_ClientCertRejected)
{
    Bn3Monkey::initializeSecuritySocket();
    // Server trusts only CA_CERT; client presents a cert from a different CA
    TLSServerProcess server{ 14449,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        true,
        CLIENT_UNTRUSTED_CERT,
        CLIENT_UNTRUSTED_KEY
    };
    auto ret = tryTLSConnect(14449, tls);
    EXPECT_EQ(SocketCode::TLS_CLIENT_CERT_REJECTED, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequestsClientCert_ClientProvidesValidCert_ConnectionSucceeds)
{
    Bn3Monkey::initializeSecuritySocket();
    // -verify (lowercase v) = request client certificate (optional)
    TLSServerProcess server{ 14450,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -verify 1 -CAfile " + CA_CERT };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        true,
        CLIENT_CERT_PATH,
        CLIENT_KEY_PATH
    };

    auto ret = tryTLSConnect(14450, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequestsClientCert_ClientDoesNotProvideClientCert_ConnectionSucceeds)
{
    Bn3Monkey::initializeSecuritySocket();
    // Optional client cert: server allows handshake without a client cert
    TLSServerProcess server{ 14451,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -verify 1 -CAfile " + CA_CERT };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        false  // no client certificate
    };


    auto ret = tryTLSConnect(14451, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequestsClientCert_ClientProvidesCertSignedByUntrustedCA_ConnectionFails_ClientCertRejected)
{
    Bn3Monkey::initializeSecuritySocket();
    // Even with optional verification, presenting a cert from an unknown CA causes failure
    TLSServerProcess server{ 14452,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -verify 1 -CAfile " + CA_CERT };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        true,
        CLIENT_UNTRUSTED_CERT,
        CLIENT_UNTRUSTED_KEY
    };
    auto ret = tryTLSConnect(14452, tls);
    EXPECT_EQ(SocketCode::TLS_CLIENT_CERT_REJECTED, ret);
    Bn3Monkey::releaseSecuritySocket();
}


// =============================================================================
// GROUP 6: Encrypted Private Key Tests  (ports 14453–14454)
// =============================================================================

TEST(TLSConnection, ClientProvidesEncryptedPrivateKey_CorrectPassword_ConnectionSucceeds)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14453,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        true,
        CLIENT_CERT_PATH,
        CLIENT_ENC_KEY_PATH,
        CLIENT_KEY_PASSWORD  // correct password for the encrypted key
    };

    auto ret = tryTLSConnect(14453, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientProvidesEncryptedPrivateKey_WrongPassword_ConnectionFails)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14454,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        true,
        CLIENT_CERT_PATH,
        CLIENT_ENC_KEY_PATH,
        "wrongpassword"  // incorrect password → key load fails → handshake fails
    };
    SocketCode code = tryTLSConnect(14454, tls);
    EXPECT_NE(SocketCode::SUCCESS, code);
    Bn3Monkey::releaseSecuritySocket();
}
