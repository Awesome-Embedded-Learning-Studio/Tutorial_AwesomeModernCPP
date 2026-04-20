// values.cpp -- 值类别与引用绑定演示
// Standard: C++11

#include <iostream>

/// @brief 返回一个整数值（右值）
int get_value()
{
    return 42;
}

/// @brief 返回一个整数的引用（左值）
int global = 100;
int& get_ref()
{
    return global;
}

int main()
{
    // ---- 左值 ----
    int x = 10;            // x 是左值
    int* ptr = &x;         // &x 合法：x 是左值，可以取地址
    *ptr = 20;             // *ptr 是左值
    int arr[3] = {1, 2, 3};
    arr[0] = 99;           // arr[0] 是左值

    std::cout << "x = " << x << "\n";            // 20
    std::cout << "arr[0] = " << arr[0] << "\n";  // 99

    // ---- 右值 ----
    // &42;                  // 错误：不能对右值取地址
    // &(x + 1);             // 错误：x + 1 的结果是右值
    // &get_value();          // 错误：函数返回值是右值

    int sum = x + arr[1];   // x + arr[1] 的结果是右值
    std::cout << "sum = " << sum << "\n";        // 22

    // ---- 左值引用 ----
    int& lref = x;          // OK：左值引用绑定到左值
    lref = 30;
    std::cout << "x = " << x << "\n";            // 30

    // int& bad = 42;        // 错误：左值引用不能绑定到右值

    const int& cref = 42;   // OK：const 引用可以绑定到右值
    std::cout << "cref = " << cref << "\n";      // 42

    // ---- 右值引用（C++11）----
    int&& rref = 42;        // OK：右值引用绑定到右值
    int&& rref2 = x + 1;   // OK：x + 1 是右值
    // int&& rref3 = x;     // 错误：右值引用不能绑定到左值

    std::cout << "rref = " << rref << "\n";      // 42
    std::cout << "rref2 = " << rref2 << "\n";    // 31

    // ---- 函数返回值的值类别 ----
    // get_value() 返回右值
    int val = get_value();
    std::cout << "get_value() = " << val << "\n";   // 42

    // get_ref() 返回左值
    get_ref() = 200;       // OK：get_ref() 返回左值引用，可以赋值
    std::cout << "global = " << global << "\n";      // 200

    return 0;
}
