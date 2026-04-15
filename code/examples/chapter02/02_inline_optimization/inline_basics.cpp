// inline 基础示例：inline 关键字的作用

#include <iostream>

// 头文件中定义的函数，默认是 inline 的
// 因为它需要在多个翻译单元中定义而不违反 ODR
inline int add(int a, int b) {
    return a + b;
}

// 简单的函数，即使不写 inline，编译器也可能自动内联
int multiply(int a, int b) {
    return a * b;
}

// 复杂的函数，即使写了 inline，编译器也可能不内联
inline int complex_calculation(int n) {
    int sum = 0;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            sum += i * j;
        }
    }
    return sum;
}

// 模板函数天然具有 inline 属性
template<typename T>
T divide(T a, T b) {
    return a / b;
}

int main() {
    std::cout << "=== inline 基础示例 ===" << std::endl;

    std::cout << "\n--- 函数调用 ---" << std::endl;
    std::cout << "add(3, 4) = " << add(3, 4) << std::endl;
    std::cout << "multiply(5, 6) = " << multiply(5, 6) << std::endl;
    std::cout << "divide(10.0, 2.0) = " << divide(10.0, 2.0) << std::endl;

    std::cout << "\n复杂计算（可能不内联）: " << complex_calculation(10) << std::endl;

    std::cout << "\n关键点：" << std::endl;
    std::cout << "1. inline 关键字不保证函数会被内联" << std::endl;
    std::cout << "2. inline 的真正作用是允许函数在多个翻译单元中定义" << std::endl;
    std::cout << "3. 编译器会根据函数大小、调用频率等决定是否内联" << std::endl;
    std::cout << "4. 模板函数天然具有 inline 属性" << std::endl;

    std::cout << "\n验证方法：" << std::endl;
    std::cout << "使用 g++ -O2 -S -fverbose-asm 查看生成的汇编代码" << std::endl;
    std::cout << "对比是否生成了 call 指令" << std::endl;

    return 0;
}
