#ifdef __ANDROID__
#include <jni.h>
#include <android/log.h>
#endif

#include <SecuritySocket.hpp>
#include <string>
#include <queue>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>

#ifdef __ANDROID__
#define LOG_D(...) __android_log_print(ANDROID_LOG_DEBUG, "SECURITYSOCKET", __VA_ARGS__)
#define LOG_E(...) __android_log_print(ANDROID_LOG_ERROR, "SECURITYSOCKET", __VA_ARGS__)
#else
#define LOG_D(...) printf(__VA_ARGS__)
#define LOG_E(...) printf(__VA_ARGS__)
#endif

using namespace Bn3Monkey;

class TCPStreamHandler : public TCPEventHandler
{
public:
    TCPStreamHandler(size_t size) {

    }
    void onConnected() override {
        LOG_D("Socket connected!\n");
        _is_connected = true;
    }
    void onDisconnected() override {
        LOG_D("Socket Disconnected!\n");

        _is_connected = false;
        _read_cv.notify_all();
        _write_cv.notify_all();
    }

    void read(char* buffer, size_t size) {
        {
            std::unique_lock<std::mutex> lock(_read_mtx);
            _read_cv.wait(
                lock, [&]() {
                    return !(_is_connected && _read_buffer.empty());
                }
            );
            if (!_is_connected)
                return;

            auto src = _read_buffer.front();
            _read_buffer.pop();

            size_t new_size = src.size() < size ? src.size() : size;
            ::memcpy(buffer, src.data(), new_size);
        }
    }
    void onRead(char* buffer, size_t size) override {
        {
            std::lock_guard<std::mutex> lock(test_mtx);
            LOG_D("onRead (%d) : %s\n", size, buffer);
        }
        {
            std::lock_guard<std::mutex> lock(_read_mtx);
            _read_buffer.push(std::vector<char>(buffer, buffer + size));
        }
        _read_cv.notify_all();
    }

    void write(char* buffer, size_t size) {
        {
            std::lock_guard<std::mutex> lock(_write_mtx);
            _write_buffer.push(std::vector<char>(buffer, buffer + size));
        }
        _write_cv.notify_all();
    }
    size_t onWrite(char* buffer, size_t size) override {
        size_t new_size{ 0 };
        {
            std::lock_guard<std::mutex> lock(test_mtx);
        }

        {
            std::unique_lock<std::mutex> lock(_write_mtx);
            _write_cv.wait(
                lock, [&]() {
                    return !(_is_connected && _write_buffer.empty());
                }
            );

            if (!_is_connected)
                return 0;

            auto src = _write_buffer.front();
            _write_buffer.pop();

            new_size = src.size() < size ? src.size() : size;
            ::memcpy(buffer, src.data(), new_size);

            LOG_D("onWrite (%d / %d) : %s\n", new_size, size, buffer);
        }
        return new_size;
    }

    void onError(const ConnectionResult& result) override {
        LOG_E("result : %s\n", result.message.c_str());
    }

private:
    std::mutex test_mtx;

    bool _is_connected{ false };
    std::queue<std::vector<char>> _read_buffer;
    std::mutex _read_mtx;
    std::condition_variable _read_cv;

    std::queue<std::vector<char>> _write_buffer;
    std::mutex _write_mtx;
    std::condition_variable _write_cv;
};

void echo(TCPStreamHandler* handler, const char* message) {
    char buffer[1024]{ 0 };
    size_t size = strlen(message);
    handler->write((char*)message, size);
    handler->read(buffer, 1024);
    LOG_D("READ VALUE : %s\n", buffer);
}

