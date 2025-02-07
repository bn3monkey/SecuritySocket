#ifndef __BN3MONKEY_OBJECT_POOL__
#define __BN3MONKEY_OBJECT_POOL__

#include <vector>
#include <queue>

namespace Bn3Monkey
{
    template<typename ObjectType>
    class ObjectPool
    {
    public:
        ObjectPool(size_t initial_size) : _objects(initial_size, std::allocator<Container>())
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

        void relelase(ObjectType* object)
        {
            auto* pos = static_cast<ObjectType*>(object);
            auto* start_ptr = reinterpret_cast<ObjectType*>(_objects.data());
            auto* end_ptr = reinterpret_cast<ObjectType*>(_objects.data()) + _objects.size() -1;


            if (start_ptr<=pos && pos <= end_ptr)
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
#endif // __BN3MONKEY_OBJECT_POOL__