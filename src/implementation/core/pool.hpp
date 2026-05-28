#if !defined(__KIOTTY_LIGHT_NODE_POOL__)
#define __KIOTTY_LIGHT_NODE_POOL__

#include "arena.hpp"
#include "free_stack.hpp"

namespace KiottyLight
{
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

        template<typename ...Args>
        size_t allocate(Args&&... args) {
            size_t free_index = _free_stack.pop();
            if (free_index == FreeStack::IS_STACK_EMPTY)
                return FreeStack::IS_STACK_EMPTY;
        }

        Object& 


    private:
        size_t _capacity {0};
        ObjectArena<Object> _arena;
        FreeStack _free_stack;
    }
}

#endif // __KIOTTY_LIGHT_NODE_POOL__