#ifndef __BN3MONKEY_FIXED_POOL__
#define __BN3MONKEY_FIXED_POOL__

#include <vector>
#include <queue>

namespace Bn3Monkey
{
    // Fixed-size placement-new pool: acquire() constructs an object into a free
    // slot and hands back a pointer, release() destroys it and recycles the
    // slot. Named distinctly from core/memory/pool.hpp's index-based ObjectPool
    // so both can be included in one translation unit (RequestServer uses this
    // for its connection pool; the SegmentTrie uses the index-based one).
    template<typename ObjectType>
    class FixedObjectPool
    {
    public:
        FixedObjectPool(size_t initial_size) : _objects(initial_size, std::allocator<Container>())
        {
            for (auto& object : _objects)
            {
                _availables.push(&object);
            }
        }

        template<class ...Args>
        ObjectType* acquire(Args&&... args)
        {
            if (_availables.empty())
            {
                return nullptr;
            }

            auto* ptr = _availables.front();
            _availables.pop();

            auto* new_ptr = new (ptr) ObjectType(std::forward<Args>(args)...);
            return new_ptr;
        }

        void release(ObjectType* object)
        {
            auto* pos = static_cast<ObjectType*>(object);
            auto* start_ptr = reinterpret_cast<ObjectType*>(_objects.data());
            auto* end_ptr = reinterpret_cast<ObjectType*>(_objects.data()) + _objects.size();


            if (start_ptr<=pos && pos < end_ptr)
            {
                object->~ObjectType();
                auto* ptr = reinterpret_cast<Container*>(object);
                _availables.push(ptr);
            }
        }

    private:
        class Container
        {
            char buffer[sizeof(ObjectType)]{ 0 };
        };

        std::vector<Container> _objects;
        std::queue<Container*> _availables;

    };
}
#endif // __BN3MONKEY_FIXED_POOL__