#define WIN32_LEAN_AND_MEAN

#include <SecuritySocket.hpp>

#include <gtest/gtest.h>
#include "securitysockettest.hpp"
#include "securitysockettest_tlshelper.hpp"

#include <string>
#include <thread>
#include <chrono>
#include <random>

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
        
        pid = Bn3Monkey::openProcess(getClient(), "openssl s_server -accept %d -trace -state %s", port, extra_args.c_str());
        // pid = LocalProcess::openProcess("openssl s_server -accept %d -trace -state %s", port, extra_args.c_str());

        _started = pid != -1;
        if (_started)
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
    }

    ~TLSServerProcess()
    {
		Bn3Monkey::closeProcess(getClient(), pid);
        // LocalProcess::closeProcess(pid);
    }

    bool started() const { return _started; }

private:
    bool _started{ false };
    int32_t pid{ -1 };
};


// =============================================================================
// Helper: create a TLS SocketClient and attempt connect; returns SocketCode.
// max_retries=1 prevents reusing a broken SSL object on failure.
// =============================================================================
// ip=nullptr → connect to the remote server's discovered address (default).
// Pass an explicit IP string to test a specific address (e.g. "127.0.0.1").
static SocketCode tryTLSConnect(unsigned short port,
                                const SocketTLSClientConfiguration& tls_cfg,
                                const char* ip = nullptr)
{
    if (ip == nullptr)
        ip = Bn3Monkey::getRemoteCommandServerAddress(getClient());
    SocketConfiguration config{
        ip,
        port,
        false,  // not unix domain
        1,      // max_retries = 1
        3000,   // read_timeout_ms
        3000,   // write_timeout_ms
        1000,   // time_between_retries_ms
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

static bool hasPrefix(const char* prefix, const char* message)
{
    if (prefix == nullptr || message == nullptr)
        return false;

    const std::size_t n = std::strlen(prefix);
    // Empty prefix matches everything (common convention)
    return std::strncmp(message, prefix, n) == 0;
}
void trackTLSEvent(const char* message) {
    if (!hasPrefix("Handshake error", message)) 
    {
        printf("|Client| %s\n", message);
    }
}

static uint16_t generateRandomPort()
{
    static std::mt19937 gen(std::random_device{}());
    static std::uniform_int_distribution<int> dist(15000, 16000);
    return dist(gen);
}

// =============================================================================
// GROUP 1: TLS Version Tests  (ports 14430–14435)
// =============================================================================

TEST(TLSConnection, ClientSupports_TLS12Only_ServerSupports_TLS12Only_ConnectionSucceeds)
{
	auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{ {SocketTLSVersion::TLS1_2} };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientSupports_TLS13Only_ServerSupports_TLS13Only_ConnectionSucceeds)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{ {SocketTLSVersion::TLS1_3} };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientSupports_TLS12And13_ServerSupports_TLS12Only_ConnectionSucceeds)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{ {SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3} };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientSupports_TLS12And13_ServerSupports_TLS13Only_ConnectionSucceeds)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{ {SocketTLSVersion::TLS1_2, SocketTLSVersion::TLS1_3} };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientSupports_TLS12Only_ServerSupports_TLS13Only_ConnectionFails_TLSVersionNotSupported)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{ {SocketTLSVersion::TLS1_2} };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::TLS_VERSION_NOT_SUPPORTED, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientSupports_TLS13Only_ServerSupports_TLS12Only_ConnectionFails_TLSVersionNotSupported)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{ {SocketTLSVersion::TLS1_3} };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::TLS_VERSION_NOT_SUPPORTED, ret);
    Bn3Monkey::releaseSecuritySocket();
}


// =============================================================================
// GROUP 2: Cipher Suite Tests  (ports 14436–14439)
// =============================================================================

