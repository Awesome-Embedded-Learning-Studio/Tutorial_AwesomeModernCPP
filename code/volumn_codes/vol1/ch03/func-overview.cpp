// functions.cpp
// Platform: host
// Standard: C++17

#include <iostream>
#include <string>

// 函数声明（原型）
int add(int a, int b);
int max_of(int a, int b);
int factorial(int n);
bool is_even(int n);
void print_result(const std::string& label, int value);

// main 函数——程序入口
int main()
{
    // 加法
    int sum = add(15, 27);
    print_result("15 + 27", sum);

    // 取较大值
    int bigger = max_of(42, 17);
    print_result("max(42, 17)", bigger);

    // 阶乘
    int fact = factorial(6);
    print_result("6!", fact);

    // 判断奇偶
    int test_values[] = {0, 1, 2, 7, 10};
    for (int val : test_values) {
        std::cout << val << " 是"
                  << (is_even(val) ? "偶数" : "奇数")
                  << std::endl;
    }

    return 0;
}

// ---- 函数定义 ----

int add(int a, int b)
{
    return a + b;
}

int max_of(int a, int b)
{
    if (a > b) {
        return a;
    }
    return b;
}

/// @brief 计算 n 的阶乘（n!）
/// @param n 非负整数
/// @return n 的阶乘
int factorial(int n)
{
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

bool is_even(int n)
{
    return n % 2 == 0;
}

void print_result(const std::string& label, int value)
{
    std::cout << label << " = " << value << std::endl;
}
