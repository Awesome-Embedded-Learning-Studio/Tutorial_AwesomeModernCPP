// Standard: C++20
// char8_t 两坑（用注释封印，取消注释即编译失败）+ 两种正确写法
#include <iostream>
#include <string>

// 坑一：u8"" 在 C++20 起类型变为 const char8_t[]，不再隐式转 const char*
// const char* p = u8"text";   // ill-formed since C++20

// 坑二：标准库显式 =delete 了 char8_t / const char8_t* 的 ostream 插入重载
// std::cout << u8"text";      // ill-formed since C++20
// std::cout << u8'z';         // ill-formed since C++20

// 正确写法之一：显式逐字节转换（内容不变，仅切换指针类型视角）
void print_as_char(const char* s) {
    std::cout << s << '\n';
}

// 正确写法之二：用 std::u8string 类型安全地持有 UTF-8，并自定义打印
std::ostream& operator<<(std::ostream& os, const std::u8string& s) {
    return os << reinterpret_cast<const char*>(s.data());
}

int main() {
    // 路线 A：把 u8 字面量当 const char* 用（适合喂给只认窄字符的旧接口）
    print_as_char(reinterpret_cast<const char*>(u8"text"));

    // 路线 B：u8string 全程保持 UTF-8 类型，打印时再转
    std::u8string u8s = u8"UTF-8 text";
    std::cout << u8s << '\n';

    std::cout << "__cpp_char8_t = " << __cpp_char8_t << '\n';
    return 0;
}
