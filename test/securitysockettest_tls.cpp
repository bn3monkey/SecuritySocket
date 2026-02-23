#define WIN32_LEAN_AND_MEAN

#include <SecuritySocket.hpp>

#include <gtest/gtest.h>
#include "securitysockettest.hpp"
#include "securitysockettest_tlshelper.hpp"

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
        snprintf(cmd, sizeof(cmd)-1, "openssl s_server -accept %d -msg -state %s -quiet",
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

void trackTLSEvent(const char* message) {
    printf("|Client| %s\n", message);
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
    tls.setOnTLSEvent(trackTLSEvent);
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
    tls.setOnTLSEvent(trackTLSEvent);
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
    tls.setOnTLSEvent(trackTLSEvent);
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
    tls.setOnTLSEvent(trackTLSEvent);
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
    tls.setOnTLSEvent(trackTLSEvent);
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
    tls.setOnTLSEvent(trackTLSEvent);
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
        {SocketTLS1_3CipherSuite::TLS_AES_256_GCM_SHA384}
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
    tls.setOnTLSEvent(trackTLSEvent);
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
        {SocketTLS1_3CipherSuite::TLS_AES_128_GCM_SHA256}
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14439, tls);
    EXPECT_EQ(SocketCode::TLS_CIPHER_SUITE_MISMATCH, ret);
    Bn3Monkey::releaseSecuritySocket();
}


// =============================================================================
// GROUP 3: Server Certificate Verification Tests
//   TLS 1.2 server  (ports 14440–14443)
//   TLS 1.3 server  (ports 14444–14447)
// =============================================================================

TEST(TLSConnection, ClientDoesNotVerifyServerCert_ServerHasSelfSignedCert_ConnectionSucceeds_TLS12)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14440,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        false   // verify_server = false
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14440, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesServerCert_ServerCertSignedByTrustedCA_ConnectionSucceeds_TLS12)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14441,
        std::string("-cert ") + SERVER_CA_CERT + " -key " + SERVER_CA_KEY
        + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        true,    // verify_server
        false,   // verify_hostname
        CA_CERT
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14441, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesServerCert_ServerHasSelfSignedCert_ConnectionFails_ServerCertInvalid_TLS12)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14442,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        true,    // verify_server
        false,   // verify_hostname
        CA_CERT  // self-signed cert is NOT in this CA chain
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14442, tls);
    EXPECT_EQ(SocketCode::TLS_SERVER_CERT_INVALID, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesServerCert_ServerCertSignedByUntrustedCA_ConnectionFails_ServerCertInvalid_TLS12)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14443,
        std::string("-cert ") + SERVER_CA_CERT + " -key " + SERVER_CA_KEY
        + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        true,              // verify_server
        false,             // verify_hostname
        UNTRUSTED_CA_CERT  // wrong CA — chain verification fails
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14443, tls);
    EXPECT_EQ(SocketCode::TLS_SERVER_CERT_INVALID, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientDoesNotVerifyServerCert_ServerHasSelfSignedCert_ConnectionSucceeds_TLS13)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14444,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        false   // verify_server = false
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14444, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesServerCert_ServerCertSignedByTrustedCA_ConnectionSucceeds_TLS13)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14445,
        std::string("-cert ") + SERVER_CA_CERT + " -key " + SERVER_CA_KEY
        + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        true,    // verify_server
        false,   // verify_hostname
        CA_CERT
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14445, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesServerCert_ServerHasSelfSignedCert_ConnectionFails_ServerCertInvalid_TLS13)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14446,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        true,    // verify_server
        false,   // verify_hostname
        CA_CERT
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14446, tls);
    EXPECT_EQ(SocketCode::TLS_SERVER_CERT_INVALID, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesServerCert_ServerCertSignedByUntrustedCA_ConnectionFails_ServerCertInvalid_TLS13)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14447,
        std::string("-cert ") + SERVER_CA_CERT + " -key " + SERVER_CA_KEY
        + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        true,              // verify_server
        false,             // verify_hostname
        UNTRUSTED_CA_CERT
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14447, tls);
    EXPECT_EQ(SocketCode::TLS_SERVER_CERT_INVALID, ret);
    Bn3Monkey::releaseSecuritySocket();
}


