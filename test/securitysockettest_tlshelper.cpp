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
    // If the root CA certificate is already present, all certs have been
    // generated in a previous run — skip generation entirely.
    if (directoryExists("certs"))
    {
        printf("[cert-gen] Certificates already exist; skipping generation.\n");
    }
    else
    {
    runCommand("Check openssl", "openssl version");
    runCommand("Check openssl", "where openssl");

    // Create the output directory that holds every test certificate file.
    createDirectory("certs");

    // -------------------------------------------------------------------------
    // Root CA
    // -------------------------------------------------------------------------

    // Generate a 2048-bit RSA private key that will be used to sign all
    // CA-issued certificates in this test suite.
    runCommand("Generate CA private key (2048-bit RSA)",
           "openssl genrsa -out certs/ca.key 2048");

    // Create a self-signed CA certificate valid for 10 years (3650 days).
    // This cert is the trust anchor supplied to clients/servers during tests.
    runCommand("Create self-signed CA certificate (CN=TestCA, 10 years)",
           "openssl req -new -x509 -days 3650 -key certs/ca.key -out certs/ca.crt"
           " -subj \"/CN=TestCA\"");

    // -------------------------------------------------------------------------
    // Server certificate signed by the CA  (SAN = IP:127.0.0.1)
    // -------------------------------------------------------------------------

    // Generate a 2048-bit RSA private key for the CA-signed server certificate.
    runCommand("Generate CA-signed server private key (2048-bit RSA)",
           "openssl genrsa -out certs/server_ca.key 2048");

    // Create a Certificate Signing Request with CN=127.0.0.1 for the server.
    runCommand("Create CSR for CA-signed server certificate (CN=127.0.0.1)",
           "openssl req -new -key certs/server_ca.key -out certs/server_ca.csr"
           " -subj \"/CN=127.0.0.1\"");

    // Write the Subject Alternative Name extension file.
    // Modern TLS clients require a SAN entry for IP address validation;
    // CN alone is no longer sufficient for hostname/IP verification.
    {
        // std::ofstream san("certs/san.ext");
        // san << "subjectAltName=IP:127.0.0.1\n";
		runCommand("Write SAN extension file for server certificate (SAN=IP:", "echo \"subjectAltName=IP:127.0.0.1\" > certs/san.ext");
    }
    
    // Sign the server CSR with the CA and embed the IP SAN so that
    // clients connecting to 127.0.0.1 can verify the server certificate.
    runCommand("Sign CA-signed server certificate with CA (SAN=IP:127.0.0.1)",
           "openssl x509 -req -days 3650"
           " -in certs/server_ca.csr -CA certs/ca.crt -CAkey certs/ca.key"
           " -CAcreateserial -out certs/server_ca.crt -extfile certs/san.ext");

    // -------------------------------------------------------------------------
    // Self-signed server certificate (no CA)
    // -------------------------------------------------------------------------

    // Generate a server certificate that is NOT signed by any CA.
    // Used in tests where the client intentionally skips certificate verification.
    runCommand("Generate self-signed server certificate (no CA, CN=127.0.0.1)",
           "openssl req -x509 -newkey rsa:2048 -days 3650 -nodes"
           " -keyout certs/server_self.key -out certs/server_self.crt"
           " -subj \"/CN=127.0.0.1\"");

    // -------------------------------------------------------------------------
    // Server certificate with wrong CN (hostname-mismatch test)
    // -------------------------------------------------------------------------

    // Generate the private key for a server cert whose CN does not match the
    // connection address (127.0.0.1), triggering a hostname-mismatch failure.
    runCommand("Generate wrong-CN server private key (2048-bit RSA)",
           "openssl genrsa -out certs/server_wrong_cn.key 2048");

    // Create a CSR with CN=wronghost — intentionally different from 127.0.0.1.
    runCommand("Create CSR for wrong-CN server certificate (CN=wronghost)",
           "openssl req -new -key certs/server_wrong_cn.key"
           " -out certs/server_wrong_cn.csr -subj \"/CN=wronghost\"");

    // Sign the wrong-CN cert with the trusted CA.
    // Certificate chain validation succeeds, but hostname verification fails.
    runCommand("Sign wrong-CN server certificate with trusted CA",
           "openssl x509 -req -days 3650"
           " -in certs/server_wrong_cn.csr -CA certs/ca.crt -CAkey certs/ca.key"
           " -CAcreateserial -out certs/server_wrong_cn.crt");

    // -------------------------------------------------------------------------
    // Client certificate signed by the CA  (mTLS / mutual-auth tests)
    // -------------------------------------------------------------------------

    // Generate a 2048-bit RSA private key for the trusted client certificate.
    runCommand("Generate trusted client private key (2048-bit RSA)",
           "openssl genrsa -out certs/client.key 2048");

    // Create a CSR for the trusted client certificate.
    runCommand("Create CSR for trusted client certificate (CN=TestClient)",
           "openssl req -new -key certs/client.key -out certs/client.csr"
           " -subj \"/CN=TestClient\"");

    // Sign the client certificate with the trusted CA so that servers
    // configured to require client authentication will accept it.
    runCommand("Sign trusted client certificate with CA",
           "openssl x509 -req -days 3650"
           " -in certs/client.csr -CA certs/ca.crt -CAkey certs/ca.key"
           " -CAcreateserial -out certs/client.crt");

    // Re-encrypt the client private key using AES-256 and the password "test1234".
    // Used in tests that verify the client can load a passphrase-protected key.
    runCommand("Encrypt client private key with AES-256 (password: test1234)",
           "openssl rsa -in certs/client.key -aes256"
           " -passout pass:test1234 -out certs/client_enc.key");

    // -------------------------------------------------------------------------
    // Untrusted CA and a client certificate signed by it
    // -------------------------------------------------------------------------

    // Generate a private key for a CA that will NOT appear in any server's
    // trust store, simulating a client cert from an unknown issuer.
    runCommand("Generate untrusted CA private key (2048-bit RSA)",
           "openssl genrsa -out certs/untrusted_ca.key 2048");

    // Create a self-signed certificate for the untrusted CA.
    runCommand("Create self-signed untrusted CA certificate (CN=UntrustedCA)",
           "openssl req -new -x509 -days 3650"
           " -key certs/untrusted_ca.key -out certs/untrusted_ca.crt"
           " -subj \"/CN=UntrustedCA\"");

    // Generate a private key for the client certificate issued by the untrusted CA.
    runCommand("Generate untrusted-client private key (2048-bit RSA)",
           "openssl genrsa -out certs/client_untrusted.key 2048");

    // Create a CSR for the untrusted client certificate.
    runCommand("Create CSR for untrusted-client certificate (CN=UntrustedClient)",
           "openssl req -new -key certs/client_untrusted.key"
           " -out certs/client_untrusted.csr -subj \"/CN=UntrustedClient\"");

    // Sign the untrusted client cert with the untrusted CA.
    // Servers that only trust the primary CA will reject this certificate.
    runCommand("Sign untrusted-client certificate with untrusted CA",
           "openssl x509 -req -days 3650"
           " -in certs/client_untrusted.csr"
           " -CA certs/untrusted_ca.crt -CAkey certs/untrusted_ca.key"
           " -CAcreateserial -out certs/client_untrusted.crt");

    printf("[cert-gen] Certificate generation complete.\n");
    } // end generation block

    // Download all certificate files from the server to the local certs/ directory.
    createLocalDirectory("certs");
    printf("[cert-gen] Downloading certificates from server...\n");
    downloadFile(SERVER_SELF_CERT,      SERVER_SELF_CERT);
    downloadFile(SERVER_SELF_KEY,       SERVER_SELF_KEY);
    downloadFile(CA_CERT,               CA_CERT);
    downloadFile(SERVER_CA_CERT,        SERVER_CA_CERT);
    downloadFile(SERVER_CA_KEY,         SERVER_CA_KEY);
    downloadFile(SERVER_WRONG_CN_CERT,  SERVER_WRONG_CN_CERT);
    downloadFile(SERVER_WRONG_CN_KEY,   SERVER_WRONG_CN_KEY);
    downloadFile(CLIENT_CERT_PATH,      CLIENT_CERT_PATH);
    downloadFile(CLIENT_KEY_PATH,       CLIENT_KEY_PATH);
    downloadFile(CLIENT_ENC_KEY_PATH,   CLIENT_ENC_KEY_PATH);
    downloadFile(UNTRUSTED_CA_CERT,     UNTRUSTED_CA_CERT);
    downloadFile(CLIENT_UNTRUSTED_CERT, CLIENT_UNTRUSTED_CERT);
    downloadFile(CLIENT_UNTRUSTED_KEY,  CLIENT_UNTRUSTED_KEY);
    printf("[cert-gen] Certificate download complete.\n");
}
