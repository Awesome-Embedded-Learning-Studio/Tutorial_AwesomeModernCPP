// inline 代码膨胀示例

#include <iostream>
#include <array>

// 一个简单的工具函数
inline int compute_value(int x) {
    return x * 2 + 1;
}

// 在多个地方调用
void function_a() {
    for (int i = 0; i < 10; ++i) {
        volatile int result = compute_value(i);  // volatile 防止优化
        (void)result;
    }
}

void function_b() {
    for (int i = 0; i < 10; ++i) {
        volatile int result = compute_value(i + 100);  // volatile 防止优化
        (void)result;
    }
}

void function_c() {
    for (int i = 0; i < 10; ++i) {
        volatile int result = compute_value(i + 200);  // volatile 防止优化
        (void)result;
    }
}

// 模板实例化导致的代码膨胀
template<typename T>
inline T process(T value) {
    return value * value + value;
}

// 不同类型的实例
void demonstrate_template_bloat() {
    volatile int a = process(42);      // 实例化 process<int>
    volatile double b = process(3.14); // 实例化 process<double>
    volatile float c = process(2.7f);  // 实例化 process<float>
    (void)a; (void)b; (void)c;
}

int main() {
    std::cout << "=== inline 代码膨胀示例 ===" << std::endl;

    function_a();
    function_b();
    function_c();
    demonstrate_template_bloat();

    std::cout << "\n关键点：" << std::endl;
    std::cout << "1. inline 会把函数体复制到每个调用点" << std::endl;
    std::cout << "2. 频繁调用的函数会导致代码体积增大" << std::endl;
    std::cout << "3. 模板会为每种类型生成独立实例" << std::endl;
    std::cout << "4. Flash 紧张的设备需要权衡代码大小和性能" << std::endl;

    std::cout << "\n验证方法：" << std::endl;
    std::cout << "1. 查看 build/ 目录中的 .o 文件大小" << std::endl;
    std::cout << "2. 使用 'size' 命令查看可执行文件各段大小" << std::endl;
    std::cout << "3. 对比开启/关闭优化时的二进制大小" << std::endl;

    std::cout << "\n建议：" << std::endl;
    std::cout << "- 小而频繁调用的函数：适合 inline" << std::endl;
    std::cout << "- 大函数或调用不频繁：避免 inline" << std::endl;
    std::cout << "- 模板：注意类型数量，可以用 concepts 约束" << std::endl;

    return 0;
}
