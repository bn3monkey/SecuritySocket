#include <jni.h>
#include <string>

#include <securitysockettest.hpp>

#include <android/log.h>
#include <thread>
#include <atomic>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <iostream>

#define LOG_TAG "SecuritySocketAndroidTest"
#define LOG_D(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOG_I(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOG_E(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

class StandardIORedirector
{
public:
    explicit StandardIORedirector() {
        // LOG_D("Redirector start");
        pipe(_stdout_pipe);
        dup2(_stdout_pipe[1], STDOUT_FILENO);
        close(_stdout_pipe[1]);

        // LOG_D("Standard output : %d %d", _stdout_pipe[0], _stdout_pipe[1]);

        pipe(_stderr_pipe);
        dup2(_stderr_pipe[1], STDERR_FILENO);
        close(_stderr_pipe[1]);

        // LOG_D("Set Sync with Stdio");
        std::ios::sync_with_stdio(true);

        // LOG_D("Standard error : %d %d", _stderr_pipe[0], _stderr_pipe[1]);

        // LOG_D("Start Thread");
        _is_running = true;
        _log_runner = std::thread {[=]() {
            logRunner(_stdout_pipe[0], _stderr_pipe[0], _is_running);
        }};
    }
    virtual ~StandardIORedirector() {
        // LOG_D("Redirector stop");
        _is_running = false;
        _log_runner.join();

        close(_stdout_pipe[0]);
        close(_stderr_pipe[0]);
    }
private:
    static void logRunner(int stdout_fd, int stderr_fd, std::atomic_bool& is_running)
    {
        struct pollfd fds[2];
        fds[0].fd = stdout_fd;
        fds[0].events = POLLIN;
        fds[1].fd = stderr_fd;
        fds[1].events = POLLIN;

        char buffer[4096] {0};
        const int timeout_ms = 1000;

        while (is_running) {
            int ret = poll(fds, 2, timeout_ms);

            if (ret > 0 )
            {
                if (fds[0].revents & POLLIN)
                {
                    auto count = read(stdout_fd, buffer, sizeof(buffer)-1);
                    if (count > 0) {
                        buffer[count] = '\0';
                        LOG_I("%s", buffer);
                    }
                    else if (count < 0) {
                        LOG_E("stdout reading error : %s", strerror(errno));
                    }
                }

                if (fds[1].revents & POLLIN) {
                    auto count = read(stderr_fd, buffer, sizeof(buffer)-1);
                    if (count > 0) {
                        buffer[count] = '\0';
                        LOG_I("%s", buffer);
                    }
                    else if (count < 0) {
                        LOG_E("stderr reading error : %s", strerror(errno));
                    }
                }
            }
            else if (ret == 0)
            {
                continue;
            }
            else {
                LOG_E("poll Error : %s", strerror(errno));
            }
        }
    }

    int _stdout_pipe[2] {0};
    int _stderr_pipe[2] {0};
    std::atomic_bool _is_running;
    std::thread _log_runner;
};


extern "C"
JNIEXPORT jint JNICALL
Java_com_bn3monkey_securitysocketandroidtest_SecuritySocketAndroidTest_startSecuritySocketTest(
        JNIEnv *env, jobject thiz) {

    StandardIORedirector ioRedirector{};
    int argc = 0;
    char** argv = nullptr;
    return startSecuritySocketTest(argc, argv);
}