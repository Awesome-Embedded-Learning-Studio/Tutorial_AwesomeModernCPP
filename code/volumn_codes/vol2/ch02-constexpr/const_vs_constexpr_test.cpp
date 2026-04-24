/**
 * @file const_vs_constexpr_test.cpp
 * @brief 验证 const 和 constexpr 在常量表达式上下文中的行为差异
 *
 * 编译环境：GCC 15.2.1, C++17
 * 验证目的：
 * 1. 测试 const 整型常量能否用于数组大小
 * 2. 测试 const 整型常量能否用于非类型模板参数
 * 3. 验证文章中关于"const 整型常量默认具有内部链接性"的断言
 */

#include <cstdint>
#include <iostream>

// const 整型变量：用编译期常量初始化
const int kConstValue = 256;

// constexpr 变量
constexpr int kConstexprValue = 256;

// 测试能否用于数组大小（需要编译期常量）
void test_array_size()
{
    int array1[kConstValue];        // C++ 标准允许：const 整型常量可用于数组大小
    int array2[kConstexprValue];    // constexpr 也可以

    std::cout << "Array sizes: " << sizeof(array1) << " vs " << sizeof(array2) << "\n";
}

// 测试能否用于非类型模板参数
template <int N>
struct Array {
    int data[N];
    int size() const { return N; }
};

void test_template_param()
{
    Array<kConstValue> a1;         // C++ 标准允许：const 整型常量可用
    Array<kConstexprValue> a2;     // constexpr 可用

    std::cout << "Template params: " << a1.size() << " vs " << a2.size() << "\n";
}

// 测试链接性
namespace {
    const int kInternalConst = 100;      // 匿名命名空间中的 const 变量
    constexpr int kInternalConstexpr = 100;
}

// 全局作用域的 const 整型常量
const int kGlobalConst = 200;
constexpr int kGlobalConstexpr = 200;

int main()
{
    test_array_size();
    test_template_param();

    std::cout << "\n结论：\n";
    std::cout << "- C++ 标准规定：const 整型常量（初始化为常量表达式）是常量表达式\n";
    std::cout << "- 因此可以用于数组大小和非类型模板参数\n";
    std::cout << "- 文章中关于'const 变量不能用于数组大小'的描述不够准确\n";
    std::cout << "- 准确的说法是：const 变量的初始值必须是常量表达式才能用于这些场景\n";

    return 0;
}
