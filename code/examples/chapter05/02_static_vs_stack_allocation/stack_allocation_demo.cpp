#include <iostream>
#include <cstdint>
#include <cstring>

// 演示栈上分配

void simple_stack_allocation() {
    std::cout << "--- Simple Stack Allocation ---\n";

    // 基本类型
    int x = 42;
    double y = 3.14;
    char c = 'A';

    std::cout << "int x at:    " << &x << ", value: " << x << "\n";
    std::cout << "double y at: " << &y << ", value: " << y << "\n";
    std::cout << "char c at:   " << static_cast<void*>(&c) << ", value: " << c << "\n";

    // 数组
    int arr[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::cout << "array at:    " << arr << "\n";

    // 结构体
    struct Point { int x, y; } p = {10, 20};
    std::cout << "struct at:   " << &p << ", p.x=" << p.x << ", p.y=" << p.y << "\n";
}

void stack_frame_growth_demo() {
    std::cout << "\n--- Stack Frame Growth Demo ---\n";

    int a;
    std::cout << "Frame level 1, a at: " << &a << "\n";

    {
        int b;
        std::cout << "Frame level 2, b at: " << &b << "\n";

        {
            int c;
            std::cout << "Frame level 3, c at: " << &c << "\n";
        }
    }

    int d;
    std::cout << "Frame level 1 again, d at: " << &d << "\n";
}

// 危险：大栈分配
void dangerous_stack_allocation() {
    std::cout << "\n--- Dangerous: Large Stack Allocation ---\n";
    std::cout << "WARNING: This can cause stack overflow!\n";

    // 不要在真实代码中这样做！
    // uint8_t big_buffer[64 * 1024];  // 64KB on stack!

    // 安全的替代方案：使用静态分配
    static uint8_t safe_buffer[64 * 1024];
    std::cout << "Static buffer at: " << static_cast<void*>(safe_buffer) << "\n";
}

void stack_vs_heap() {
    std::cout << "\n--- Stack vs Heap Comparison ---\n";

    // 栈分配
    int stack_var = 100;
    std::cout << "Stack var at: " << &stack_var << "\n";

    // 堆分配
    int* heap_var = new int(200);
    std::cout << "Heap var at:  " << heap_var << "\n";

    delete heap_var;
}

void recursive_demo(int depth) {
    if (depth <= 0) {
        std::cout << "Recursion depth reached\n";
        return;
    }

    int local = depth;
    std::cout << "Depth " << depth << ", local at: " << &local << "\n";

    recursive_demo(depth - 1);
}

void recursion_demo() {
    std::cout << "\n--- Recursion and Stack Usage ---\n";
    std::cout << "WARNING: Deep recursion can overflow stack!\n";

    // 限制递归深度
    recursive_demo(5);
}

// VLA (Variable Length Array) - 危险！
// 注：C++标准不支持VLA，但GCC扩展支持
void vla_demo() {
    std::cout << "\n--- Variable Length Arrays (VLA) ---\n";
    std::cout << "WARNING: VLA is non-standard and dangerous in embedded!\n";

    int n = 10;
    // int vla[n];  // 不要使用！

    // 安全替代方案：std::array或固定最大大小
    int safe_arr[100];  // 固定最大大小
    std::cout << "Fixed array at: " << safe_arr << "\n";
}

void alloca_demo() {
    std::cout << "\n--- alloca() Usage ---\n";
    std::cout << "WARNING: alloca is dangerous in embedded systems!\n";

    // void* ptr = alloca(1024);  // 不要使用！

    // 安全替代方案：静态或栈上固定大小
    char buffer[1024];
    std::cout << "Fixed buffer at: " << static_cast<void*>(buffer) << "\n";
}

// 栈使用检测
size_t stack_remaining() {
    // 平台相关的实现
    // 这是一个简化的演示
    char c;
    uintptr_t stack_addr = reinterpret_cast<uintptr_t>(&c);
    // 假设栈大小是8KB，栈底在高地址
    constexpr size_t stack_size = 8 * 1024;
    uintptr_t stack_base = stack_addr | (stack_size - 1);
    return stack_base - stack_addr;
}

void stack_usage_demo() {
    std::cout << "\n--- Stack Usage Estimation ---\n";
    std::cout << "Estimated stack remaining: " << stack_remaining() << " bytes\n";
}

int main() {
    std::cout << "=== Stack Allocation Demo ===\n\n";

    simple_stack_allocation();
    stack_frame_growth_demo();
    dangerous_stack_allocation();
    stack_vs_heap();
    recursion_demo();
    vla_demo();
    alloca_demo();
    stack_usage_demo();

    std::cout << "\n=== Key Takeaways ===\n";
    std::cout << "1. Stack allocation is fast (pointer arithmetic)\n";
    std::cout << "2. Stack size is limited (typically KB range)\n";
    std::cout << "3. Avoid large allocations on stack\n";
    std::cout << "4. Avoid deep recursion in embedded\n";
    std::cout << "5. Use static allocation for large buffers\n";

    return 0;
}
