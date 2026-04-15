// using_basics.cpp
// typedef vs using 基本用法对比

#include <cstdint>
#include <iostream>

// 老派 typedef
typedef uint32_t reg32_t;
typedef volatile uint32_t* reg_ptr_t;
typedef void (*handler_t)(int);

// 现代 using
using reg32_t_modern = uint32_t;
using reg_ptr_t_modern = volatile uint32_t*;
using handler_t_modern = void(*)(int);

// 复杂函数指针对比
typedef void (*signal_handler_old)(int signo, void* info, void* ctx);
using signal_handler_new = void(*)(int signo, void* info, void* ctx);

void test_handler(int x) {
    std::cout << "Handler called with: " << x << "\n";
}

int main() {
    std::cout << "=== typedef vs using ===\n\n";

    // 使用 typedef 定义的类型
    reg32_t r1 = 42;
    std::cout << "reg32_t: " << r1 << "\n";

    // 使用 using 定义的类型
    reg32_t_modern r2 = 100;
    std::cout << "reg32_t_modern: " << r2 << "\n";

    // 函数指针
    handler_t_modern h = test_handler;
    h(42);

    std::cout << "\nSize comparison:\n";
    std::cout << "sizeof(reg32_t): " << sizeof(reg32_t) << "\n";
    std::cout << "sizeof(reg32_t_modern): " << sizeof(reg32_t_modern) << "\n";

    return 0;
}
