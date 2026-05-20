// 01_first_program.cpp
// 你的第一个 C++ 程序——从 Hello World 到简单计算

#include <iostream>

int main() {
    // 1. 经典 Hello World
    std::cout << "Hello, C++!" << std::endl;

    // 2. 简单的加法计算（使用固定值，方便在线运行）
    int a = 10;
    int b = 20;
    int sum = a + b;

    std::cout << a << " + " << b << " = " << sum << std::endl;

    // 3. 摄氏转华氏
    int celsius = 25;
    int fahrenheit = celsius * 9 / 5 + 32;
    std::cout << celsius << " C = " << fahrenheit << " F" << std::endl;

    return 0;
}
