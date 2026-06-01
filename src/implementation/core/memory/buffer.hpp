#if !defined(__BN3MONKEY_STAGING_BUFFER__)
#define __BN3MONKEY_STAGING_BUFFER__

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <limits>

namespace Bn3Monkey
{
    class StagingBuffer
    {
    public:
        StagingBuffer(size_t initial_capacity) :
            _capacity(initial_capacity)
        {
            _data = static_cast<char*>(std::malloc(sizeof(char) * _capacity));
            if (!_data) {
                _capacity = 0;
            }
        }
        ~StagingBuffer() {
            if (_data) {
                std::free(_data);
            }
        }

        StagingBuffer(const StagingBuffer& other) = delete;
        StagingBuffer& operator=(const StagingBuffer& other) = delete;

        StagingBuffer(StagingBuffer&& other) = delete;
        StagingBuffer& operator=(StagingBuffer&& other) = delete;
        

        bool empty() {
            return _sent == _received;
        }
        void clear() {
            _sent = _received = 0;
        }

        // Read & Write
        void* data() { return _data; }
        const void* data() const { return _data; }
        
        void* head() { return _data + _sent; }
        const void* head() const { return _data + _sent; }

        void* tail() { return _data + _received; }
        const void* tail() const { return _data + _received; }

        size_t sent() const { return _sent; }
        size_t received() const { return _received; }
        size_t pending() const { return _received - _sent;}
        size_t remaining() const { return _capacity - _received; }
        size_t capacity() const { return _capacity; }

        void fill(size_t n) { _received += n;}
        void drain(size_t n) { _sent += n; } 

        bool canAppend(size_t required_size) const {
            return _received + required_size <= _capacity;
        }

        void compact() {
            if (_sent == 0) {
                return;
            }
            auto live = pending();
            if (live > 0) {
                std::memmove(_data, _data + _sent, live);
            }
            _received = live;
            _sent = 0;
        }

        // Guarantee at least `extra` more bytes can be appended at tail(): try
        // in-place compaction first (cheap), then realloc only if still short.
        // Returns false on allocation failure. May invalidate data()/head()/tail().
        bool reserve(size_t extra) {
            if (remaining() >= extra) {
                return true;                  // already room at tail
            }

            if (_sent > 0) {                  // reclaim drained prefix first
                compact();
                if (remaining() >= extra) {
                    return true;
                }
            }

            size_t new_capacity = nextPowerOfTwo(_received + extra);
            char* new_data = static_cast<char*>(std::realloc(_data, new_capacity * sizeof(char)));
            if (!new_data) {
                return false;
            }
            _data = new_data;
            _capacity = new_capacity;
            return true;
        }


    private:
        size_t nextPowerOfTwo(size_t value) {
            if (value <= 1) {
                return 1;
            }

            value -= 1;
            for (size_t shift = 1; shift < std::numeric_limits<size_t>::digits; shift <<= 1) {
                value |= value >> shift;
            }
            return value + 1;
        }

        char* _data {nullptr};
        size_t _received {0};
        size_t _sent {0};
        size_t _capacity {0};
    };
}

#endif // __BN3MONKEY_STAGING_BUFFER__