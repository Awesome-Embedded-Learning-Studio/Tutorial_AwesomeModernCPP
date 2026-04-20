// string_construct.cpp
#include <iostream>
#include <string>

int main()
{
    // 从字面量构造
    std::string s1 = "hello";
    // 重复字符：10 个 'x'
    std::string s2(10, 'x');
    // 拷贝构造
    std::string s3(s1);
    // 从另一个 string 的一部分构造（起始位置，长度）
    std::string s4(s1, 1, 3);  // "ell"
    // 用 + 直接拼接构造
    std::string s5 = s1 + " world";
    // 空字符串
    std::string s6;
    // 移动构造（C++11）
    std::string s7 = std::move(s5);

    std::cout << s1 << "\n" << s2 << "\n" << s3 << "\n"
              << s4 << "\n" << s7 << "\n"
              << "s6 empty: " << std::boolalpha << s6.empty() << "\n";
    return 0;
}
