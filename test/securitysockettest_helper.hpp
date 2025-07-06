#if !defined(__SECURITY_SOCKET_TEST_HELPER__)
#define __SECURITY_SOCKET_TEST_HELPER__

#include <mutex>
#include <condition_variable>

class SimpleEvent
{
public:
    void wake()
    {
        {
            std::unique_lock<std::mutex> lock(_mtx);
            _is_sleeping = false;
            _cv.notify_all();
        }
    }

    void sleep()
    {
        {
            std::unique_lock<std::mutex> lock(_mtx);
            _is_sleeping = true;
            _cv.wait(lock, [&](){
                return !_is_sleeping;
            });
        }
    }
private:
    bool _is_sleeping {false};
    std::mutex _mtx;
    std::condition_variable _cv;
};

#endif // __SECURITY_SOCKET_TEST_HELPER__