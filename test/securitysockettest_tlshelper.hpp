#if !defined(__SECURITY_SOCKET_TEST_TLS_HELPER__)
#define __SECURITY_SOCKET_TEST_TLS_HELPER__

#include <cstdio>
#include <cstdint>
#include <fstream>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include <remote_command_client.hpp>

#include <cstdarg>
#include <vector>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <spawn.h>
extern char** environ;
#endif

// Remote-side relative cert paths (used in openssl s_server commands on the remote machine)
constexpr static const char* SERVER_SELF_CERT      = "certs/server_self.crt";
constexpr static const char* SERVER_SELF_KEY       = "certs/server_self.key";
constexpr static const char* CA_CERT               = "certs/ca.crt";
constexpr static const char* SERVER_CA_CERT        = "certs/server_ca.crt";
constexpr static const char* SERVER_CA_KEY         = "certs/server_ca.key";
constexpr static const char* SERVER_WRONG_CN_CERT  = "certs/server_wrong_cn.crt";
constexpr static const char* SERVER_WRONG_CN_KEY   = "certs/server_wrong_cn.key";
constexpr static const char* CLIENT_CERT_PATH      = "certs/client.crt";
constexpr static const char* CLIENT_KEY_PATH       = "certs/client.key";
constexpr static const char* CLIENT_ENC_KEY_PATH   = "certs/client_enc.key";
constexpr static const char* CLIENT_KEY_PASSWORD   = "test1234";
constexpr static const char* UNTRUSTED_CA_CERT     = "certs/untrusted_ca.crt";
constexpr static const char* CLIENT_UNTRUSTED_CERT = "certs/client_untrusted.crt";
constexpr static const char* CLIENT_UNTRUSTED_KEY  = "certs/client_untrusted.key";

// Returns the working directory used for LOCAL cert files (default: ".").
// Set via startSecuritySocketTest(argc, argv, cwd).
const char* getCWD();

// Builds a local file path by prepending getCWD() to a relative cert path.
// Use this whenever passing a cert path to SocketTLSClientConfiguration or
// to the local destination argument of downloadFile().
inline std::string localCertPath(const char* rel) {
    return std::string(getCWD()) + "/" + rel;
}


inline Bn3Monkey::RemoteCommandClient* getClient() {
    static Bn3Monkey::RemoteCommandClient* client = nullptr;
    if (client == nullptr)
    {
        client = Bn3Monkey::discoverRemoteCommandClient(9000);
        //client = Bn3Monkey::createRemoteCommandClient(9001, 9002, "192.168.0.5");
        Bn3Monkey::onRemoteOutput(client, [](const char* output) {
            printf("[remote] %s\n", output);
            });
		Bn3Monkey::onRemoteError(client, [](const char* error) {
            printf("[remote][error] %s\n", error);
			});
    }
    return client;
}


inline void createLocalDirectory(const char* path)
{
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}





class LocalProcess
{
public:
    static int32_t openProcess(const char* cmd, ...);
    static void closeProcess(int32_t process_id);
};

void createCertificates();

#endif // __SECURITY_SOCKET_TEST_TLS_HELPER__