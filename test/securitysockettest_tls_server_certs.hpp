#if !defined(__SECURITY_SOCKET_TEST_TLS_SERVER_CERTS__)
#define __SECURITY_SOCKET_TEST_TLS_SERVER_CERTS__

// Self-contained PEM fixture for the server-side TLS tests.
//
// Embedded rather than generated at runtime: the existing TLS test harness
// (securitysockettest_tlshelper) shells out to a *remote* command server to
// produce certs, which is unavailable on a plain developer checkout. These
// tests must run anywhere, so they write these literals to temp files instead.
//
// Generated once with (openssl 3.x):
//   openssl req -x509 -newkey rsa:2048 -nodes -keyout server.key -out server.crt \
//       -days 3650 -subj "/CN=127.0.0.1" -addext "subjectAltName=IP:127.0.0.1"
//   openssl genrsa -out other.key 2048
//   openssl req -x509 -newkey rsa:2048 -nodes -keyout ca.key -out ca.crt \
//       -days 3650 -subj "/CN=SecuritySocket Test CA"
//
// kOtherKeyPem is a *different* keypair: pairing it with kServerCertPem is what
// SSL_CTX_check_private_key() must reject (TLS_SERVER_KEY_MISMATCH).
// Expires 2036; regenerate with the commands above if a test starts failing on date.

