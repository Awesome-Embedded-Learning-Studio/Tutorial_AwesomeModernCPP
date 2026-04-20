// func_template.cpp
// 编译: g++ -Wall -Wextra -std=c++17 func_template.cpp -o func_template

#include <cstring>
#include <iostream>
#include <string>
// ============================================================
// max_value：返回两个值中较大的一个
// ============================================================
template <typename T>
T max_value(T a, T b)
{
    return (a > b) ? a : b;
}

// const char* 特化：按字典序比较字符串内容
template <>
const char* max_value<const char*>(const char* a, const char* b)
{
    return (std::strcmp(a, b) > 0) ? a : b;
}
// ============================================================
// swap_value：交换两个值
// ============================================================
template <typename T>
void swap_value(T& a, T& b)
{
    T temp = a;
    a = b;
    b = temp;
}
// ============================================================
// print_array：打印数组内容
// ============================================================
template <typename T, std::size_t kSize>
void print_array(const T (&arr)[kSize])
{
    std::cout << "[";
    for (std::size_t i = 0; i < kSize; ++i) {
        std::cout << arr[i];
        if (i + 1 < kSize) {
            std::cout << ", ";
        }
    }
    std::cout << "]";
}
// ============================================================
// main
// ============================================================
int main()
{
    // --- max_value ---
    std::cout << "=== max_value ===\n";
    std::cout << "max_value(3, 7) = " << max_value(3, 7) << "\n";
    std::cout << "max_value(2.5, 1.3) = " << max_value(2.5, 1.3)
              << "\n";
    std::cout << "max_value(\"banana\", \"apple\") = "
              << max_value("banana", "apple") << "\n";

    // 显式实例化：混合类型
    std::cout << "max_value<double>(3, 5.7) = "
              << max_value<double>(3, 5.7) << "\n";

    // --- swap_value ---
    std::cout << "\n=== swap_value ===\n";
    int a = 10, b = 20;
    std::cout << "before: a=" << a << ", b=" << b << "\n";
    swap_value(a, b);
    std::cout << "after:  a=" << a << ", b=" << b << "\n";

    double x = 1.5, y = 2.5;
    std::cout << "before: x=" << x << ", y=" << y << "\n";
    swap_value(x, y);
    std::cout << "after:  x=" << x << ", y=" << y << "\n";

    std::string s1 = "hello", s2 = "world";
    std::cout << "before: s1=\"" << s1 << "\", s2=\"" << s2 << "\"\n";
    swap_value(s1, s2);
    std::cout << "after:  s1=\"" << s1 << "\", s2=\"" << s2 << "\"\n";

    // --- print_array ---
    std::cout << "\n=== print_array ===\n";
    int nums[] = {3, 1, 4, 1, 5, 9};
    std::cout << "int[]:    ";
    print_array(nums);
    std::cout << "\n";

    double vals[] = {1.1, 2.2, 3.3};
    std::cout << "double[]: ";
    print_array(vals);
    std::cout << "\n";

    std::string names[] = {"Alice", "Bob", "Charlie"};
    std::cout << "string[]: ";
    print_array(names);
    std::cout << "\n";

    return 0;
}
