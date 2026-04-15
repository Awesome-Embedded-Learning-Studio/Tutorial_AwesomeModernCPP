// std::invoke_result and Compile-Time Type Deduction
// Demonstrates compile-time return type deduction using std::invoke_result_t

#include <iostream>
#include <type_traits>
#include <functional>
#include <string>

struct Multiplier {
    double operator()(int x, int y) const {
        return static_cast<double>(x) * y;
    }
};

int add(int a, int b) {
    return a + b;
}

void demo_invoke_result() {
    std::cout << "=== std::invoke_result Demo ===" << std::endl;

    // Deduce return type at compile time
    using Result1 = std::invoke_result_t<decltype(&Multiplier::operator()), Multiplier, int, int>;
    static_assert(std::is_same_v<Result1, double>);
    std::cout << "Multiplier::operator() returns double" << std::endl;

    using Result2 = std::invoke_result_t<decltype(add), int, int>;
    static_assert(std::is_same_v<Result2, int>);
    std::cout << "add returns int" << std::endl;

    using Result3 = std::invoke_result_t<decltype([]() { return 42; })>;
    static_assert(std::is_same_v<Result3, int>);
    std::cout << "Lambda returns int" << std::endl;
}

// Generic function with compile-time return type handling
template<typename Func, typename... Args>
auto call_and_print(Func&& func, Args&&... args)
    -> std::invoke_result_t<Func, Args...>
{
    using Ret = std::invoke_result_t<Func, Args...>;

    if constexpr (std::is_void_v<Ret>) {
        std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
        std::cout << "(void return)" << std::endl;
    } else {
        Ret result = std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
        std::cout << "result: " << result << std::endl;
        return result;
    }
}

void demo_generic_handler() {
    std::cout << "\n=== Generic Handler Demo ===" << std::endl;

    Multiplier mult;

    std::cout << "Calling Multiplier: ";
    call_and_print(mult, 3, 4);

    std::cout << "Calling add: ";
    call_and_print(add, 10, 20);

    std::cout << "Calling void lambda: ";
    call_and_print([]() { std::cout << "Side effect!"; });
}

// Compile-time constraints
template<typename Func, typename... Args>
auto safe_invoke(Func&& func, Args&&... args)
    -> std::enable_if_t<std::is_invocable_v<Func, Args...>,
                       std::invoke_result_t<Func, Args...>>
{
    return std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
}

// SFINAE example
template<typename Func, typename... Args>
auto invoke_or_error(Func&& func, Args&&... args)
    -> std::enable_if_t<std::is_invocable_v<Func, Args...>, bool>
{
    using Ret = std::invoke_result_t<Func, Args...>;
    if constexpr (!std::is_void_v<Ret>) {
        auto result = std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
        std::cout << "Result: " << result << std::endl;
    }
    return true;
}

template<typename Func, typename... Args>
auto invoke_or_error(Func&&, Args&&...)
    -> std::enable_if_t<!std::is_invocable_v<Func, Args...>, bool>
{
    std::cout << "Error: arguments not callable with given types" << std::endl;
    return false;
}

void demo_sfinae() {
    std::cout << "\n=== SFINAE Demo ===" << std::endl;

    auto valid_func = [](int x) { return x * 2; };

    std::cout << "Valid call: ";
    invoke_or_error(valid_func, 21);

    std::cout << "Invalid call: ";
    invoke_or_error(valid_func, "not an int");
}

// Type checking at compile time
template<typename Func, typename... Args>
constexpr bool is_callable_with_return_v =
    std::is_invocable_r_v<int, Func, Args...>;

void demo_type_checking() {
    std::cout << "\n=== Type Checking Demo ===" << std::endl;

    auto returns_int = [](int x) { return x; };
    auto returns_double = [](int x) { return 1.0 * x; };

    std::cout << "Lambda returning int is callable -> int: "
              << is_callable_with_return_v<decltype(returns_int), int> << std::endl;

    std::cout << "Lambda returning double is callable -> int: "
              << is_callable_with_return_v<decltype(returns_double), int> << std::endl;
}

int main() {
    demo_invoke_result();
    demo_generic_handler();
    demo_sfinae();
    demo_type_checking();

    return 0;
}