namespace Bn3MonkeyTest
{
constexpr const char* kServerCertPem =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDGjCCAgKgAwIBAgIUCmwD2sNpz4dGLMby2feMbp2TUZ4wDQYJKoZIhvcNAQEL\n"
    "BQAwFDESMBAGA1UEAwwJMTI3LjAuMC4xMB4XDTI2MDcwOTA4MzAzMVoXDTM2MDcw\n"
    "NjA4MzAzMVowFDESMBAGA1UEAwwJMTI3LjAuMC4xMIIBIjANBgkqhkiG9w0BAQEF\n"
    "AAOCAQ8AMIIBCgKCAQEArCrHrtrNmUEaz2cyFCZWshXB46e8ErOLvCOnyZO/y0vA\n"
    "JpyNAAtQDZB3cvjwZ0dTXTP9/DJXAV3QrWlyfy7iT1+SUZN6F8yXxHMIzdrY4qns\n"
    "CKkeOmqSrp9Ad4tmBoMqcJ3PdQdBJ6IxPYXLtBJcPgbb5XXBE7ejYY9PlZBa9C2w\n"
    "eCWeXwjdgJNgRM6pFRW4Df93pSqxiSB7OU11V0hL9uV5yfo5Y7OvOmEZXP9RNMDv\n"
    "o89HZE2ETOylQ/GvrROOKdnG0qVPw7QmrYd18MbKo58xpu96NDsr6BiZkXpSXjap\n"
    "bDkkwQhdp4bcCRir06CyQWFZnIY6wZwEuhgFyt8MrwIDAQABo2QwYjAdBgNVHQ4E\n"
    "FgQUnv1//CKkSckhwE53g5Vf3Ym6+3IwHwYDVR0jBBgwFoAUnv1//CKkSckhwE53\n"
    "g5Vf3Ym6+3IwDwYDVR0TAQH/BAUwAwEB/zAPBgNVHREECDAGhwR/AAABMA0GCSqG\n"
    "SIb3DQEBCwUAA4IBAQByZGPw08nrszik583vkjNYMJwpzIr22nxRiepEM6Qmo+kJ\n"
    "aFRfqeHxpzf1+C3x3EK4qXXOBG1bs7x9ZPnTJSBPNB0BIVi+IGBAQiz9+qvGZ1GC\n"
    "KEF9zs9wr6bMontaHxMENf4EZ9gmPToFCvJ7VmfuEdPYO/cypTA7h5NnsgDW1ZYU\n"
    "489WaVU+TPoAbTGhcAVhjgsi74ceJaOX3l2dAcpdSSViPNXz8Ay6/fWYfOdPmRLF\n"
    "LgpuowGlOYZg/WxQAcHncnR5mR0/YROSV+uw9dqLoxZw+Y5zvFtmciS/1F5iV/A+\n"
    "VoQF2TA0RAbTmCRZnb6gDQ+QzzkJUXYoj1b+AHXb\n"
    "-----END CERTIFICATE-----\n"
    ;

constexpr const char* kServerKeyPem =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQCsKseu2s2ZQRrP\n"
    "ZzIUJlayFcHjp7wSs4u8I6fJk7/LS8AmnI0AC1ANkHdy+PBnR1NdM/38MlcBXdCt\n"
    "aXJ/LuJPX5JRk3oXzJfEcwjN2tjiqewIqR46apKun0B3i2YGgypwnc91B0EnojE9\n"
    "hcu0Elw+BtvldcETt6Nhj0+VkFr0LbB4JZ5fCN2Ak2BEzqkVFbgN/3elKrGJIHs5\n"
    "TXVXSEv25XnJ+jljs686YRlc/1E0wO+jz0dkTYRM7KVD8a+tE44p2cbSpU/DtCat\n"
    "h3XwxsqjnzGm73o0OyvoGJmRelJeNqlsOSTBCF2nhtwJGKvToLJBYVmchjrBnAS6\n"
    "GAXK3wyvAgMBAAECggEAC0k/T3rqyRXyVCVKLEr6b5srMNQhPpfu3EBOx/vmXJp/\n"
    "IqjW5L6ub+0yqg1KnnftAupHxz1k5GM016FWzvfmVa4IsU35ziSXJbAleW/UEZZy\n"
    "XCZQo84HsT3MTpSLSs2qEw7X94PTEBjlochVuwdhwkkrJ3F0Mj7dYtBaYbjAVaBW\n"
    "GjMT9pCPsYij7noX0wR0iLjpeokzh4j4QnsC0W73C/AWGbwAqCIGX/BLxKExdzBo\n"
    "v3NaRRJbRp6Co/VVpfw4rQN+dfJRZdQU3E5qrLjbICZoLnXjnyAazUPmJk9latW8\n"
    "BhkIs+WONNmEPRvrEIMpGeg3f016FM55JCVKlNFmrQKBgQDqXvs6tS7X9MsJ0B6o\n"
    "lNDZfqH5nraYf13wyOVVuYvxSYia3hzXkTJORV5KMXavSE7rMXryfkGI+RNWXvb+\n"
    "RFQxLYUsSNdN9oEDRZm6M/g+/HtZPh3n5qzlzYmGJ5ZoDkLvmtWQ/+a1MB6EqRv8\n"
    "JZSH1iHtkxj8GzJKhUjJ3+ZvBQKBgQC8Djr5kYdCJrsw0anG5In6fH0r05/IJzdg\n"
    "EzHpEQ+4ETv51gUGSXtcIfM0ah0FVVZxGo9rfKr2McmFkXzvo8RrKd2O7BQaC49R\n"
    "0mBDO301AGu38R86yYPewY/qRogyHzhKTxiqxPq4mb8s9e5QsNg86DRnaFylffMS\n"
    "Jq5lwIaTIwKBgCTLjqOz6EvNSccbnoSXAIb045cd/MrKTERONfsUa00RX1n4/ww0\n"
    "5nH1YA1D4L86GfHAze2eNsm7WPbZZ+uZbKJf5CWEthCuX6jU14KtQA8bcn4bS3Sp\n"
    "+YEGmM2wD2P8wTN+2oKuOlk92by4FAWtHLmKu77hti12U5nxfPD8rt/FAoGAD1lF\n"
    "/PqgxIhuvNRP/BjJHjWs7bfPEcIJpgDLEQ/AbbCSaZjLPjEfLWzj8cI7biUB8idN\n"
    "z2MUfRWhMhKm1NRUAr8fAzxHg7yvzOjTzIj9dib7o769Ysnxxmub+G4bTaP4ry/3\n"
    "fnCnWgvKa7wC4HkenRICvPHqpeM6xJ99mnZ1RWsCgYBQWWNMCaoohmb5W+xT2IT3\n"
    "XCEVKlXhTJWOlN9G6fLTBCxBcbGtmFxX+uuTKL96dM/xoADq8VERj5bhk2EGAlXo\n"
    "bIFfqqgsnx/9v+3c1RaZQM+DC8PQNRpyskduOa12UjE7ZK44trismWX7S2HOzsM7\n"
    "c5PiN/OANEn/PWFW2sHG/A==\n"
    "-----END PRIVATE KEY-----\n"
    ;

constexpr const char* kOtherKeyPem =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQCN+GN6J8clfvDa\n"
    "HHmYcEsUl1aePOVeGGodsDvBLUl0gTiWwd+UHsD8iTPL8fBHGbut9Xg4J+yXLcRB\n"
    "Z1TGXPVt27q5XKv63PLrj6e49C60YW/cRCzyE6mf46V69K71qTFana2qW5+Em0GZ\n"
    "MTl7VII1aY6THXaj6uQHUGT/5+gjVnLn5a1ZWZDOjBgA860lHQHigsg3inSdk9HW\n"
    "JuwWicnsbDclaUK1pUNiFfTxIbMcKxgAiSGAFGg1zuX8EYCiQvxbYUOzw1yjHosh\n"
    "bk5FJJMcl1/H9UMZD8GmPTBNyvYuiOK10+YTeLr6sv2ox4JjfXOI2VyQAgH2pv64\n"
    "Cd7KfoupAgMBAAECggEACjoLk8Eu9bWWd3u9dZyXMqPxM85gA750pxqqv0W6Hh1F\n"
    "3ImZh65hyVSVKYU41qz/nu5ztcXdzjPC3fw/Itfk6mroNpHlUJmfGmwQ0VWuUe+X\n"
    "6CZ1ag2+YRERZVFKvicCh2Fxi7f0aSVpnQsAS0ZL5+Q98T0vg+t3zXVo2ndT5UxU\n"
    "LXk8FBq2j04xgs9FdDWwm/yefi1AgBViAxp4cby/W6owDZQA8oBRXechKmdpMEmF\n"
    "p4z7e0IXGyZ7FqBzXoqSza444SumF2IhzqGR281Q+FE7TY2jIBU2L/O/KojIIEBJ\n"
    "75li0HpEdvayx1oojmMJP3h+JwEjuTfsxmApT1heNQKBgQDFw0+QHOjSKnsX0KSp\n"
    "LdkIlCZ9Ndwbt23Bqcov0qX+0eybg2mNCCCTMQW/PTMKZExUJLiXOBfPqG/gKJ+o\n"
    "9QaAK3VjGI8fE7mPtPAOFaDgBK5IDJFos3ZDEZltMcilda4puP3FzdVQwtxL98ck\n"
    "GVaDW8AoE4cM8NY/8DUL2MrOBQKBgQC3xw3z17ri6JYVSYpZDZ8qfBV5VPEmSGHc\n"
    "bsOL+DJqwHh0zjs0P0+GrHKNAZE4qVXDq1+oCLyAso6W2qZ0ykX16sD3mYao9FkZ\n"
    "xHlFfkPKytNGAhPvj3wrlQui2OpQ2Moee83ycJKMi3A80itFa3JkVYLudipF/tf/\n"
    "mM6ARBzUVQKBgGiekEKe6JP2ITRSDinptT0SkuH+UjjfatLe7bUq7OHHUDJc7MPN\n"
    "Ht5/OpkW2R+QO8jvvC9TX1VduAGPvBb3uL6pPupznZNFLg9WGwB4dKjOERzQeUQ4\n"
    "XEHS16Wqhojxnnc4BBhWcZjN3RjbNaBlPx0yto789Z7k8ZLVklp4D6G5AoGAd5qY\n"
    "vgW8n0h3xMrjuyleWSwAXcKmXx/kcK21njaduVQiEQLXDR3XB/LbamGGvbWXF41E\n"
    "5/snkyqiGhObGY87EN0DUSEdvn+oLVjtBAk03Zo6/ekESHi9ey+5Lva1KICuqkAw\n"
    "vIP/HHDzTuIJP1i6TgpI+CD3Si6dLL+qRVgO2zECgYAZp8q/elbbeI3iUZHmb1ks\n"
    "Li1qIntvYrvxpFWPJ+ekP62J92QVSMQb+NIBgOHhGLrc8CyZuyRqCArBC6NmcsNP\n"
    "T6isIhCKEy7mT3Blle+cP7539cQ1QrXTdI42DQa0abzDjBL4Fut52anH6dHMCx2E\n"
    "0vkJ9YIKpuRFT0YI2CnDaw==\n"
    "-----END PRIVATE KEY-----\n"
    ;

constexpr const char* kCaCertPem =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDIzCCAgugAwIBAgIUH6XbcYTAs06iD3FnXzFpcnfFpRYwDQYJKoZIhvcNAQEL\n"
    "BQAwITEfMB0GA1UEAwwWU2VjdXJpdHlTb2NrZXQgVGVzdCBDQTAeFw0yNjA3MDkw\n"
    "ODMwMzJaFw0zNjA3MDYwODMwMzJaMCExHzAdBgNVBAMMFlNlY3VyaXR5U29ja2V0\n"
    "IFRlc3QgQ0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCunO0fYIbl\n"
    "RStnfpE1tNv6kEweIC/DyH6U7NgA/6urp96srwseE8T64/UKqPlLp+/1kZQnXc/A\n"
    "8lpnCFCYVg2hyH538MCxtMD17cMTMG0H4hDbcxhYDOpYc/OZhU9U//UWPBuDEceK\n"
    "HIbgz9ZGYGEVA6dG9Dh/oVflZpxECakwWr9y8Z4SRm3oJWxYURSS6vNdeZxADzsd\n"
    "Sm2cD0Z6bYvYLWag7eEw+XhvgaPgum+9yrvTJA0yaHX/R/vqsJo/ri/plITc8EAY\n"
    "3TIFttfOVR5NMP8I/tydH2C+W8Xacx5YyXXJx3BB1vRsT5GCfi76YrrEsDlsUkFx\n"
    "TmyPuYPaler9AgMBAAGjUzBRMB0GA1UdDgQWBBTNR1XtCCn9SaR8W+dn6gANjxaE\n"
    "gTAfBgNVHSMEGDAWgBTNR1XtCCn9SaR8W+dn6gANjxaEgTAPBgNVHRMBAf8EBTAD\n"
    "AQH/MA0GCSqGSIb3DQEBCwUAA4IBAQApi+dK090cM+aa056MkS7I/At0HI7iD9iB\n"
    "TpN78Lb/IyqY1cvhyKaOx+Z5l0+OfFgQ1hIj/mZ0lAt6Ly72UidozMNgNx8Kf0zt\n"
    "o4o7cCfYiLgzp4vyovQqID5/Yw+GSxZU/DRJv5oKYEyc0mOCnj6fH19Z890pFQsz\n"
    "t87ERvuoQrmecvQ0yRTrgFbK48ZcIyuemcbJGDDWG5aVUfu7SLluwdeTZZ46l98R\n"
    "v2ASzX4kWaA+XMkFscyuJw7VALmRloZDEyYePO8teDkkDo+aW3ALKROrqaf06NMS\n"
    "/Unpmdp+TGU4UVWLnA/PggmKQi8gVDeizYxiTexltVUnSfM8kcne\n"
    "-----END CERTIFICATE-----\n"
    ;

}

#endif // __SECURITY_SOCKET_TEST_TLS_SERVER_CERTS__
