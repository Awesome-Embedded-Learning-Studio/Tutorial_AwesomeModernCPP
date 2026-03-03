#include <iostream>
#include <cstdint>
#include <cstddef>

// 演示内存布局：静态区、栈、堆

// 静态区 - 全局变量
static int global_var = 42;

// .rodata - 只读常量
static const char rodata_str[] = "This is in read-only memory";

void stack_allocation_demo() {
    // 栈上分配
    int stack_var = 100;
    std::cout << "Stack variable address: " << &stack_var << "\n";
    std::cout << "Stack variable value: " << stack_var << "\n";

    // 栈上的数组
    uint8_t stack_buffer[128];
    std::cout << "Stack buffer address: " << static_cast<void*>(stack_buffer) << "\n";

    // 危险：大栈分配可能导致溢出
    // uint8_t big_buffer[64 * 1024];  // 不要这样做！
}

void heap_allocation_demo() {
    // 堆上分配 - 在嵌入式系统中要谨慎使用
    int* heap_var = new int(200);
    std::cout << "Heap variable address: " << heap_var << "\n";
    std::cout << "Heap variable value: " << *heap_var << "\n";

    // 必须记得释放
    delete heap_var;

    // 数组分配
    const size_t n = 10;
    int* heap_array = new int[n];
    for (size_t i = 0; i < n; ++i) {
        heap_array[i] = static_cast<int>(i * i);
    }

    // 使用数组...
    for (size_t i = 0; i < n; ++i) {
        std::cout << "heap_array[" << i << "] = " << heap_array[i] << "\n";
    }

    delete[] heap_array;
}

// 静态分配示例
void static_allocation_demo() {
    // static 局部变量 - 在静态区，只初始化一次
    static int static_counter = 0;
    static_counter++;
    std::cout << "Static counter (preserved across calls): " << static_counter << "\n";
    std::cout << "Static variable address: " << &static_counter << "\n";
}

struct AlignmentDemo {
    char c;      // offset 0
    // 3 bytes padding
    int32_t x;   // offset 4 (4-byte aligned)
    char d;      // offset 8
    // 3 bytes padding to make sizeof 12
}; // sizeof(AlignmentDemo) = 12

struct PackedDemo {
    int32_t x;   // offset 0
    char c;      // offset 4
    char d;      // offset 5
    // 2 bytes padding to make sizeof 8
}; // sizeof(PackedDemo) = 8

void alignment_demo() {
    std::cout << "sizeof(AlignmentDemo) = " << sizeof(AlignmentDemo) << "\n";
    std::cout << "alignof(AlignmentDemo) = " << alignof(AlignmentDemo) << "\n";

    std::cout << "sizeof(PackedDemo) = " << sizeof(PackedDemo) << "\n";
    std::cout << "alignof(PackedDemo) = " << alignof(PackedDemo) << "\n";

    AlignmentDemo a;
    std::cout << "Address of a.c: " << static_cast<void*>(&a.c) << "\n";
    std::cout << "Address of a.x: " << &a.x << "\n";
    std::cout << "Address of a.d: " << static_cast<void*>(&a.d) << "\n";
}

int main() {
    std::cout << "=== Memory Layout Demo ===\n\n";

    std::cout << "Global variable address: " << &global_var << "\n";
    std::cout << "ROdata string address: " << static_cast<const void*>(rodata_str) << "\n\n";

    std::cout << "--- Stack Allocation ---\n";
    stack_allocation_demo();
    std::cout << "\n";

    std::cout << "--- Heap Allocation ---\n";
    heap_allocation_demo();
    std::cout << "\n";

    std::cout << "--- Static Allocation (multiple calls) ---\n";
    static_allocation_demo();
    static_allocation_demo();
    static_allocation_demo();
    std::cout << "\n";

    std::cout << "--- Alignment and Padding ---\n";
    alignment_demo();

    return 0;
}
