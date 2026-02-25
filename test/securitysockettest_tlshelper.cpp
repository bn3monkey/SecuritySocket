#include "securitysockettest_tlshelper.hpp"


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


void createCertificates()
{
    using namespace Bn3Monkey;

    auto* remote_ip = Bn3Monkey::getRemoteCommandServerAddress(getClient());

	printf("Check if OpenSSL is available on the remote system:\n");
    runCommand(getClient(), "openssl version");
    runCommand(getClient(), "where openssl");

    // Create the output directory that holds every test certificate file.
    createDirectory(getClient(), "certs");

    // -------------------------------------------------------------------------
    // Root CA
    // -------------------------------------------------------------------------

    // Generate a 2048-bit RSA private key that will be used to sign all
    // CA-issued certificates in this test suite.
    printf("Generate CA private key(2048 - bit RSA\n");
    runCommand(getClient(), "openssl genrsa -out certs/ca.key 2048");

    // Create a self-signed CA certificate valid for 10 years (3650 days).
    // This cert is the trust anchor supplied to clients/servers during tests.
    printf("Create self-signed CA certificate (CN=TestCA, 10 years)\n");
    runCommand(getClient(), "openssl req -new -x509 -days 3650 -key certs/ca.key -out certs/ca.crt"
           " -subj \"/CN=TestCA\"");

    // -------------------------------------------------------------------------
    // Server certificate signed by the CA  (SAN = IP:127.0.0.1)
    // -------------------------------------------------------------------------

    // Generate a 2048-bit RSA private key for the CA-signed server certificate.
    printf("Generate CA-signed server private key (2048-bit RSA)\n");
	runCommand(getClient(),
           "openssl genrsa -out certs/server_ca.key 2048");

    // Create a Certificate Signing Request with CN=127.0.0.1 for the server.
    printf("Create CSR for CA-signed server certificate (CN=127.0.0.1)\n");
	runCommand(getClient(),
           "openssl req -new -key certs/server_ca.key -out certs/server_ca.csr"
           " -subj \"/CN=127.0.0.1\"");

    // Write the Subject Alternative Name extension file.
    // Modern TLS clients require a SAN entry for IP address validation;
    // CN alone is no longer sufficient for hostname/IP verification.
    // Include both 127.0.0.1 (localhost) and the server's external IP so that
    // hostname verification succeeds regardless of which address the client uses.
    {
        std::string remote_ip_str = remote_ip;
        printf("Write SAN extension file for server certificate (SAN=IP:127.0.0.1,IP:%s)\n", remote_ip);
        if (remote_ip_str == "127.0.0.1") {
            // Server is local — a single SAN entry covers both cases.
            runCommand(getClient(), "cmd /c echo subjectAltName=IP:127.0.0.1 > certs/san.ext");
        } else {
            std::string san_cmd =
                "cmd /c echo subjectAltName=IP:127.0.0.1,IP:" + remote_ip_str + " > certs/san.ext";
            runCommand(getClient(), "%s", san_cmd.c_str());
        }
    }
    
    // Sign the server CSR with the CA and embed the IP SAN so that
    // clients connecting to 127.0.0.1 can verify the server certificate.
    printf("Sign CA-signed server certificate with CA (SAN=IP:127.0.0.1)\n");
	runCommand(getClient(),
           "openssl x509 -req -days 3650"
           " -in certs/server_ca.csr -CA certs/ca.crt -CAkey certs/ca.key"
           " -CAcreateserial -out certs/server_ca.crt -extfile certs/san.ext");

    // -------------------------------------------------------------------------
    // Self-signed server certificate (no CA)
    // -------------------------------------------------------------------------

    // Generate a server certificate that is NOT signed by any CA.
    // Used in tests where the client intentionally skips certificate verification.
    printf("Generate self-signed server certificate (no CA, CN=127.0.0.1)\n");
    runCommand(getClient(),
           "openssl req -x509 -newkey rsa:2048 -days 3650 -nodes"
           " -keyout certs/server_self.key -out certs/server_self.crt"
           " -subj \"/CN=127.0.0.1\"");

    // -------------------------------------------------------------------------
    // Server certificate with wrong CN (hostname-mismatch test)
    // -------------------------------------------------------------------------

    // Generate the private key for a server cert whose CN does not match the
    // connection address (127.0.0.1), triggering a hostname-mismatch failure.
    printf("Generate wrong-CN server private key (2048-bit RSA)\n");
    runCommand(getClient(),
           "openssl genrsa -out certs/server_wrong_cn.key 2048");

    // Create a CSR with CN=wronghost — intentionally different from 127.0.0.1.
    printf("Create CSR for wrong-CN server certificate (CN=wronghost)\n");
	runCommand(getClient(),
           "openssl req -new -key certs/server_wrong_cn.key"
           " -out certs/server_wrong_cn.csr -subj \"/CN=wronghost\"");

    // Sign the wrong-CN cert with the trusted CA.
    // Certificate chain validation succeeds, but hostname verification fails.
    printf("Sign wrong-CN server certificate with trusted CA\n");
    runCommand(getClient(),        
           "openssl x509 -req -days 3650"
           " -in certs/server_wrong_cn.csr -CA certs/ca.crt -CAkey certs/ca.key"
           " -CAcreateserial -out certs/server_wrong_cn.crt");

    // -------------------------------------------------------------------------
    // Client certificate signed by the CA  (mTLS / mutual-auth tests)
    // -------------------------------------------------------------------------

    // Generate a 2048-bit RSA private key for the trusted client certificate.
    printf("Generate trusted client private key (2048-bit RSA)\n");
    runCommand(getClient(),
           "openssl genrsa -out certs/client.key 2048");

    // Create a CSR for the trusted client certificate.
    printf("Create CSR for trusted client certificate (CN=TestClient)\n");
	runCommand(getClient(),
           "openssl req -new -key certs/client.key -out certs/client.csr"
           " -subj \"/CN=TestClient\"");

    // Sign the client certificate with the trusted CA so that servers
    // configured to require client authentication will accept it.
    printf("Sign trusted client certificate with CA\n");
    runCommand(getClient(),
           "openssl x509 -req -days 3650"
           " -in certs/client.csr -CA certs/ca.crt -CAkey certs/ca.key"
           " -CAcreateserial -out certs/client.crt");

    // Re-encrypt the client private key using AES-256 and the password "test1234".
    // Used in tests that verify the client can load a passphrase-protected key.
    printf("Encrypt client private key with AES-256 (password: test1234)\n");
    runCommand(getClient(),
           "openssl rsa -in certs/client.key -aes256"
           " -passout pass:test1234 -out certs/client_enc.key");

    // -------------------------------------------------------------------------
    // Untrusted CA and a client certificate signed by it
    // -------------------------------------------------------------------------

    // Generate a private key for a CA that will NOT appear in any server's
    // trust store, simulating a client cert from an unknown issuer.
    printf("Generate untrusted CA private key (2048-bit RSA)\n");
	runCommand(getClient(),
           "openssl genrsa -out certs/untrusted_ca.key 2048");

    // Create a self-signed certificate for the untrusted CA.
    printf("Create self-signed untrusted CA certificate (CN=UntrustedCA)\n");
    runCommand(getClient(),
           "openssl req -new -x509 -days 3650"
           " -key certs/untrusted_ca.key -out certs/untrusted_ca.crt"
           " -subj \"/CN=UntrustedCA\"");

    // Generate a private key for the client certificate issued by the untrusted CA.
    printf("Generate untrusted-client private key (2048-bit RSA)\n");
    runCommand(getClient(),
           "openssl genrsa -out certs/client_untrusted.key 2048");

    // Create a CSR for the untrusted client certificate.
    printf("Create CSR for untrusted-client certificate (CN=UntrustedClient)\n");
    runCommand(getClient(),
           "openssl req -new -key certs/client_untrusted.key"
           " -out certs/client_untrusted.csr -subj \"/CN=UntrustedClient\"");

    // Sign the untrusted client cert with the untrusted CA.
    // Servers that only trust the primary CA will reject this certificate.
    printf("Sign untrusted-client certificate with untrusted CA\n");
    runCommand(getClient(),
           "openssl x509 -req -days 3650"
           " -in certs/client_untrusted.csr"
           " -CA certs/untrusted_ca.crt -CAkey certs/untrusted_ca.key"
           " -CAcreateserial -out certs/client_untrusted.crt");

    printf("[cert-gen] Certificate generation complete.\n");
    
    // Download all certificate files from the server to the local certs/ directory.
    // localCertPath() prepends getCWD() so files land in the correct location on
    // Android internal storage (or the current directory on other platforms).
    createLocalDirectory(localCertPath("certs").c_str());
    printf("[cert-gen] Downloading certificates from server...\n");
    downloadFile(getClient(), localCertPath(SERVER_SELF_CERT).c_str()      , SERVER_SELF_CERT     );
    downloadFile(getClient(), localCertPath(SERVER_SELF_KEY).c_str()       , SERVER_SELF_KEY      );
    downloadFile(getClient(), localCertPath(CA_CERT).c_str()               , CA_CERT              );
    downloadFile(getClient(), localCertPath(SERVER_CA_CERT).c_str()        , SERVER_CA_CERT       );
    downloadFile(getClient(), localCertPath(SERVER_CA_KEY).c_str()         , SERVER_CA_KEY        );
    downloadFile(getClient(), localCertPath(SERVER_WRONG_CN_CERT).c_str()  , SERVER_WRONG_CN_CERT );
    downloadFile(getClient(), localCertPath(SERVER_WRONG_CN_KEY).c_str()   , SERVER_WRONG_CN_KEY  );
    downloadFile(getClient(), localCertPath(CLIENT_CERT_PATH).c_str()      , CLIENT_CERT_PATH     );
    downloadFile(getClient(), localCertPath(CLIENT_KEY_PATH).c_str()       , CLIENT_KEY_PATH      );
    downloadFile(getClient(), localCertPath(CLIENT_ENC_KEY_PATH).c_str()   , CLIENT_ENC_KEY_PATH  );
    downloadFile(getClient(), localCertPath(UNTRUSTED_CA_CERT).c_str()     , UNTRUSTED_CA_CERT    );
    downloadFile(getClient(), localCertPath(CLIENT_UNTRUSTED_CERT).c_str() , CLIENT_UNTRUSTED_CERT);
    downloadFile(getClient(), localCertPath(CLIENT_UNTRUSTED_KEY).c_str()  , CLIENT_UNTRUSTED_KEY );
    printf("[cert-gen] Certificate download complete.\n");
}

