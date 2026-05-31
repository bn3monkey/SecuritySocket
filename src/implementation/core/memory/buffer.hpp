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
            _length(0),
            _capacity(initial_capacity)
        {
            _data = std::malloc(sizeof(char) * _capacity);
            if (!_data) {
                _capacity = 0;
            }
        }
        ~StagingBuffer() {
            if (_data) {
                std::free(_data);
            }
        }

        void clear() {
            memset(_data, 0, _capacity);
            _length = 0;
        }

        // Read & Write
        size_t& length() {return _length; }
        void* data() { return _data; }
        const void* data() const { return _data; }

        bool canAppend(size_t required_size) {
            return _length + required_size <= _capacity;
        }
        bool grow(size_t required_size) {
            size_t new_capacity = nextPowerOfTwo(_length + required_size);
            void* new_data = std::realloc(_data, new_capacity * sizeof(char));
            if (!new_data) {
                return false;
            }
            _data = new_data;
            _capacity = new_capacity;
            return true;
        }


        size_t capacity() { return _capacity; }

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

        void* _data {nullptr};
        size_t _length {0};
        size_t _capacity {0};
    };
}

#endif // __BN3MONKEY_STAGING_BUFFER__