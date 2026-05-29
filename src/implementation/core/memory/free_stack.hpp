#if !defined(__BN3MONKEY_FREE_STACK__)
#define __BN3MONKEY_FREE_STACK__

#include <cstdlib>
#include <cassert>

namespace Bn3Monkey
{
    struct FreeStackView {
        size_t index_size;   // sizeof(ArenaIndex) — 포맷 검증용
        size_t capacity;
        size_t head;         // _head.next (freelist 시작 인덱스)
        void*  data;         // non-owning
    };

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

        bool isAllocated(size_t index) {
            return _data[index].next == ALLOCATED;
        }

        bool empty() const {
            return _head.next == IS_STACK_EMPTY;
        }

        // 내보내기: 현재 freelist 상태를 가리키는 창 (복사 없음)
        FreeStackView readView() const {
            return FreeStackView {
                sizeof(ArenaIndex),
                _capacity,
                _head.next,
                _data
            };
        }

        // 들여오기: capacity 버퍼를 확보하고 head를 세팅한 뒤 그 버퍼를 가리키는 창 반환.
        // freelist를 재구성하지 않으므로(clear() 호출 안 함) 외부가 data를 그대로 복원할 수 있다.
        FreeStackView writeView(size_t capacity, size_t head) {
            if (capacity != _capacity) {
                ArenaIndex* new_data = reinterpret_cast<ArenaIndex*>(
                    std::realloc(_data, capacity * sizeof(ArenaIndex)));
                assert(new_data != nullptr);
                _data = new_data;
                _capacity = capacity;
            }
            _head.next = head;
            return readView();
        }

    private:
        size_t _capacity {0};
        ArenaIndex _head;
        ArenaIndex* _data {nullptr};
    };
}

#endif // __BN3MONKEY_FREE_STACK__
