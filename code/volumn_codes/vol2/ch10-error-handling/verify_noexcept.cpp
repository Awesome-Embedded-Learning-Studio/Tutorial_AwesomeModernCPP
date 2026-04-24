// 验证 noexcept 的双重身份：说明符和运算符
// 编译: g++ -std=c++17 -Wall verify_noexcept.cpp -o verify_noexcept
// 运行: ./verify_noexcept

#include <iostream>
#include <type_traits>

// noexcept 作为说明符：承诺函数不抛异常
void func_noexcept() noexcept {
    // 这个函数承诺不抛异常
}

void func_may_throw() {
    // 这个函数可能抛异常
}

// noexcept 作为运算符：编译时检查表达式是否可能抛异常
template<typename T>
void test_noexcept(T&& func) {
    std::cout << "noexcept check: " << noexcept(func()) << "\n";
}

int main() {
    std::cout << "func_noexcept: " << noexcept(func_noexcept()) << "\n";
    std::cout << "func_may_throw: " << noexcept(func_may_throw()) << "\n";

    // 条件 noexcept：根据条件决定是否 noexcept
    auto lambda = []() noexcept(noexcept(func_noexcept())) {
        func_noexcept();
    };

    std::cout << "lambda noexcept: " << noexcept(lambda()) << "\n";

    return 0;
}
