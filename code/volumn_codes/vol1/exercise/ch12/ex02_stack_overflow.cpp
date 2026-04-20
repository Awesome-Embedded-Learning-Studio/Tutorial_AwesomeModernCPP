/**
 * @file ex02_stack_overflow.cpp
 * @brief 练习：找出栈溢出隐患
 *
 * 识别并修复两种典型的栈溢出风险：
 * 1. 在栈上分配过大的局部数组（约 8 MB，超出默认栈限制）
 * 2. 递归函数缺少终止条件导致无限递归
 */

#include <iostream>
#include <vector>

// ============================================================
// 问题 1：栈上分配过大的缓冲区
// ============================================================

// ---- 有问题的版本（已注释）----
//
// void process_image_bad()
// {
//     // 图像缓冲区：1920 x 1080 x 4 (RGBA) = 8,294,400 字节 ≈ 8 MB
//     // Linux 默认栈大小约 8 MB，Windows 约 1 MB
//     // 在栈上分配这么大的数组会直接导致栈溢出 (segmentation fault)
//     unsigned char buffer[1920 * 1080 * 4];
//     // ... 处理图像 ...
// }

// ---- 修复后的版本：使用 std::vector 在堆上分配 ----

void process_image_fixed()
{
    // 图像缓冲区：1920 x 1080 x 4 (RGBA)
    // std::vector 内部在堆上分配内存，不受栈大小限制
    constexpr int kWidth = 1920;
    constexpr int kHeight = 1080;
    constexpr int kChannels = 4;
    constexpr std::size_t kBufferSize =
        static_cast<std::size_t>(kWidth) * kHeight * kChannels;

    std::vector<unsigned char> buffer(kBufferSize, 0);

    std::cout << "  图像缓冲区大小: " << kBufferSize << " 字节 ("
              << kBufferSize / (1024.0 * 1024.0) << " MB)\n";
    std::cout << "  buffer.data() 地址: " << static_cast<void*>(buffer.data())
              << " (堆上)\n";

    // 模拟图像处理：写入像素值
    for (std::size_t i = 0; i < kBufferSize; ++i) {
        buffer[i] = static_cast<unsigned char>(i % 256);
    }

    std::cout << "  前 4 个像素值: ";
    for (int i = 0; i < 4; ++i) {
        std::cout << static_cast<int>(buffer[i]) << " ";
    }
    std::cout << "\n";
    std::cout << "  处理完成，函数返回后 vector 自动释放内存\n";
}

// ============================================================
// 问题 2：递归缺少终止条件
// ============================================================

// ---- 有问题的版本（已注释）----
//
// int fibonacci_bad(int n)
// {
//     // 缺少终止条件！无限递归 → 栈溢出
//     return fibonacci_bad(n - 1) + fibonacci_bad(n - 2);
// }

// ---- 修复后的版本：添加终止条件 ----

int fibonacci(int n)
{
    // 终止条件（基准情形）
    if (n <= 0) { return 0; }
    if (n == 1) { return 1; }

    return fibonacci(n - 1) + fibonacci(n - 2);
}

// ---- 更安全的版本：用迭代代替递归，彻底消除栈溢出风险 ----

int fibonacci_iterative(int n)
{
    if (n <= 0) { return 0; }
    if (n == 1) { return 1; }

    int prev = 0;
    int curr = 1;
    for (int i = 2; i <= n; ++i) {
        int next = prev + curr;
        prev = curr;
        curr = next;
    }
    return curr;
}

int main()
{
    std::cout << "===== ex02: 找出栈溢出隐患 =====\n\n";

    // ---- 演示 1：大数组修复 ----
    std::cout << "--- 问题 1：栈上分配大缓冲区 ---\n";
    std::cout << "  有问题的代码:\n";
    std::cout << "    unsigned char buffer[1920 * 1080 * 4]; // ≈8 MB，超出栈限制\n";
    std::cout << "  修复方案:\n";
    std::cout << "    std::vector<unsigned char> buffer(kSize); // 堆上分配\n\n";

    process_image_fixed();

    // ---- 演示 2：递归修复 ----
    std::cout << "\n--- 问题 2：递归缺少终止条件 ---\n";
    std::cout << "  有问题的代码:\n";
    std::cout << "    return fibonacci(n - 1) + fibonacci(n - 2); // 无终止条件\n";
    std::cout << "  修复方案: 添加 n <= 0 和 n == 1 的终止条件\n\n";

    std::cout << "  递归版 fibonacci 结果:\n";
    for (int i = 0; i <= 10; ++i) {
        std::cout << "    fib(" << i << ") = " << fibonacci(i) << "\n";
    }

    std::cout << "\n  迭代版 fibonacci 结果 (更安全):\n";
    for (int i = 0; i <= 10; ++i) {
        std::cout << "    fib(" << i << ") = " << fibonacci_iterative(i) << "\n";
    }

    // 验证两种实现结果一致
    bool all_match = true;
    for (int i = 0; i <= 10; ++i) {
        if (fibonacci(i) != fibonacci_iterative(i)) {
            all_match = false;
            break;
        }
    }
    std::cout << "\n  递归版与迭代版结果一致: "
              << (all_match ? "是" : "否") << "\n";

    std::cout << "\n要点:\n";
    std::cout << "  1. 栈空间有限（Linux ≈8MB, Windows ≈1MB），大数组应用 vector 或堆分配\n";
    std::cout << "  2. 递归必须有终止条件，否则无限递归导致栈溢出\n";
    std::cout << "  3. 栈溢出是 SIGSEGV 信号，程序直接终止，无异常处理机会\n";
    std::cout << "  4. 能用迭代替代递归时优先使用迭代，彻底消除栈溢出风险\n";

    return 0;
}
