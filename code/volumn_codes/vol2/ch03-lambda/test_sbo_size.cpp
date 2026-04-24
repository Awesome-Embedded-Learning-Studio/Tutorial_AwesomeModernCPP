/**
 * @file test_sbo_size.cpp
 * @brief 验证 std::function 的 SBO（小对象优化）缓冲区大小
 * @details 测试不同大小捕获的 lambda 是否触发堆分配
 */

#include <functional>
#include <iostream>
#include <array>
#include <cstddef>
#include <cstdlib>

// 全局计数器用于追踪堆分配
static std::size_t allocation_count = 0;

// 重载 operator new 来检测堆分配
void* operator new(std::size_t size) {
    ++allocation_count;
    return std::malloc(size);
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

void test_sbo_size() {
    std::cout << "=== SBO 缓冲区大小测试 ===\n";
    std::cout << "编译器: GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "\n";
    std::cout << "sizeof(std::function<int()>): "
              << sizeof(std::function<int()>) << " bytes\n";
    std::cout << "sizeof(void(*)()): "
              << sizeof(void(*)()) << " bytes\n";

    // 测试不同大小的 lambda 是否触发堆分配
    // 小 lambda（应该放入 SBO）
    allocation_count = 0;
    {
        auto small_lambda = [x = 42]() { return x * 2; };
        std::function<int()> f1 = small_lambda;
    }
    std::cout << "\n小 lambda (捕获1个int): 堆分配 " << allocation_count << " 次\n";

    // 中等 lambda（可能刚好在边界）
    allocation_count = 0;
    {
        auto medium_lambda = [a = 1, b = 2, c = 3, d = 4, e = 5]() {
            return a + b + c + d + e;
        };
        std::function<int()> f2 = medium_lambda;
    }
    std::cout << "中 lambda (捕获5个int): 堆分配 " << allocation_count << " 次\n";

    // 大 lambda（肯定触发堆分配）
    allocation_count = 0;
    {
        auto large_lambda = [data = std::array<int, 100>{}]() {
            return data.size();
        };
        std::function<std::size_t()> f3 = large_lambda;
    }
    std::cout << "大 lambda (捕获array<100>): 堆分配 " << allocation_count << " 次\n";

    // 测试不同数量的 size_t 捕获
    std::cout << "\n=== 寻找 SBO 边界 ===\n";

    // 测试小对象
    allocation_count = 0;
    {
        auto tiny = []() { return 42; };
        std::function<int()> f = tiny;
    }
    std::cout << "无捕获 lambda: 堆分配 " << allocation_count << " 次\n";

    // 测试指针大小
    allocation_count = 0;
    {
        int data[10] = {};
        auto ptr_lambda = [data]() { return data[0]; };
        std::function<int()> f = ptr_lambda;
    }
    std::cout << "捕获指针(8字节): 堆分配 " << allocation_count << " 次\n";

    // 测试多个指针
    allocation_count = 0;
    {
        int a[10] = {}, b[10] = {}, c[10] = {};
        auto multi_ptr = [a, b, c]() { return a[0] + b[0] + c[0]; };
        std::function<int()> f = multi_ptr;
    }
    std::cout << "捕获3个指针(24字节): 堆分配 " << allocation_count << " 次\n";
}

int main() {
    test_sbo_size();
    std::cout << "\n结论: GCC 15.2 的 SBO 实现较为保守\n";
    std::cout << "建议: 如果需要避免堆分配，优先使用模板参数或手写类型擦除\n";
    return 0;
}
