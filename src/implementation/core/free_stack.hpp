#if !defined(__KIOTTY_LIGHT_FREE_STACK__)
#define __KIOTTY_LIGHT_FREE_STACK__

#include <cstdlib>
#include <cassert>

namespace KiottyLight
{

    class FreeStack
    {
        
        constexpr static size_t ALLOCATED {static_cast<size_t>(-1)};
        static constexpr size_t IS_STACK_EMPTY  {static_cast<size_t>(-2)};
        struct ArenaIndex {
            size_t next { IS_STACK_EMPTY };
        };

    public:
        FreeStack(size_t initial_capacity) : _capacity(initial_capacity) {
            if (initial_capacity > 0) {
                _data = reinterpret_cast<ArenaIndex*>(
                    std::malloc(initial_capacity * sizeof(ArenaIndex)));
                assert(_data != nullptr);
            }
            clear();
        }

        ~FreeStack() {
            std::free(_data);
        }

        FreeStack(const FreeStack&) = delete;
        FreeStack& operator=(const FreeStack&) = delete;

        void clear() {
            if (_capacity == 0) {
                _head.next = IS_STACK_EMPTY;
                return;
            }
            for (size_t i = 0; i + 1 < _capacity; ++i) {
                _data[i].next = i + 1;
            }
            _data[_capacity - 1].next = IS_STACK_EMPTY;
            _head.next = 0;
        }

        void reset(size_t new_capacity) {
            if (new_capacity <= _capacity) return;

            ArenaIndex* new_data = reinterpret_cast<ArenaIndex*>(
                std::realloc(_data, new_capacity * sizeof(ArenaIndex)));
            assert(new_data != nullptr);
            _data = new_data;

            // [_capacity, new_capacity) 가 새로 생긴 슬롯
            // 이들을 free list 앞쪽에 LIFO push
            //   _capacity → _capacity+1 → ... → new_capacity-1 → (기존 _head.next)
            for (size_t i = _capacity; i + 1 < new_capacity; ++i) {
                _data[i].next = i + 1;
            }
            _data[new_capacity - 1].next = _head.next;
            _head.next = _capacity;

            _capacity = new_capacity;
        }

        size_t pop() {
            size_t index = _head.next;
            _head.next = _data[index].next;
            _data[index].next = ALLOCATED;
            return index;
        }

        void push(size_t index) {
            assert(index < _capacity);
            assert(_data[index].next == ALLOCATED);
            _data[index].next = _head.next;
            _head.next = index;
        }

    private:
        ArenaIndex _head;
        ArenaIndex* _data {nullptr};
        size_t _capacity {0};
    };
}

#endif // __KIOTTY_LIGHT_FREE_STACK__
