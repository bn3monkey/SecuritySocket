#if !defined(__BN3MONKEY__ARENA__)
#define __BN3MONKEY__ARENA__

#include <type_traits>
#include <utility>
#include <cstdlib>
#include <cassert>
#include <new>

namespace Bn3Monkey
{
    struct ObjectArenaView {
        size_t object_size;   // sizeof(Object) — 포맷 검증용
        size_t capacity;
        void*  data;          // non-owning
    };


    template<typename Object>
    class ObjectArena
    {
        static_assert(
            std::is_trivially_copyable<Object>::value ||
                std::is_move_constructible<Object>::value,
            "Object must be trivially copyable or move constructible");

        using trivial_tag = typename std::is_trivially_copyable<Object>::type;

    public:
        ObjectArena(size_t initial_capacity) : _capacity(initial_capacity) {
            if (initial_capacity > 0) {
                _data = static_cast<Object*>(
                    std::malloc(initial_capacity * sizeof(Object)));
                assert(_data != nullptr);
            }
        }

        ~ObjectArena() {
            clear();
            std::free(_data);
        }

        ObjectArena(const ObjectArena&) = delete;
        ObjectArena& operator=(const ObjectArena&) = delete;

        void reset(size_t new_capacity) {
            if (new_capacity <= _capacity) return;
            reset_impl(new_capacity, trivial_tag{});
        }

        void clear() {
            deallocateAll(_data, _capacity, trivial_tag{});
        }

        template<typename ...Args>
        Object* allocate(size_t index, Args&&... args) {
            return ::new (_data + index) Object(std::forward<Args>(args)...);
        }
        void deallocate(size_t index) {
            _data[index].~Object();
        }

        Object* get(size_t index) { return _data + index; }
        const Object* get(size_t index) const { return _data + index; }



        ObjectArenaView readView() const {
            static_assert(std::is_trivially_copyable<Object>::value,
                "raw-byte view requires trivially copyable Object");
            return ObjectArenaView {
                sizeof(Object),
                _capacity,
                _data
            };
        }
        ObjectArenaView writeView(size_t capacity) {
            static_assert(std::is_trivially_copyable<Object>::value,
                "raw-byte view requires trivially copyable Object");
            reset(capacity);
            return readView();
        }


    private:
        // trivial: bitwise relocate가 안전하므로 realloc 그대로
        void reset_impl(size_t new_capacity, std::true_type) {
            Object* new_data = static_cast<Object*>(
                std::realloc(_data, new_capacity * sizeof(Object)));
            assert(new_data != nullptr);
            _data = new_data;
            _capacity = new_capacity;
        }

        // movable: malloc + std::move + ~Object() + free
        void reset_impl(size_t new_capacity, std::false_type) {
            Object* new_data = static_cast<Object*>(
                std::malloc(new_capacity * sizeof(Object)));
            assert(new_data != nullptr);
            for (size_t i = 0; i < _capacity; ++i) {
                ::new (new_data + i) Object(std::move(_data[i]));
                _data[i].~Object();
            }
            std::free(_data);
            _data = new_data;
            _capacity = new_capacity;
        }

        static void deallocateAll(Object*, size_t, std::true_type) {
            // trivial dtor: no-op
        }
        static void deallocateAll(Object* data, size_t capacity, std::false_type) {
            for (size_t i = 0; i < capacity; ++i) {
                data[i].~Object();
            }
        }

        
        size_t _capacity {0};
        Object* _data {nullptr};
    };
}

#endif // __KIOTTY_LIGHT_ARENA__