void tlsTest() {
    TCPConfiguration config{
            "192.168.0.69",
            "443",
            true,
            3,
            2000
    };

    auto* handler = new TCPStreamHandler(32768);

    {
        TCPClient client{ config, *handler };
        auto result = client.getLastError();
        if (result.code != ConnectionCode::SUCCESS)
        {
            LOG_E("error : %s\n", result.message.c_str());
            return;
        }

        char buffer[512]{ 0 };



        std::string big_sentense;
        for (size_t i = 0; i < 100; i++)
        {
            big_sentense += "MONKEY";
        }

        echo(handler, big_sentense.c_str());

        std::string small_sentense;
        small_sentense = "do you know picakchu?";
        handler->write((char*)small_sentense.c_str(), small_sentense.size());
        small_sentense = "do you know raichu?";
        handler->write((char*)small_sentense.c_str(), small_sentense.size());
        small_sentense = "Do you know kimchi?";
        handler->write((char*)small_sentense.c_str(), small_sentense.size());
        small_sentense = "Do you know sans?";
        handler->write((char*)small_sentense.c_str(), small_sentense.size());
        small_sentense = "Do you know papyrus?";
        handler->write((char*)small_sentense.c_str(), small_sentense.size());

        std::this_thread::sleep_for(std::chrono::seconds(20));

        memset(buffer, 0, 512);
        handler->read(buffer, 512);
        LOG_D("READ_VALUE : %s\n", buffer);

        memset(buffer, 0, 512);
        handler->read(buffer, 512);
        LOG_D("READ_VALUE : %s\n", buffer);

        memset(buffer, 0, 512);
        handler->read(buffer, 512);
        LOG_D("READ_VALUE : %s\n", buffer);

        memset(buffer, 0, 512);
        handler->read(buffer, 512);
        LOG_D("READ_VALUE : %s\n", buffer);

        memset(buffer, 0, 512);
        handler->read(buffer, 512);
        LOG_D("READ_VALUE : %s\n", buffer);

    }
}

void tcpTest() {
    TCPConfiguration config{
            "192.168.0.69",
            "3000",
            false,
            3,
            2000
    };

    auto* handler = new TCPStreamHandler(32768);

    {
        TCPClient client{ config, *handler };
        auto result = client.getLastError();
        if (result.code != ConnectionCode::SUCCESS)
        {
            LOG_E("error : %s\n", result.message.c_str());
            return;
        }

        char buffer[512]{ 0 };

        LOG_D("Send : Do you know kimchi?\n");
        echo(handler, "Do you know kimchi?");
        result = client.getLastError();
        if (result.code != ConnectionCode::SUCCESS)
        {
            LOG_E("error : %s\n", result.message.c_str());
            return;
        }
        LOG_D("Send : Do you know sans?\n");
        echo(handler, "Do you know sans?");
        result = client.getLastError();
        if (result.code != ConnectionCode::SUCCESS)
        {
            LOG_E("error : %s\n", result.message.c_str());
            return;
        }
        LOG_D("Send : Do you know papyrus?\n");
        echo(handler, "Do you know papyrus?");
        result = client.getLastError();
        if (result.code != ConnectionCode::SUCCESS)
        {
            LOG_E("error : %s\n", result.message.c_str());
            return;
        }

        std::string big_sentense;
        for (size_t i = 0; i < 100; i++)
        {
            big_sentense += "MONKEY";
        }

        LOG_D("Send : MONKEY\n");
        echo(handler, big_sentense.c_str());
        result = client.getLastError();
        if (result.code != ConnectionCode::SUCCESS)
        {
            LOG_E("error : %s\n", result.message.c_str());
            return;
        }

        std::string small_sentense;
        small_sentense = "do you know picakchu?";
        LOG_D("Send : %s\n", small_sentense.c_str());
        handler->write((char*)small_sentense.c_str(), small_sentense.size());
        result = client.getLastError();
        if (result.code != ConnectionCode::SUCCESS)
        {
            LOG_E("error : %s\n", result.message.c_str());
            return;
        }

        small_sentense = "do you know raichu?";
        LOG_D("Send : %s\n", small_sentense.c_str());
        handler->write((char*)small_sentense.c_str(), small_sentense.size());
        result = client.getLastError();
        if (result.code != ConnectionCode::SUCCESS)
        {
            LOG_E("error : %s\n", result.message.c_str());
            return;
        }

        memset(buffer, 0, 512);
        handler->read(buffer, 512);
        result = client.getLastError();
        if (result.code != ConnectionCode::SUCCESS) {
            LOG_E("error : %s\n", result.message.c_str());
            return;
        }
        LOG_D("READ_VALUE : %s\n", buffer);
        memset(buffer, 0, 512);

        handler->read(buffer, 512);
        result = client.getLastError();
        if (result.code != ConnectionCode::SUCCESS)
        {
            LOG_E("error : %s\n", result.message.c_str());
            return;
        }
        LOG_D("READ_VALUE : %s\n", buffer);

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

int main() {
    tcpTest();
    // tlsTest();
}