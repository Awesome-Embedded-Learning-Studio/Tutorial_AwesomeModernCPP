// Fast Callback: Template-Based Zero Overhead Callbacks
// Demonstrates compile-time polymorphism for maximum performance

#include <iostream>
#include <functional>

// Fast callback: direct function pointer storage
template<typename R, typename... Args>
struct FastCallback {
    using FnPtr = R(*)(Args...);
    FnPtr ptr = nullptr;

    FastCallback() = default;
    FastCallback(FnPtr p) : ptr(p) {}

    R operator()(Args... args) const {
        return ptr(args...);
    }

    bool empty() const { return ptr == nullptr; }
};

// Alternative: Template-based callback (type specified at compile time)
template<typename T>
struct CallbackBox {
    T callable;

    CallbackBox(T&& c) : callable(std::forward<T>(c)) {}

    template<typename... Args>
    auto operator()(Args&&... args) -> decltype(callable(args...)) {
        return callable(std::forward<Args>(args)...);
    }
};

// Example functions
int add(int a, int b) {
    return a + b;
}

void print_message(const char* msg) {
    std::cout << "Message: " << msg << std::endl;
}

// Usage examples
void demo_fast_callback() {
    std::cout << "=== FastCallback Demo ===" << std::endl;

    FastCallback<int, int, int> cb1 = add;
    std::cout << "cb1(3, 4) = " << cb1(3, 4) << std::endl;

    FastCallback<void, const char*> cb2 = print_message;
    cb2("Hello from FastCallback!");
}

void demo_callback_box() {
    std::cout << "\n=== CallbackBox Demo ===" << std::endl;

    // Type is known at compile time
    auto lambda = [](int x) { return x * 2; };
    CallbackBox<decltype(lambda)> box(std::move(lambda));

    std::cout << "box(21) = " << box(21) << std::endl;  // 42

    // With capture
    int multiplier = 10;
    auto captured = [multiplier](int x) { return x * multiplier; };
    CallbackBox<decltype(captured)> box2(std::move(captured));

    std::cout << "box2(5) = " << box2(5) << std::endl;  // 50
}

// Hybrid: Choose based on context needs
void demo_hybrid() {
    std::cout << "\n=== Hybrid Approach ===" << std::endl;

    // For no context: use FastCallback (function pointer)
    FastCallback<int, int, int> fast = add;

    // For context: use CallbackBox with specific lambda type
    int offset = 100;
    auto with_offset = [offset](int x) { return x + offset; };
    CallbackBox<decltype(with_offset)> slow(std::move(with_offset));

    std::cout << "fast(5, 3) = " << fast(5, 3) << std::endl;
    std::cout << "slow(5) = " << slow(5) << std::endl;
}

// Size comparison
void demo_sizes() {
    std::cout << "\n=== Size Comparison ===" << std::endl;

    std::cout << "sizeof(FastCallback<int(int,int)>) = "
              << sizeof(FastCallback<int, int, int>) << " bytes" << std::endl;

    auto lambda = [](int x) { return x; };
    std::cout << "sizeof(CallbackBox<decltype(lambda)>) = "
              << sizeof(CallbackBox<decltype(lambda)>) << " bytes" << std::endl;

    std::cout << "sizeof(std::function<int(int)>) = "
              << sizeof(std::function<int(int)>) << " bytes" << std::endl;
}

int main() {
    demo_fast_callback();
    demo_callback_box();
    demo_hybrid();
    demo_sizes();

    std::cout << "\n=== Recommendations ===" << std::endl;
    std::cout << "- FastCallback: hot paths, no context needed" << std::endl;
    std::cout << "- CallbackBox: compile-time known type, with context" << std::endl;
    std::cout << "- std::function: runtime polymorphism, flexible" << std::endl;
    std::cout << "- Choose based on your performance/flexibility needs" << std::endl;

    return 0;
}
