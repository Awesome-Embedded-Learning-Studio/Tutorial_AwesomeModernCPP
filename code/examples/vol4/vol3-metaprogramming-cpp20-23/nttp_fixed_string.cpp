// 配套 05-compile-time-strings.md「C++20 的解药:P0732 与 structural type」
// fixed_string 惯用法:把字符串包进 structural 结构体,当非类型模板参数(NTTP)用
// 编译运行:g++ -std=c++20 -Wall -Wextra nttp_fixed_string.cpp -o nfs && ./nfs
#include <iostream>

// C++20:NTTP 可以是 class type,只要它是 structural(所有基类和数据成员 public)
// fixed_string:把字符串包进结构体,从而能当模板参数
template <std::size_t N> struct FixedString {
    char value[N] = {};

    // 用 const char 数组构造(字面量 "abc" 是 const char[4],含 \0)
    constexpr FixedString(const char (&str)[N]) {
        for (std::size_t i = 0; i < N; ++i) {
            value[i] = str[i];
        }
    }

    constexpr bool operator==(const FixedString& other) const {
        for (std::size_t i = 0; i < N; ++i) {
            if (value[i] != other.value[i])
                return false;
        }
        return true;
    }

    constexpr const char* c_str() const { return value; }
};

// CTAD 推导指引:让字面量 "hello" 推导出 FixedString<6>(含 \0)
template <std::size_t N> FixedString(const char (&)[N]) -> FixedString<N>;

// 把 FixedString 当 NTTP 用
template <FixedString S> struct Named {
    static constexpr auto name = S;
};

template <FixedString S> void greet() {
    std::cout << "hello, " << S.c_str() << "\n";
}

int main() {
    std::cout << Named<"world">{}.name.c_str() << "\n";
    greet<"templates">();

    // 编译期字符串比较
    static_assert(FixedString{"abc"} == FixedString{"abc"});
    static_assert(!(FixedString{"abc"} == FixedString{"abd"}));
    std::cout << "编译期字符串比较断言通过\n";
}