int32_t LocalProcess::openProcess(const char* cmd, ...)
{
    if (!cmd)
        return -1;

    char buffer[2048]{ 0 };

    va_list args;
    va_start(args, cmd);
    vsnprintf(buffer, sizeof(buffer), cmd, args);
    va_end(args);

#if defined(_WIN32)

    // --------- Windows ---------


    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    BOOL result = CreateProcessA(
        nullptr,
        buffer,
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    if (!result)
        return -1;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return static_cast<int32_t>(pi.dwProcessId);

#else

    // --------- Linux ---------

    pid_t pid = 0;

    // sh -c "your full command"
    char* argv[] = {
        const_cast<char*>("/bin/sh"),
        const_cast<char*>("-c"),
        buffer,
        nullptr
    };

    int result = posix_spawnp(
        &pid,
        "/bin/sh",
        nullptr,
        nullptr,
        argv,
        environ
    );

    if (result != 0)
        return -1;

    return static_cast<int32_t>(pid);

#endif
}

void LocalProcess::closeProcess(int32_t process_id)
{
    if (process_id <= 0)
        return;

#if defined(_WIN32)

    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, process_id);
    if (!hProcess)
        return;

    TerminateProcess(hProcess, 0);
    CloseHandle(hProcess);

#else

    // 1️⃣ 정상 종료 시도
    kill(process_id, SIGTERM);

    // 2️⃣ 최대 3초 대기
    for (int i = 0; i < 30; ++i)
    {
        if (kill(process_id, 0) != 0)
            break; // 이미 종료됨

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 3️⃣ 아직 살아있으면 강제 종료
    if (kill(process_id, 0) == 0)
    {
        kill(process_id, SIGKILL);
    }

    // 4️⃣ 우리가 부모라면 reap
    waitpid(process_id, nullptr, WNOHANG);

#endif
}