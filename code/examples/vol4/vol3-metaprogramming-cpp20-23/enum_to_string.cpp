// 配套 06-static-reflection-basics.md「实战二:枚举转字符串」
// ⚠️ 本机 GCC16/clang22 不支持 P2996(C++26 反射)。
//    在 Godbolt 选 clang_bb_p2996(Bloomberg 实验分支)编译运行,参数 -std=c++2c -freflection-latest
#include <iostream>
#include <meta>
#include <string_view>

enum class Color { Red, Green, Blue };

// 反射版:枚举值 -> 名字,不用手写任何 switch/查表
template <typename E> constexpr std::string_view enum_to_string(E value) {
    using namespace std::meta;
    constexpr auto enumerators = define_static_array(enumerators_of(^^E));
    template for (constexpr auto enumerator : enumerators) {
        if (value == [:enumerator:]) {
            return identifier_of(enumerator);
        }
    }
    return "<unknown>";
}

int main() {
    std::cout << enum_to_string(Color::Red) << "\n";
    std::cout << enum_to_string(Color::Green) << "\n";
    std::cout << enum_to_string(Color::Blue) << "\n";
}
