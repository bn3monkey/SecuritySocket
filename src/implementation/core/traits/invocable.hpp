#if !defined(__BN3MONKEY__INVOCABLE__)
#define __BN3MONKEY__INVOCABLE__

#include <type_traits>
#include <utility>

namespace Bn3Monkey
{
    namespace detail
    {
        template<typename... Ts>
        struct make_void { using type = void; };

        template<typename... Ts>
        using void_t = typename make_void<Ts...>::type;
    }

    // Function을 Args...로 호출 가능한지 평가한다.
    // 호출 가능하면 value == true, 아니면 false.
    template<typename Function, typename... Args>
    struct is_invocable
    {
    private:
        template<typename F, typename... A>
        static auto test(int)
            -> decltype(std::declval<F>()(std::declval<A>()...),
                        std::true_type{});

        template<typename, typename...>
        static std::false_type test(...);

    public:
        static constexpr bool value =
            decltype(test<Function, Args...>(0))::value;
    };
}

#endif // __BN3MONKEY__INVOCABLE__