TEST(TLSConnection, ClientSpecifiesTLS12Cipher_ServerSupportsMatchingCipher_ConnectionSucceeds)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_3 -cipher ECDHE-RSA-AES256-GCM-SHA384" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {SocketTLS1_2CipherSuite::ECDHE_RSA_AES256_GCM_SHA384}
    };
	auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientSpecifiesTLS13Cipher_ServerSupportsMatchingCipher_ConnectionSucceeds)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_2 -ciphersuites TLS_AES_256_GCM_SHA384" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {},
        {SocketTLS1_3CipherSuite::TLS_AES_256_GCM_SHA384}
    };
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientSpecifiesTLS12Cipher_ServerDoesNotSupportCipher_ConnectionFails_CipherSuiteMismatch)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    // Server: only ECDHE-RSA-AES128-GCM-SHA256  |  Client: only ECDHE-RSA-AES256-GCM-SHA384
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_3 -cipher ECDHE-RSA-AES128-GCM-SHA256" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {SocketTLS1_2CipherSuite::ECDHE_RSA_AES256_GCM_SHA384}
    };
    tls.setOnTLSEvent(trackTLSEvent);
	auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::TLS_CIPHER_SUITE_MISMATCH, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientSpecifiesTLS13Cipher_ServerDoesNotSupportCipher_ConnectionFails_CipherSuiteMismatch)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    // Server: only TLS_AES_256_GCM_SHA384  |  Client: only TLS_AES_128_GCM_SHA256
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_2 -ciphersuites TLS_AES_256_GCM_SHA384" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {},
        {SocketTLS1_3CipherSuite::TLS_AES_128_GCM_SHA256}
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
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
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        false   // verify_server = false
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesServerCert_ServerCertSignedByTrustedCA_ConnectionSucceeds_TLS12)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_CA_CERT + " -key " + SERVER_CA_KEY
        + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        true,    // verify_server
        false,   // verify_hostname
        localCertPath(CA_CERT).c_str()
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesServerCert_ServerHasSelfSignedCert_ConnectionFails_ServerCertInvalid_TLS12)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        true,    // verify_server
        false,   // verify_hostname
        localCertPath(CA_CERT).c_str()  // self-signed cert is NOT in this CA chain
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::TLS_SERVER_CERT_INVALID, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesServerCert_ServerCertSignedByUntrustedCA_ConnectionFails_ServerCertInvalid_TLS12)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_CA_CERT + " -key " + SERVER_CA_KEY
        + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        true,              // verify_server
        false,             // verify_hostname
        localCertPath(UNTRUSTED_CA_CERT).c_str()  // wrong CA — chain verification fails
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::TLS_SERVER_CERT_INVALID, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientDoesNotVerifyServerCert_ServerHasSelfSignedCert_ConnectionSucceeds_TLS13)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        false   // verify_server = false
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesServerCert_ServerCertSignedByTrustedCA_ConnectionSucceeds_TLS13)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_CA_CERT + " -key " + SERVER_CA_KEY
        + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        true,    // verify_server
        false,   // verify_hostname
        localCertPath(CA_CERT).c_str()
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesServerCert_ServerHasSelfSignedCert_ConnectionFails_ServerCertInvalid_TLS13)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        true,    // verify_server
        false,   // verify_hostname
        localCertPath(CA_CERT).c_str()
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::TLS_SERVER_CERT_INVALID, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesServerCert_ServerCertSignedByUntrustedCA_ConnectionFails_ServerCertInvalid_TLS13)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_CA_CERT + " -key " + SERVER_CA_KEY
        + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        true,              // verify_server
        false,             // verify_hostname
        localCertPath(UNTRUSTED_CA_CERT).c_str()
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
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
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    // server_ca.crt has CN=127.0.0.1 / SAN=IP:127.0.0.1,IP:<remote_ip>
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_CA_CERT + " -key " + SERVER_CA_KEY
        + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        true,   // verify_server
        true,   // verify_hostname
        localCertPath(CA_CERT).c_str()
    };
    tls.setOnTLSEvent(trackTLSEvent);

    // Remote IP: cert SAN includes the server's external IP → match
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);

    // Localhost IP: cert SAN also includes 127.0.0.1 → match
    // (Requires the server to be reachable via 127.0.0.1 from the test machine)
    /*
    auto ret2 = tryTLSConnect(port, tls, "127.0.0.1");
    EXPECT_EQ(SocketCode::SUCCESS, ret2);
    */
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesHostname_ServerCertDoesNotMatchHostname_ConnectionFails_HostnameMismatch_TLS12)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    // server_wrong_cn.crt has CN=wronghost, no SAN — mismatches any real IP
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_WRONG_CN_CERT + " -key " + SERVER_WRONG_CN_KEY
        + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        true,   // verify_server
        true,   // verify_hostname
        localCertPath(CA_CERT).c_str()
    };
    tls.setOnTLSEvent(trackTLSEvent);

    // Remote IP: CN=wronghost does not match → mismatch
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::TLS_HOSTNAME_MISMATCH, ret);

    /*
    // Localhost IP: CN=wronghost does not match 127.0.0.1 → mismatch
    auto ret2 = tryTLSConnect(port, tls, "127.0.0.1");
    EXPECT_EQ(SocketCode::TLS_HOSTNAME_MISMATCH, ret2);
    */

    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientDoesNotVerifyHostname_ServerCertDoesNotMatchHostname_ConnectionSucceeds_TLS12)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    // CN=wronghost but verify_hostname is false — chain check still passes
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_WRONG_CN_CERT + " -key " + SERVER_WRONG_CN_KEY
        + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        true,   // verify_server (cert chain is valid — signed by CA)
        false,  // verify_hostname = false
        localCertPath(CA_CERT).c_str()
    };
    tls.setOnTLSEvent(trackTLSEvent);

    // Remote IP: hostname not checked → success despite CN mismatch
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);

    /*
    // Localhost IP: hostname not checked → success
    auto ret2 = tryTLSConnect(port, tls, "127.0.0.1");
    EXPECT_EQ(SocketCode::SUCCESS, ret2);
    */

    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesHostname_ServerCertMatchesHostname_ConnectionSucceeds_TLS13)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    // server_ca.crt has CN=127.0.0.1 / SAN=IP:127.0.0.1,IP:<remote_ip>
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_CA_CERT + " -key " + SERVER_CA_KEY
        + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        true,   // verify_server
        true,   // verify_hostname
        localCertPath(CA_CERT).c_str()
    };
    tls.setOnTLSEvent(trackTLSEvent);

    // Remote IP: cert SAN includes the server's external IP → match
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);

    /*
    // Localhost IP: cert SAN also includes 127.0.0.1 → match
    auto ret2 = tryTLSConnect(port, tls, "127.0.0.1");
    EXPECT_EQ(SocketCode::SUCCESS, ret2);
    */

    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientVerifiesHostname_ServerCertDoesNotMatchHostname_ConnectionFails_HostnameMismatch_TLS13)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    // server_wrong_cn.crt has CN=wronghost, no SAN — mismatches any real IP
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_WRONG_CN_CERT + " -key " + SERVER_WRONG_CN_KEY
        + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        true,   // verify_server
        true,   // verify_hostname
        localCertPath(CA_CERT).c_str()
    };
    tls.setOnTLSEvent(trackTLSEvent);

    // Remote IP: CN=wronghost does not match → mismatch
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::TLS_HOSTNAME_MISMATCH, ret);

    /*
    // Localhost IP: CN=wronghost does not match 127.0.0.1 → mismatch
    auto ret2 = tryTLSConnect(port, tls, "127.0.0.1");
    EXPECT_EQ(SocketCode::TLS_HOSTNAME_MISMATCH, ret2);
    */

    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientDoesNotVerifyHostname_ServerCertDoesNotMatchHostname_ConnectionSucceeds_TLS13)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_WRONG_CN_CERT + " -key " + SERVER_WRONG_CN_KEY
        + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        true,   // verify_server
        false,  // verify_hostname = false
        localCertPath(CA_CERT).c_str()
    };
    tls.setOnTLSEvent(trackTLSEvent);

    // Remote IP: hostname not checked → success despite CN mismatch
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);

    /*
    // Localhost IP: hostname not checked → success
    auto ret2 = tryTLSConnect(port, tls, "127.0.0.1");
    EXPECT_EQ(SocketCode::SUCCESS, ret2);
    */

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
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    // -Verify (capital V) = require client certificate
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        false, false, nullptr,
        true,
        localCertPath(CLIENT_CERT_PATH).c_str(),
        localCertPath(CLIENT_KEY_PATH).c_str()
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequiresClientCert_ClientDoesNotProvideAnyClientCert_ConnectionFails_ClientCertRejected_TLS12)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    // TLS 1.2: server sends handshake_failure synchronously inside SSL_connect().
    // SSL_R_SSLV3_ALERT_HANDSHAKE_FAILURE + no cert configured on client side.
    TLSServerProcess server{ port,
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
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::TLS_CLIENT_CERT_REJECTED, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequiresClientCert_ClientProvidesCertSignedByUntrustedCA_ConnectionFails_ClientCertRejected_TLS12)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    // Server trusts only CA_CERT; client presents a cert from a different CA.
    // -verify_return_error makes the server's verify callback actually reject the
    // connection (return 0) on failure, instead of the default behaviour of
    // printing the error and continuing.
    // TLS 1.2: SSL_R_TLSV1_ALERT_UNKNOWN_CA received synchronously inside SSL_connect().
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT + " -verify_return_error -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        false, false, nullptr,
        true,
        localCertPath(CLIENT_UNTRUSTED_CERT).c_str(),
        localCertPath(CLIENT_UNTRUSTED_KEY).c_str()
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::TLS_CLIENT_CERT_REJECTED, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequestsClientCert_ClientProvidesValidCert_ConnectionSucceeds_TLS12)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    // -verify (lowercase v) = request client certificate (optional)
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -verify 1 -CAfile " + CA_CERT + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        false, false, nullptr,
        true,
        localCertPath(CLIENT_CERT_PATH).c_str(),
        localCertPath(CLIENT_KEY_PATH).c_str()
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequestsClientCert_ClientDoesNotProvideClientCert_ConnectionSucceeds_TLS12)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    // Optional client cert: server allows handshake without a client cert
    TLSServerProcess server{ port,
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
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequestsClientCert_ClientProvidesCertSignedByUntrustedCA_ConnectionFails_ClientCertRejected_TLS12)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    // Even with optional verification, presenting a cert from an unknown CA causes failure.
    // -verify_return_error makes the server's verify callback reject on failure.
    // TLS 1.2: SSL_R_TLSV1_ALERT_UNKNOWN_CA received synchronously inside SSL_connect().
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -verify 1 -CAfile " + CA_CERT + " -verify_return_error -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        false, false, nullptr,
        true,
        localCertPath(CLIENT_UNTRUSTED_CERT).c_str(),
        localCertPath(CLIENT_UNTRUSTED_KEY).c_str()
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::TLS_CLIENT_CERT_REJECTED, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequiresClientCert_ClientProvidesValidCert_ConnectionSucceeds_TLS13)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        true,
        localCertPath(CLIENT_CERT_PATH).c_str(),
        localCertPath(CLIENT_KEY_PATH).c_str()
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequiresClientCert_ClientDoesNotProvideAnyClientCert_ConnectionFails_ClientCertRejected_TLS13)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    // TLS 1.3: SSL_connect() returns 1 before server processes the empty
    // Certificate message.  The certificate_required alert is caught by the
    // post-handshake probe (SSL_peek with 100 ms timeout) in reconnect().
    TLSServerProcess server{ port,
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
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::TLS_CLIENT_CERT_REJECTED, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequiresClientCert_ClientProvidesCertSignedByUntrustedCA_ConnectionFails_ClientCertRejected_TLS13)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    // TLS 1.3: unknown_ca alert arrives after SSL_connect() succeeds because the
    // server processes the client Certificate after sending its own Finished.
    // -verify_return_error makes the server's verify callback reject on failure,
    // causing the alert to actually be sent.
    // The post-handshake probe in reconnect() catches the deferred alert.
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT + " -verify_return_error -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        true,
        localCertPath(CLIENT_UNTRUSTED_CERT).c_str(),
        localCertPath(CLIENT_UNTRUSTED_KEY).c_str()
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::TLS_CLIENT_CERT_REJECTED, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequestsClientCert_ClientProvidesValidCert_ConnectionSucceeds_TLS13)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -verify 1 -CAfile " + CA_CERT + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        true,
        localCertPath(CLIENT_CERT_PATH).c_str(),
        localCertPath(CLIENT_KEY_PATH).c_str()
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequestsClientCert_ClientDoesNotProvideClientCert_ConnectionSucceeds_TLS13)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
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
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ServerRequestsClientCert_ClientProvidesCertSignedByUntrustedCA_ConnectionFails_ClientCertRejected_TLS13)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    // TLS 1.3: unknown_ca alert arrives after SSL_connect() succeeds.
    // -verify_return_error makes the server's verify callback reject on failure.
    // The post-handshake probe in reconnect() catches the deferred alert.
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -verify 1 -CAfile " + CA_CERT + " -verify_return_error -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        true,
        localCertPath(CLIENT_UNTRUSTED_CERT).c_str(),
        localCertPath(CLIENT_UNTRUSTED_KEY).c_str()
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
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
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        false, false, nullptr,
        true,
        localCertPath(CLIENT_CERT_PATH).c_str(),
        localCertPath(CLIENT_ENC_KEY_PATH).c_str(),
        CLIENT_KEY_PASSWORD  // correct password for the encrypted key
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientProvidesEncryptedPrivateKey_WrongPassword_ConnectionFails_TLS12)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT + " -no_tls1_3" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_2},
        {}, {},
        false, false, nullptr,
        true,
        localCertPath(CLIENT_CERT_PATH).c_str(),
        localCertPath(CLIENT_ENC_KEY_PATH).c_str(),
        "wrongpassword"  // incorrect password → key load fails → handshake fails
    };
    tls.setOnTLSEvent(trackTLSEvent);
    SocketCode code = tryTLSConnect(port, tls);
    EXPECT_NE(SocketCode::SUCCESS, code);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientProvidesEncryptedPrivateKey_CorrectPassword_ConnectionSucceeds_TLS13)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        true,
        localCertPath(CLIENT_CERT_PATH).c_str(),
        localCertPath(CLIENT_ENC_KEY_PATH).c_str(),
        CLIENT_KEY_PASSWORD
    };
    tls.setOnTLSEvent(trackTLSEvent);
    auto ret = tryTLSConnect(port, tls);
    EXPECT_EQ(SocketCode::SUCCESS, ret);
    Bn3Monkey::releaseSecuritySocket();
}

TEST(TLSConnection, ClientProvidesEncryptedPrivateKey_WrongPassword_ConnectionFails_TLS13)
{
    auto port = generateRandomPort();
    Bn3Monkey::initializeSecuritySocket();
    TLSServerProcess server{ port,
        std::string("-cert ") + SERVER_SELF_CERT + " -key " + SERVER_SELF_KEY
        + " -Verify 1 -CAfile " + CA_CERT + " -no_tls1_2" };
    ASSERT_TRUE(server.started());

    SocketTLSClientConfiguration tls{
        {SocketTLSVersion::TLS1_3},
        {}, {},
        false, false, nullptr,
        true,
        localCertPath(CLIENT_CERT_PATH).c_str(),
        localCertPath(CLIENT_ENC_KEY_PATH).c_str(),
        "wrongpassword"
    };
    tls.setOnTLSEvent(trackTLSEvent);
    SocketCode code = tryTLSConnect(port, tls);
    EXPECT_NE(SocketCode::SUCCESS, code);
    Bn3Monkey::releaseSecuritySocket();
}
