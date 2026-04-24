#include <string>
#include <string_view>
#include <iostream>

std::string_view get_name() {
    std::string s = "Alice";
    return std::string_view{s};  // UB！s 在函数返回后销毁
}

int main() {
    auto name = get_name();
    // name 指向已释放的栈内存——未定义行为
    std::cout << name << "\n";  // 可能输出乱码、空字符串、或者 crash
}
