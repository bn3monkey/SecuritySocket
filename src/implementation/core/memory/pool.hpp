#if !defined(__BN3MONKEY_POOL__)
#define __BN3MONKEY_POOL__

#include "arena.hpp"
#include "free_stack.hpp"

namespace Bn3Monkey
{
    struct ObjectPoolView {
        size_t          capacity;
        ObjectArenaView arena;
        FreeStackView   free_stack;
    };

    template<typename Object>
    class ObjectPool
    {
    public:

        ObjectPool(size_t initial_capacity) :
            _capacity(initial_capacity),
            _arena(initial_capacity),
            _free_stack(initial_capacity)
        {

        }

        ObjectPool(const ObjectPool&) = delete;
        ObjectPool& operator=(const ObjectPool&) = delete;

        template<typename ...Args>
        size_t allocate(Args&&... args) {
            if (_free_stack.empty()) {
                _capacity = _capacity ? _capacity << 1 : 1;   // 0에서 시작해도 자라게
                _arena.reset(_capacity);
                _free_stack.reset(_capacity);
            }

            size_t free_index = _free_stack.pop();
            _arena.allocate(free_index, std::forward<Args>(args)...);
            ++_count;
            return free_index;
        }

        void deallocate(size_t index) {
            _arena.deallocate(index);
            _free_stack.push(index);
            --_count;
        }

        void clear() {
            _arena.clear();
            _free_stack.clear();
            _count = 0;
        }

        Object* get(size_t index) { return _arena.get(index); }
        const Object* get(size_t index) const { return _arena.get(index); }

        bool isAllocated(size_t index) {
            return _free_stack.isAllocated(index);
        }

        size_t capacity() const { return _capacity; }
        size_t size() const { return _count; }
        bool empty() const { return _count == 0; }
        bool full() const { return _free_stack.empty(); }

        // 내보내기: pool 전체 상태를 가리키는 창 (arena + freestack 합성)
        ObjectPoolView readView() const {
            return ObjectPoolView {
                _capacity,
                _arena.readView(),
                _free_stack.readView()
            };
        }

        // 들여오기: capacity/head를 세팅하고 하위 버퍼들을 확보한 뒤 채울 창 반환.
        // 외부는 view.arena.data / view.free_stack.data 를 각각 복원하면 된다.
        ObjectPoolView writeView(size_t capacity, size_t head) {
            _capacity = capacity;
            return ObjectPoolView {
                capacity,
                _arena.writeView(capacity),
                _free_stack.writeView(capacity, head)
            };
        }

    private:
        size_t _capacity {0};
        size_t _count {0};
        ObjectArena<Object> _arena;
        FreeStack _free_stack;
    };
}

#endif // __BN3MONKEY_POOL__