// =============================================================================
// GROUP 4: Hostname Verification Tests
//   TLS 1.2 server  (ports 14448–14450)
//   TLS 1.3 server  (ports 14451–14453)
// =============================================================================

TEST(TLSConnection, ClientVerifiesHostname_ServerCertMatchesHostname_ConnectionSucceeds_TLS12)
{
    Bn3Monkey::initializeSecuritySocket();
    // server_ca.crt has CN=127.0.0.1 / SAN=IP:127.0.0.1
    TLSServerProcess server{ 14448,
        std::string("-cert ") + SERVER_CA_CERT + " -key " + SERVER_CA_KEY
        + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        true,   // verify_server
        true,   // verify_hostname (connects to 127.0.0.1, cert matches)
        CA_CERT
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14448, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesHostname_ServerCertDoesNotMatchHostname_ConnectionFails_HostnameMismatch_TLS12)
{
    Bn3Monkey::initializeSecuritySocket();
    // server_wrong_cn.crt has CN=wronghost; client connects to 127.0.0.1
    TLSServerProcess server{ 14449,
        std::string("-cert ") + SERVER_WRONG_CN_CERT + " -key " + SERVER_WRONG_CN_KEY
        + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        true,   // verify_server
        true,   // verify_hostname
        CA_CERT
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14449, tls);
    EXPECT_EQ(SocketCode::TLS_HOSTNAME_MISMATCH, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientDoesNotVerifyHostname_ServerCertDoesNotMatchHostname_ConnectionSucceeds_TLS12)
{
    Bn3Monkey::initializeSecuritySocket();
    // CN=wronghost but verify_hostname is false — chain check still passes
    TLSServerProcess server{ 14450,
        std::string("-cert ") + SERVER_WRONG_CN_CERT + " -key " + SERVER_WRONG_CN_KEY
        + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        true,   // verify_server (cert chain is valid — signed by CA)
        false,  // verify_hostname = false
        CA_CERT
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14450, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesHostname_ServerCertMatchesHostname_ConnectionSucceeds_TLS13)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14451,
        std::string("-cert ") + SERVER_CA_CERT + " -key " + SERVER_CA_KEY
        + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        true,   // verify_server
        true,   // verify_hostname
        CA_CERT
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14451, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesHostname_ServerCertDoesNotMatchHostname_ConnectionFails_HostnameMismatch_TLS13)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14452,
        std::string("-cert ") + SERVER_WRONG_CN_CERT + " -key " + SERVER_WRONG_CN_KEY
        + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        true,   // verify_server
        true,   // verify_hostname
        CA_CERT
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14452, tls);
    EXPECT_EQ(SocketCode::TLS_HOSTNAME_MISMATCH, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientDoesNotVerifyHostname_ServerCertDoesNotMatchHostname_ConnectionSucceeds_TLS13)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14453,
        std::string("-cert ") + SERVER_WRONG_CN_CERT + " -key " + SERVER_WRONG_CN_KEY
        + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        true,   // verify_server
        false,  // verify_hostname = false
        CA_CERT
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14453, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}


// =============================================================================
// GROUP 5: Client Certificate (mTLS) Tests
//   TLS 1.2 server  (ports 14454–14459)
//   TLS 1.3 server  (ports 14460–14465)
//
// Note: In TLS 1.2 the server's handshake_failure alert is received
// synchronously inside SSL_connect(), so no post-handshake probe is needed.
// In TLS 1.3 the server processes the client certificate after sending its
// own Finished, so the rejection alert arrives after SSL_connect() returns
// success — caught by the post-handshake probe in TLSClientActiveSocket::reconnect().
// =============================================================================

