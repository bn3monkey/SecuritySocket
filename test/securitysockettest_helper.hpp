#if !defined(__SECURITY_SOCKET_TEST_HELPER__)
#define __SECURITY_SOCKET_TEST_HELPER__

#include <mutex>
#include <condition_variable>
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>


inline std::mutex& getPrintMutex() {
    static std::mutex _mtx;
    return _mtx;
}
template<class... Args>
inline void printConcurrent(const char* format, Args&&... args)
{
    {
        std::lock_guard<std::mutex> lock(getPrintMutex());
        printf(format, std::forward<Args>(args)...);
    }
}
template<>
inline void printConcurrent(const char* format)
{
    {
        std::lock_guard<std::mutex> lock(getPrintMutex());
        printf("%s", format);
    }
}

// Records timestamped events from multiple threads into a single timeline,
// then dumps them sorted by absolute time so per-step gaps are easy to read.
//
// Usage:
//   TimeWatch tw;                         // _t0 = now
//   tw.mark("server: write start");       // record an event from any thread
//   tw.markf("client: read[%zu] done", i);
//   tw.dump("test name");                 // sorted timeline + per-step delta
//
// All marks share one steady_clock baseline so cross-thread ordering and
// inter-event gaps are directly comparable.
class TimeWatch
{
public:
    TimeWatch() : _t0(std::chrono::steady_clock::now()) {}

    void mark(const char* label) {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lk(_mtx);
        _marks.push_back(Entry{ now, std::string(label) });
    }

    template <class... Args>
    void markf(const char* fmt, Args... args) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), fmt, args...);
        mark(buf);
    }

    void dump(const char* title) {
        std::vector<Entry> snapshot;
        {
            std::lock_guard<std::mutex> lk(_mtx);
            snapshot = _marks;
        }
        std::sort(snapshot.begin(), snapshot.end(),
            [](const Entry& a, const Entry& b) { return a.t < b.t; });

        std::lock_guard<std::mutex> plk(getPrintMutex());
        std::printf("\n=== TimeWatch: %s ===\n", title);
        std::printf("       abs (ms)     delta (ms)   event\n");
        long long prev_us = 0;
        for (size_t i = 0; i < snapshot.size(); ++i) {
            const auto& m = snapshot[i];
            auto rel_us = std::chrono::duration_cast<std::chrono::microseconds>(
                m.t - _t0).count();
            auto delta_us = (i == 0) ? rel_us : (rel_us - prev_us);
            std::printf("  %12.3f   (+%9.3f)   %s\n",
                rel_us / 1000.0, delta_us / 1000.0, m.label.c_str());
            prev_us = rel_us;
        }
        std::printf("=== end TimeWatch (%zu marks) ===\n\n", snapshot.size());
    }

private:
    struct Entry {
        std::chrono::steady_clock::time_point t;
        std::string label;
    };
    std::chrono::steady_clock::time_point _t0;
    std::mutex _mtx;
    std::vector<Entry> _marks;
};

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