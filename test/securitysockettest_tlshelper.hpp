#if !defined(__SECURITY_SOCKET_TEST_TLS_HELPER__)
#define __SECURITY_SOCKET_TEST_TLS_HELPER__

#include <cstdio>
#include <cstdint>
#include <fstream>

#include <remote_command_client.hpp>

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


inline Bn3Monkey::RemoteCommandClient* getClient() {
    static Bn3Monkey::RemoteCommandClient* client = nullptr;
    if (client == nullptr)
    {
        client = Bn3Monkey::createRemoteCommandContext(9001, 9002);
        Bn3Monkey::onRemoteOutput(client, [](const char* output) {
            printf("[remote] %s", output);
            });
		Bn3Monkey::onRemoteError(client, [](const char* error) {
            printf("[remote][error] %s", error);
			});
    }
    return client;
}

// Returns true if the file at 'path' exists and is accessible.
inline bool directoryExists(const char* path)
{
    return Bn3Monkey::directoryExists(getClient(), path);
}

inline bool createDirectory(const char* path)
{
    return Bn3Monkey::createDirectory(getClient(), path);
}

// Prints a description, runs the shell command, and warns if it fails.
template<typename ...Args>
inline void runCommand(const char* description, const char* fmt, Args... args)
{
    printf("[cert-gen] %s\n", description);
    char cmd[4096]{ 0 };
    std::snprintf(cmd, sizeof(cmd), fmt, args...);

    Bn3Monkey::runCommand(getClient(), cmd);
}


void createCertificates();

#endif // __SECURITY_SOCKET_TEST_TLS_HELPER__