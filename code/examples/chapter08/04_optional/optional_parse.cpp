// optional_parse.cpp
// 使用 optional 的解析函数示例

#include <optional>
#include <string>
#include <iostream>

std::optional<int> parse_int(const std::string& s) {
    try {
        size_t idx;
        int v = std::stoi(s, &idx);
        if (idx != s.size()) return std::nullopt; // 有多余字符
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

int main() {
    std::cout << "=== parse_int with std::optional ===\n\n";

    // 成功解析
    auto r1 = parse_int("123");
    if (r1) {
        std::cout << "Parsed '123': " << r1.value() << "\n";
    }

    // 有多余字符
    auto r2 = parse_int("123abc");
    if (!r2) {
        std::cout << "Failed to parse '123abc' (extra chars)\n";
    }

    // 完全无效
    auto r3 = parse_int("abc");
    if (!r3) {
        std::cout << "Failed to parse 'abc' (invalid)\n";
    }

    // 使用 value_or
    int result = parse_int("42").value_or(-1);
    std::cout << "\nparse_int(\"42\").value_or(-1): " << result << "\n";

    result = parse_int("invalid").value_or(-1);
    std::cout << "parse_int(\"invalid\").value_or(-1): " << result << "\n";

    return 0;
}
