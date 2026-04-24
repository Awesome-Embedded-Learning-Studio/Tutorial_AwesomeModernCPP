// 验证：std::string 可以正确处理包含嵌入 NUL 的 string_view
#include <string>
#include <string_view>
#include <iostream>

int main() {
    // 创建包含嵌入 NUL 的缓冲区
    char buf[] = "hello\0world";
    size_t len = strlen("hello\0world");  // strlen 只计算到第一个 NUL

    std::cout << "strlen 结果: " << len << " (只到第一个 \\0)\n";

    // 使用 string_view 指定显式长度
    std::string_view sv(buf, 11);  // "hello\0world" 是 11 字节
    std::cout << "string_view 大小: " << sv.size() << "\n";

    // 从 string_view 构造 string
    std::string s(sv);
    std::cout << "string 大小: " << s.size() << "\n";

    // 验证内容
    std::cout << "string 字节: ";
    for (unsigned char c : s) {
        std::cout << (int)c << " ";
    }
    std::cout << "\n";

    std::cout << "结论：std::string(string_view) 可以正确处理包含嵌入 NUL 的字符串\n";
    return 0;
}
