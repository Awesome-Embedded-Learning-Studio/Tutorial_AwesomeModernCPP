// Wrapper Patterns with std::invoke
// Demonstrates various wrapper utilities using std::invoke

#include <iostream>
#include <functional>
#include <chrono>
#include <type_traits>

// Timing wrapper
template<typename Func, typename... Args>
auto timed_invoke(Func&& func, Args&&... args) {
    auto start = std::chrono::high_resolution_clock::now();

    auto result = std::invoke(std::forward<Func>(func),
                             std::forward<Args>(args)...);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "Call took " << duration.count() << " us" << std::endl;
    return result;
}

// Logging wrapper
template<typename Func, typename... Args>
auto logged_invoke(const char* name, Func&& func, Args&&... args)
    -> decltype(std::invoke(std::forward<Func>(func), std::forward<Args>(args)...))
{
    std::cout << "Calling " << name << "..." << std::endl;
    auto result = std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
    std::cout << "Finished " << name << std::endl;
    return result;
}

// Retry wrapper
template<typename Func, typename Rep, typename Period>
auto retry_with_backoff(Func&& func,
                       unsigned max_attempts,
                       std::chrono::duration<Rep, Period> initial_delay)
    -> decltype(func())
{
    using ResultType = decltype(func());
    auto delay = initial_delay;

    for (unsigned attempt = 0; attempt < max_attempts; ++attempt) {
        auto result = func();

        if (result) {
            return result;
        }

        if (attempt < max_attempts - 1) {
            std::cout << "Attempt " << (attempt + 1) << " failed, retrying in "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(delay).count()
                      << " ms..." << std::endl;
            std::this_thread::sleep_for(delay);
            delay *= 2;
        }
    }

    return ResultType();
}

// Test functions
struct Calculator {
    int add(int a, int b) const {
        volatile int x = a + b;  // Prevent optimization
        return x;
    }
};

int compute(int x, int y) {
    volatile int sum = 0;
    for (int i = 0; i < 1000; ++i) {
        sum += x * y;
    }
    return sum;
}

// Demo
void demo_timed_wrapper() {
    std::cout << "=== Timed Wrapper Demo ===" << std::endl;

    Calculator calc;

    std::cout << "Free function: ";
    timed_invoke(compute, 5, 3);

    std::cout << "Member function: ";
    timed_invoke(&Calculator::add, calc, 10, 20);

    std::cout << "Lambda: ";
    timed_invoke([]() { return compute(7, 11); });
}

void demo_logged_wrapper() {
    std::cout << "\n=== Logged Wrapper Demo ===" << std::endl;

    Calculator calc;

    auto result = logged_invoke("add", &Calculator::add, calc, 5, 3);
    std::cout << "Result: " << result << std::endl;
}

// Perfect forwarding demo
template<typename Func, typename... Args>
auto forward_and_call(Func&& func, Args&&... args)
    -> decltype(std::invoke(std::forward<Func>(func), std::forward<Args>(args)...))
{
    return std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
}

void take_by_value(int x) {
    std::cout << "by value: " << x << std::endl;
}

void take_by_lvalue_ref(int& x) {
    std::cout << "by lvalue ref: " << x << std::endl;
    x *= 2;
}

void demo_perfect_forwarding() {
    std::cout << "\n=== Perfect Forwarding Demo ===" << std::endl;

    int value = 10;

    forward_and_call(take_by_value, value);
    forward_and_call(take_by_lvalue_ref, value);
    std::cout << "After lvalue ref: " << value << std::endl;
}

// Type-safe wrapper with constraints
template<typename Func, typename... Args>
auto safe_call(Func&& func, Args&&... args)
    -> decltype(std::invoke(std::forward<Func>(func), std::forward<Args>(args)...))
{
    using Result = decltype(std::invoke(std::forward<Func>(func), std::forward<Args>(args)...));

    if constexpr (std::is_void_v<Result>) {
        std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
        std::cout << "(void return)" << std::endl;
    } else {
        auto result = std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
        std::cout << "result: " << result << std::endl;
        return result;
    }
}

void demo_safe_call() {
    std::cout << "\n=== Safe Call Demo ===" << std::endl;

    Calculator calc;

    std::cout << "Void lambda: ";
    safe_call([]() { std::cout << "Hello" << std::endl; });

    std::cout << "Returning lambda: ";
    safe_call([]() { return 42; });
}

int main() {
    demo_timed_wrapper();
    demo_logged_wrapper();
    demo_perfect_forwarding();
    demo_safe_call();

    return 0;
}