TEST(TLSConnection, ServerRequiresClientCert_ClientProvidesValidCert_ConnectionSucceeds_TLS12)
{
    Bn3Monkey::initializeSecuritySocket();
    // -Verify (capital V) = require client certificate
    TLSServerProcess server{ 14454,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        false, false, nullptr,
        true,
        CLIENT_CERT_PATH,
        CLIENT_KEY_PATH
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14454, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequiresClientCert_ClientDoesNotProvideAnyClientCert_ConnectionFails_ClientCertRejected_TLS12)
{
    Bn3Monkey::initializeSecuritySocket();
    // TLS 1.2: server sends handshake_failure synchronously inside SSL_connect().
    // SSL_R_SSLV3_ALERT_HANDSHAKE_FAILURE + no cert configured on client side.
    TLSServerProcess server{ 14455,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        false, false, nullptr,
        false  // no client certificate
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14455, tls);
    EXPECT_EQ(SocketCode::TLS_CLIENT_CERT_REJECTED, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequiresClientCert_ClientProvidesCertSignedByUntrustedCA_ConnectionFails_ClientCertRejected_TLS12)
{
    Bn3Monkey::initializeSecuritySocket();
    // Server trusts only CA_CERT; client presents a cert from a different CA.
    // -verify_return_error makes the server's verify callback actually reject the
    // connection (return 0) on failure, instead of the default behaviour of
    // printing the error and continuing.
    // TLS 1.2: SSL_R_TLSV1_ALERT_UNKNOWN_CA received synchronously inside SSL_connect().
    TLSServerProcess server{ 14456,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT + " -verify_return_error -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        false, false, nullptr,
        true,
        CLIENT_UNTRUSTED_CERT,
        CLIENT_UNTRUSTED_KEY
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14456, tls);
    EXPECT_EQ(SocketCode::TLS_CLIENT_CERT_REJECTED, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequestsClientCert_ClientProvidesValidCert_ConnectionSucceeds_TLS12)
{
    Bn3Monkey::initializeSecuritySocket();
    // -verify (lowercase v) = request client certificate (optional)
    TLSServerProcess server{ 14457,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -verify 1 -CAfile " + CA_CERT + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        false, false, nullptr,
        true,
        CLIENT_CERT_PATH,
        CLIENT_KEY_PATH
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14457, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequestsClientCert_ClientDoesNotProvideClientCert_ConnectionSucceeds_TLS12)
{
    Bn3Monkey::initializeSecuritySocket();
    // Optional client cert: server allows handshake without a client cert
    TLSServerProcess server{ 14458,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -verify 1 -CAfile " + CA_CERT + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        false, false, nullptr,
        false
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14458, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequestsClientCert_ClientProvidesCertSignedByUntrustedCA_ConnectionFails_ClientCertRejected_TLS12)
{
    Bn3Monkey::initializeSecuritySocket();
    // Even with optional verification, presenting a cert from an unknown CA causes failure.
    // -verify_return_error makes the server's verify callback reject on failure.
    // TLS 1.2: SSL_R_TLSV1_ALERT_UNKNOWN_CA received synchronously inside SSL_connect().
    TLSServerProcess server{ 14459,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -verify 1 -CAfile " + CA_CERT + " -verify_return_error -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        false, false, nullptr,
        true,
        CLIENT_UNTRUSTED_CERT,
        CLIENT_UNTRUSTED_KEY
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14459, tls);
    EXPECT_EQ(SocketCode::TLS_CLIENT_CERT_REJECTED, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequiresClientCert_ClientProvidesValidCert_ConnectionSucceeds_TLS13)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14460,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        true,
        CLIENT_CERT_PATH,
        CLIENT_KEY_PATH
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14460, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequiresClientCert_ClientDoesNotProvideAnyClientCert_ConnectionFails_ClientCertRejected_TLS13)
{
    Bn3Monkey::initializeSecuritySocket();
    // TLS 1.3: SSL_connect() returns 1 before server processes the empty
    // Certificate message.  The certificate_required alert is caught by the
    // post-handshake probe (SSL_peek with 100 ms timeout) in reconnect().
    TLSServerProcess server{ 14461,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        false
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14461, tls);
    EXPECT_EQ(SocketCode::TLS_CLIENT_CERT_REJECTED, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequiresClientCert_ClientProvidesCertSignedByUntrustedCA_ConnectionFails_ClientCertRejected_TLS13)
{
    Bn3Monkey::initializeSecuritySocket();
    // TLS 1.3: unknown_ca alert arrives after SSL_connect() succeeds because the
    // server processes the client Certificate after sending its own Finished.
    // -verify_return_error makes the server's verify callback reject on failure,
    // causing the alert to actually be sent.
    // The post-handshake probe in reconnect() catches the deferred alert.
    TLSServerProcess server{ 14462,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT + " -verify_return_error -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        true,
        CLIENT_UNTRUSTED_CERT,
        CLIENT_UNTRUSTED_KEY
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14462, tls);
    EXPECT_EQ(SocketCode::TLS_CLIENT_CERT_REJECTED, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequestsClientCert_ClientProvidesValidCert_ConnectionSucceeds_TLS13)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14463,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -verify 1 -CAfile " + CA_CERT + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        true,
        CLIENT_CERT_PATH,
        CLIENT_KEY_PATH
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14463, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequestsClientCert_ClientDoesNotProvideClientCert_ConnectionSucceeds_TLS13)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14464,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -verify 1 -CAfile " + CA_CERT + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        false
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14464, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequestsClientCert_ClientProvidesCertSignedByUntrustedCA_ConnectionFails_ClientCertRejected_TLS13)
{
    Bn3Monkey::initializeSecuritySocket();
    // TLS 1.3: unknown_ca alert arrives after SSL_connect() succeeds.
    // -verify_return_error makes the server's verify callback reject on failure.
    // The post-handshake probe in reconnect() catches the deferred alert.
    TLSServerProcess server{ 14465,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -verify 1 -CAfile " + CA_CERT + " -verify_return_error -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        true,
        CLIENT_UNTRUSTED_CERT,
        CLIENT_UNTRUSTED_KEY
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14465, tls);
    EXPECT_EQ(SocketCode::TLS_CLIENT_CERT_REJECTED, ret);
    Bn3Monkey::releaseSecuritySocket();
}


// =============================================================================
// GROUP 6: Encrypted Private Key Tests
//   TLS 1.2 server  (ports 14466–14467)
//   TLS 1.3 server  (ports 14468–14469)
// =============================================================================

TEST(TLSConnection, ClientProvidesEncryptedPrivateKey_CorrectPassword_ConnectionSucceeds_TLS12)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14466,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        false, false, nullptr,
        true,
        CLIENT_CERT_PATH,
        CLIENT_ENC_KEY_PATH,
        CLIENT_KEY_PASSWORD  // correct password for the encrypted key
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14466, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientProvidesEncryptedPrivateKey_WrongPassword_ConnectionFails_TLS12)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14467,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        false, false, nullptr,
        true,
        CLIENT_CERT_PATH,
        CLIENT_ENC_KEY_PATH,
        "wrongpassword"  // incorrect password → key load fails → handshake fails
    };
    tls.setOnTLSEvent(trackTLSEvent);
    SocketCode code = tryTLSConnect(14467, tls);
    EXPECT_NE(SocketCode::SUCCESS, code);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientProvidesEncryptedPrivateKey_CorrectPassword_ConnectionSucceeds_TLS13)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14468,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        true,
        CLIENT_CERT_PATH,
        CLIENT_ENC_KEY_PATH,
        CLIENT_KEY_PASSWORD
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(14468, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientProvidesEncryptedPrivateKey_WrongPassword_ConnectionFails_TLS13)
{
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ 14469,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        true,
        CLIENT_CERT_PATH,
        CLIENT_ENC_KEY_PATH,
        "wrongpassword"
    };
    tls.setOnTLSEvent(trackTLSEvent);
    SocketCode code = tryTLSConnect(14469, tls);
    EXPECT_NE(SocketCode::SUCCESS, code);
    Bn3Monkey::releaseSecuritySocket();
}
