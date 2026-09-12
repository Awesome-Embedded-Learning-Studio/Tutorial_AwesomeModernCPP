#include <iostream>
#include <string>

std::string make_greeting() {
    return "hello, modern cpp";
}

int main() {
    // 修复:让字符串自己活过使用点,而不是借临时对象的视图
    std::string greeting = make_greeting();
    std::cout << greeting << '\n';
}
