#include <iostream>
#include <cstdint>
#include <array>

// 演示静态存储的各种形式

// 1. 全局变量 (.data段 - 已初始化)
int global_initialized = 100;

// 2. 未初始化全局变量 (.bss段)
int global_uninitialized;

// 3. 只读常量 (.rodata段 - 通常在Flash中)
static const int16_t sine_table[16] = {
    0,  6424,  11773,  15836,
    18479,  19595,  19151,  17205,
    13938,  9605,   4479,    0,
    -4479, -9605, -13938, -17205
};

// 4. 自定义段的变量
__attribute__((section(".rodata.lookup"))) const int lookup_table[8] = {0, 1, 2, 3, 4, 5, 6, 7};

// 5. 放在快速RAM的变量（示例）
__attribute__((section(".fastram"))) int fast_var;

// 6. 不初始化的变量（不会在启动时清零）
__attribute__((section(".noinit"))) int noinit_var;

void print_addresses() {
    std::cout << "=== Static Storage Addresses ===\n\n";

    std::cout << "Global initialized (.data):    " << &global_initialized << "\n";
    std::cout << "Global uninitialized (.bss):   " << &global_uninitialized << "\n";
    std::cout << "Const table (.rodata):         " << sine_table << "\n";
    std::cout << "Lookup table (.rodata.lookup): " << lookup_table << "\n";
    std::cout << "Fast var (.fastram):            " << &fast_var << "\n";
    std::cout << "Noinit var (.noinit):          " << &noinit_var << "\n";
}

void static_local_demo() {
    // static局部变量 - 只初始化一次
    static int counter = 0;
    counter++;

    std::cout << "Static local counter: " << counter
              << " (address: " << &counter << ")\n";
}

void constexpr_static_demo() {
    // constexpr静态变量 - 编译期计算
    static constexpr int fib[] = {0, 1, 1, 2, 3, 5, 8, 13, 21, 34};

    std::cout << "Compile-time Fibonacci: ";
    for (int v : fib) {
        std::cout << v << " ";
    }
    std::cout << "\n";
}

// 演示静态存储用于查表
int16_t fast_sin(uint8_t angle) {
    // 简化版：只演示查表访问
    return sine_table[angle % 16];
}

void lookup_table_demo() {
    std::cout << "\n--- Lookup Table Demo ---\n";
    std::cout << "sin(0) = " << fast_sin(0) << "\n";
    std::cout << "sin(90) = " << fast_sin(4) << "\n";
    std::cout << "sin(180) = " << fast_sin(8) << "\n";
}

// 模板演示编译期静态常量
template<int N>
struct Factorial {
    static constexpr int value = N * Factorial<N - 1>::value;
};

template<>
struct Factorial<0> {
    static constexpr int value = 1;
};

void template_static_demo() {
    std::cout << "\n--- Template Static Constant Demo ---\n";
    std::cout << "Factorial<5>::value = " << Factorial<5>::value << "\n";
    std::cout << "Factorial<10>::value = " << Factorial<10>::value << "\n";
}

// 静态断言演示编译期检查
static_assert(sizeof(sine_table) == 32, "sine_table size mismatch");

int main() {
    std::cout << "=== Static Allocation Demo ===\n\n";

    print_addresses();

    std::cout << "\n--- Static Local Variable (multiple calls) ---\n";
    static_local_demo();
    static_local_demo();
    static_local_demo();

    constexpr_static_demo();
    lookup_table_demo();
    template_static_demo();

    std::cout << "\n=== Key Takeaways ===\n";
    std::cout << "1. Static storage lifetime = program lifetime\n";
    std::cout << "2. .data: initialized globals (copied from Flash to RAM)\n";
    std::cout << "3. .bss: uninitialized globals (zeroed at startup)\n";
    std::cout << "4. .rodata: constants (stay in Flash,节省RAM)\n";
    std::cout << "5. Use sections to control memory placement\n";

    return 0;
}
