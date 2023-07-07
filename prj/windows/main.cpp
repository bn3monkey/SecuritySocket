#include <SecuritySocket.hpp>
#include <cstring>
#include <vector>
#include <queue>

using namespace Bn3Monkey;

class TCPStreamHandler : public TCPEventHandler
{
public:
    TCPStreamHandler(size_t size) {

    }
    void onConnected() override {
        printf("Socket Connected!");
        _is_connected = true;
    }
    void onDisconnected() override {
        printf("Socket Disconnected!");

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
    void onWrite(char* buffer, size_t size) override {
        {
            std::unique_lock<std::mutex> lock(_write_mtx);
            _write_cv.wait(
                lock, [&]() {
                    return !(_is_connected  && _write_buffer.empty());
                }
            );

            if (!_is_connected)
                return;

            auto src = _write_buffer.front();
            _write_buffer.pop();

            size_t new_size = src.size() < size ? src.size() : size;
            ::memcpy(buffer, src.data(), new_size);
        }
    }

    void onError(const ConnectionResult& result) override {
        printf("result : %s\n", result.message.c_str());
    }

private:
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
	handler->write((char *)message, size);
	handler->read(buffer, 1024);
	printf("%s\n", buffer);
}
int main() {

	TCPConfiguration config {
		"127.0.0.1",
		"4444",
		false,
		3,
		2000
	};

	auto* handler = new TCPStreamHandler(32768);

	{
		TCPClient client{config, *handler};

		char buffer[512]{ 0 };

		echo(handler, "Do you know kimchi?");
		echo(handler, "Do you know sans?");
		echo(handler, "Do you know papyrus?");

		std::string big_sentense;
		for (size_t i = 0; i < 100; i++)
		{
			big_sentense += "MONKEY";
		}

		echo(handler, big_sentense.c_str());

		std::string small_sentense;
		small_sentense = "do you know picakchu?";
		handler->write((char *)small_sentense.c_str(), small_sentense.size());
		small_sentense = "do you know raichu?";
		handler->write((char*)small_sentense.c_str(), small_sentense.size());

        memset(buffer, 0, 512);
		handler->read(buffer, 512);
		printf("%s\n", buffer);
        memset(buffer, 0, 512);
		handler->read(buffer, 512);
		printf("%s\n", buffer);

	}

	printf("³¡?");
	return 0;
}