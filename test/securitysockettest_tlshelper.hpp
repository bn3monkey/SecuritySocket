#if !defined(__SECURITY_SOCKET_TEST_TLS_HELPER__)
#define __SECURITY_SOCKET_TEST_TLS_HELPER__

#include <cstdio>
#include <cstdint>
#include <fstream>

#if defined _WIN32
#include <windows.h>
#include <direct.h>   // _mkdir
#include <sys/stat.h> // _stat
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h> // stat, mkdir
#endif

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

// Returns true if the file at 'path' exists and is accessible.
inline bool fileExists(const char* path)
{
#ifdef _WIN32
    struct _stat st;
    return _stat(path, &st) == 0;
#else
    struct stat st;
    return stat(path, &st) == 0;
#endif
}

inline void createDirectory(const char* path)
{
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

// Prints a description, runs the shell command, and warns if it fails.
template<typename ...Args>
inline int32_t runCommand(const char* description, const char* fmt, Args... args)
{
    printf("[cert-gen] %s\n", description);
    char cmd[4096]{ 0 };
    std::snprintf(cmd, sizeof(cmd), fmt, args...);

    int32_t ret = -1;

#ifdef _WIN32

    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    if (CreateProcessA(
            nullptr,
            cmd,
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &si,
            &pi))
    {
        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        ret = static_cast<int>(exitCode);

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    else
    {
        ret = -1;
    }

#else

    pid_t pid = fork();

    if (pid == 0)
    {
        execl("/bin/sh", "sh", "-c", cmd, (char*)nullptr);
        _exit(127);
    }
    else if (pid > 0)
    {
        int status = 0;
        waitpid(pid, &status, 0);

        if (WIFEXITED(status))
            ret = WEXITSTATUS(status);
        else
            ret = -1;
    }
    else
    {
        ret = -1;
    }

#endif

    if (ret != 0)
        std::printf("[cert-gen] WARNING: command exited with code %d\n", ret);

    return ret;
}


void createCertificates();

#endif // __SECURITY_SOCKET_TEST_TLS_HELPER__