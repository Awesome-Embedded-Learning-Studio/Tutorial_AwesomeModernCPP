// 配套 05-compile-time-strings.md「先说麻烦:C++17 之前 const char* 作 NTTP 的难处」
// 演示字符串字面量为什么不能直接作 NTTP(无 linkage)
//
// 默认编译跑:有 linkage 的 constexpr 变量能作 NTTP
//   g++ -std=c++20 -Wall -Wextra nttp_cstr_pitfall.cpp -o cstr && ./cstr
// 看字面量直接作 NTTP 的报错:
//   g++ -std=c++20 -Wall -Wextra -DSHOW_LITERAL_ERROR nttp_cstr_pitfall.cpp
#include <iostream>

// 有外部链接的 constexpr 字符串变量,能作 NTTP
constexpr const char kRed[] = "red";

template <const char* Name> struct Tagged {
    static constexpr const char* name = Name;
};

#ifdef SHOW_LITERAL_ERROR
template <const char* Name> struct Bad {};
Bad<"hello"> b; // 字符串字面量无 linkage,不能作 NTTP
#else
int main() {
    std::cout << "kRed 的内容 = " << kRed << "\n";

    // OK:kRed 是有 linkage 的对象,能作模板参数(数组到指针衰减)
    Tagged<kRed> t;
    std::cout << "Tagged<kRed>::name = " << t.name << "\n";
    std::cout
        << "\n字符串字面量 \"hello\" 不能直接写进 template<...>,加 -DSHOW_LITERAL_ERROR 看报错\n";
}
#endif
