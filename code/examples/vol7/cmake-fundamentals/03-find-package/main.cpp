// main.cpp —— 演示 target_compile_features(cxx_std_20) 设的最低标准
//
// 这里用了一个 C++20 才有的语法:模板 lambda([]<typename T>(...) ...)
// 如果编译器实际拿到的标准低于 C++20,这一行就编不过
// 用来证明 target_compile_features 真的把标准要求传到了编译命令里

#include <iostream>
#include <string>

int main() {
    // C++20: 显式模板参数列表的 lambda
    auto add = []<typename T>(T a, T b) { return a + b; };

    std::cout << add(1, 2) << '\n';
    std::cout << add(std::string("a"), std::string("b")) << '\n';
    return 0;
}
