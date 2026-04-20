/**
 * @file ex11_ref_binding_predict.cpp
 * @brief 练习：预测引用绑定
 *
 * 预测以下引用绑定哪些能编译通过，哪些会报错：
 *   int& r1 = x;       // 左值引用绑定到左值
 *   int& r2 = x + 1;   // 左值引用绑定到右值
 *   const int& r3 = x + 1; // const 引用绑定到右值
 *   int&& r4 = x;      // 右值引用绑定到左值
 *   int&& r5 = x + 1;  // 右值引用绑定到右值
 *   const int& r6 = x; // const 左值引用绑定到左值
 */

#include <iostream>

int main() {
    std::cout << "===== ex11: 预测引用绑定 =====\n\n";

    int x = 10;

    // r1: int& r1 = x;
    // 预测：编译通过
    // 原因：x 是左值，非 const 左值引用可以绑定到左值
    int& r1 = x;
    std::cout << "[通过] int& r1 = x;          // r1 = " << r1 << "\n";
    std::cout << "       原因：左值引用可以绑定到左值\n\n";

    // r2: int& r2 = x + 1;
    // 预测：编译失败
    // 原因：x + 1 是右值（算术表达式的临时结果），
    //       非 const 左值引用不能绑定到右值
    // int& r2 = x + 1;  // 编译错误！
    std::cout << "[失败] int& r2 = x + 1;      // 非常量左值引用不能绑定右值\n";
    std::cout << "       原因：x + 1 是右值，非常量 int& 不能绑定右值\n\n";

    // r3: const int& r3 = x + 1;
    // 预测：编译通过
    // 原因：const 左值引用可以绑定到右值，编译器会延长临时对象的生命周期
    const int& r3 = x + 1;
    std::cout << "[通过] const int& r3 = x + 1; // r3 = " << r3 << "\n";
    std::cout << "       原因：const 左值引用可以绑定右值（延长生命周期）\n\n";

    // r4: int&& r4 = x;
    // 预测：编译失败
    // 原因：x 是左值，右值引用（T&&）只能绑定到右值
    // int&& r4 = x;  // 编译错误！
    std::cout << "[失败] int&& r4 = x;         // 右值引用不能绑定左值\n";
    std::cout << "       原因：x 是左值，int&& 只能绑定右值\n\n";

    // r5: int&& r5 = x + 1;
    // 预测：编译通过
    // 原因：x + 1 是右值（临时表达式结果），右值引用可以绑定到右值
    int&& r5 = x + 1;
    std::cout << "[通过] int&& r5 = x + 1;     // r5 = " << r5 << "\n";
    std::cout << "       原因：x + 1 是右值，int&& 可以绑定右值\n\n";

    // r6: const int& r6 = x;
    // 预测：编译通过
    // 原因：const 左值引用可以绑定到左值（权限只减不增）
    const int& r6 = x;
    std::cout << "[通过] const int& r6 = x;    // r6 = " << r6 << "\n";
    std::cout << "       原因：const 左值引用可以绑定左值（权限收缩）\n\n";

    std::cout << "===== 总结 =====\n";
    std::cout << "  int&          -> 只能绑定左值\n";
    std::cout << "  const int&    -> 可以绑定左值和右值（万能但只读）\n";
    std::cout << "  int&&         -> 只能绑定右值（C++11）\n";

    return 0;
}
