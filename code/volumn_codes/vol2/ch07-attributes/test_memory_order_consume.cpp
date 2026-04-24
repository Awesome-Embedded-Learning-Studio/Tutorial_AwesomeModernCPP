// 验证代码：memory_order_consume 的实际处理
// 验证 GCC/Clang 是否将 consume 升级为 acquire

#include <atomic>

std::atomic<int*> ptr;

// 测试 consume
int test_consume() {
    int* p = ptr.load(std::memory_order_consume);
    return *p;  // 依赖关系
}

// 测试 acquire
int test_acquire() {
    int* p = ptr.load(std::memory_order_acquire);
    return *p;
}

/*
编译命令：
g++ -std=c++17 -O2 -S test_memory_order_consume.cpp -o test_consume.s

测试结果（GCC 15.2.1, -O2）：
_Z11test_consumev:
    movq    ptr(%rip), %rax
    movl    (%rax), %eax
    ret

_Z11test_acquirev:
    movq    ptr(%rip), %rax
    movl    (%rax), %eax
    ret

两个函数生成的汇编完全相同！

结论：文章断言正确 - GCC 确实将 memory_order_consume
升级为 memory_order_acquire，两者的汇编代码完全一致。
这证实了 [[carries_dependency]] 属性在主流编译器上
几乎成了摆设的说法。
*/
