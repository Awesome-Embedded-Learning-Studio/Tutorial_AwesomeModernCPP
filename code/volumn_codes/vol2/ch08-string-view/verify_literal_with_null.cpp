// 验证：字符串字面量中的 \x00 会被解释为嵌入的 NUL 字节
#include <string_view>
#include <iostream>

int main() {
    using namespace std::literals::string_view_literals;

    // \x00 在字符串字面量中是嵌入的 NUL 字节
    auto sv = "hello\x00world"sv;

    std::cout << "大小: " << sv.size() << "\n";

    // 打印字节
    std::cout << "字节: ";
    for (unsigned char c : sv) {
        std::cout << (int)c << " ";
    }
    std::cout << "\n";

    std::cout << "结论：字符串字面量中的 \\x00 被正确解析为嵌入的 NUL 字节\n";
    return 0;
}
