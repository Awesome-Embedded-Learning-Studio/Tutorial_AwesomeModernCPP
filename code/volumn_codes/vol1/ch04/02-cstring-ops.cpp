#include <iostream>

int main()
{
    const char* s = "hello";

    std::cout << "字符串: " << s << "\n";
    std::cout << "首字符: " << *s << "\n";
    std::cout << "第3个字符: " << s[2] << "\n";

    // 手动计算字符串长度——模拟 strlen
    std::size_t len = 0;
    while (s[len] != '\0') {
        ++len;
    }
    std::cout << "长度: " << len << "\n";

    return 0;
}
