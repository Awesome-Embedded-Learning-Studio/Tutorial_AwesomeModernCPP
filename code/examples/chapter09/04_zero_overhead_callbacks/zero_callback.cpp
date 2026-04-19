// Zero Overhead Callback Implementation
// Demonstrates a type-erased callback with manual vtable

#include <iostream>
#include <cstdint>
#include <cstring>
#include <functional>

template<typename Signature, size_t Size = 32>
class ZeroCallback;

template<size_t Size, typename R, typename... Args>
class ZeroCallback<R(Args...), Size> {
    // VTable with function pointers instead of virtual functions
    struct VTable {
        void (*move)(void* dest, void* src);
        void (*destroy)(void* obj);
        R (*invoke)(void* obj, Args...);
    };

    // Storage for the callable
    alignas(std::max_align_t) std::byte storage[Size];
    const VTable* vtable = nullptr;

    // Generate VTable for each callable type
    template<typename T>
    struct VTableFor {
        static void do_move(void* dest, void* src) {
            new(dest) T(std::move(*reinterpret_cast<T*>(src)));
        }
        static void do_destroy(void* obj) {
            reinterpret_cast<T*>(obj)->~T();
        }
        static R do_invoke(void* obj, Args... args) {
            return (*reinterpret_cast<T*>(obj))(args...);
        }
        static constexpr VTable value = {do_move, do_destroy, do_invoke};
    };

public:
    ZeroCallback() = default;

    // Accept any callable object
    template<typename T>
    ZeroCallback(T&& callable) {
        using TDecay = std::decay_t<T>;
        static_assert(sizeof(TDecay) <= Size, "Callable too large for ZeroCallback");
        new(storage) TDecay(std::forward<T>(callable));
        vtable = &VTableFor<TDecay>::value;
    }

    // Move constructor
    ZeroCallback(ZeroCallback&& other) noexcept : vtable(other.vtable) {
        if (vtable) {
            vtable->move(storage, other.storage);
            other.vtable = nullptr;
        }
    }

    // Destructor
    ~ZeroCallback() {
        if (vtable) {
            vtable->destroy(storage);
        }
    }

    // No copy
    ZeroCallback(const ZeroCallback&) = delete;
    ZeroCallback& operator=(const ZeroCallback&) = delete;

    // Invoke the callback
    R operator()(Args... args) const {
        return vtable->invoke(const_cast<std::byte*>(storage), args...);
    }

    bool empty() const { return vtable == nullptr; }
};

// Demo usage
int main() {
    std::cout << "=== Zero Overhead Callback Demo ===" << std::endl;

    // Store a simple lambda
    ZeroCallback<int(int)> cb = [](int x) { return x * 2; };
    std::cout << "cb(21) = " << cb(21) << std::endl;  // 42

    // Store a lambda with capture
    int multiplier = 10;
    ZeroCallback<int(int)> captured = [multiplier](int x) {
        return x * multiplier;
    };
    std::cout << "captured(5) = " << captured(5) << std::endl;  // 50

    // Store a function pointer
    auto add = [](int a, int b) { return a + b; };
    ZeroCallback<int(int, int), 16> fp_cb = add;
    std::cout << "fp_cb(3, 4) = " << fp_cb(3, 4) << std::endl;  // 7

    // Size comparison
    std::cout << "\n=== Size Comparison ===" << std::endl;
    std::cout << "sizeof(ZeroCallback<int(int), 32>) = "
              << sizeof(ZeroCallback<int(int), 32>) << " bytes" << std::endl;
    std::cout << "sizeof(std::function<int(int)>) = "
              << sizeof(std::function<int(int)>) << " bytes" << std::endl;
    std::cout << "sizeof(int(*)(int)) = " << sizeof(int(*)(int)) << " bytes" << std::endl;

    // Performance characteristics
    std::cout << "\n=== Performance Characteristics ===" << std::endl;
    std::cout << "- No heap allocation (for small callables)" << std::endl;
    std::cout << "- One level of indirection (function pointer)" << std::endl;
    std::cout << "- Can be inlined by compiler in some cases" << std::endl;
    std::cout << "- Fixed size, stack-allocated storage" << std::endl;

    return 0;
}
