// 内联函数 vs 宏示例：展示为什么应该用内联函数替代宏

#include <cstdint>
#include <iostream>

// ==================== 不推荐：宏（没有类型检查）====================
#define MAX(a, b) ((a) > (b) ? (a) : (b))

// ==================== 推荐：内联函数（零开销且类型安全）====================
template<typename T>
inline constexpr T max(T a, T b) {
    return (a > b) ? a : b;
}

// ==================== 演示主函数 ====================
int main() {
    std::cout << "=== 内联函数 vs 宏示例 ===" << std::endl;

    // 基本用法
    std::cout << "MAX(3, 5) = " << MAX(3, 5) << std::endl;
    std::cout << "max(3, 5) = " << max(3, 5) << std::endl;

    // 类型安全：max 模板支持不同类型
    std::cout << "max(3.5, 2.1) = " << max(3.5, 2.1) << std::endl;

    // 演示宏的问题：参数被求值两次
    int i = 0;
    int j = 0;
    // MAX(i++, j++) 会展开为 ((i++) > (j++) ? (i++) : (j++))
    // 结果 i 或 j 会被增加两次！
    int macro_result = MAX(i++, j++);
    std::cout << "\n宏的问题：MAX(i++, j++)" << std::endl;
    std::cout << "结果: " << macro_result << ", i = " << i << ", j = " << j << std::endl;

    // 内联函数没有这个问题
    int k = 0;
    int l = 0;
    int inline_result = max(k++, l++);
    std::cout << "\n内联函数：max(k++, l++)" << std::endl;
    std::cout << "结果: " << inline_result << ", k = " << k << ", l = " << l << std::endl;

    std::cout << "\n关键点：" << std::endl;
    std::cout << "1. 宏没有类型检查，可能有副作用（参数多次求值）" << std::endl;
    std::cout << "2. 内联函数有类型检查，参数只求值一次" << std::endl;
    std::cout << "3. inline 让编译器直接插入函数体，无函数调用开销" << std::endl;
    std::cout << "4. constexpr 还能让编译期常量在编译期计算" << std::endl;

    return 0;
}
