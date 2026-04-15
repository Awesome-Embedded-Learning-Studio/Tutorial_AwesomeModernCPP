// parse_example.cpp
// 使用 expected 进行错误处理的示例

#include "expected.hpp"
#include <string>
#include <iostream>

// 解析整数，返回 expected<int, string>
expected<int, std::string> parse_int(const std::string& s) {
    try {
        size_t pos;
        int v = std::stoi(s, &pos);
        if (pos != s.size()) return unexpected<std::string>{"trailing chars"};
        return v;
    } catch (...) {
        return unexpected<std::string>{"not a number"};
    }
}

// 验证范围，返回 expected<int, string>
expected<int, std::string> validate_range(int x) {
    if (x < 0) return unexpected<std::string>{"negative value"};
    if (x > 100) return unexpected<std::string>{"value too large"};
    return x;
}

int main() {
    std::cout << "=== std::expected (C++17) Example ===\n\n";

    // 基本用法
    auto r = parse_int("123");
    if (r.has_value()) {
        std::cout << "Parsed: " << r.value() << "\n";
    } else {
        std::cerr << "Error: " << r.error() << "\n";
    }

    // 错误情况
    auto r2 = parse_int("abc");
    if (!r2) {
        std::cerr << "\nError parsing 'abc': " << r2.error() << "\n";
    }

    auto r3 = parse_int("123abc");
    if (!r3) {
        std::cerr << "Error parsing '123abc': " << r3.error() << "\n";
    }

    // value_or
    std::cout << "\nvalue_or demo:\n";
    std::cout << "parse_int(\"42\").value_or(-1): " << parse_int("42").value_or(-1) << "\n";
    std::cout << "parse_int(\"bad\").value_or(-1): " << parse_int("bad").value_or(-1) << "\n";

    // 链式调用
    std::cout << "\n=== Chaining ===\n";
    auto result = parse_int("50")
        .and_then(validate_range)
        .map([](int x) { return x * 2; });

    if (result) {
        std::cout << "Success: " << result.value() << "\n";
    } else {
        std::cout << "Error: " << result.error() << "\n";
    }

    // 链式调用 - 失败情况
    auto result2 = parse_int("200")
        .and_then(validate_range)
        .map([](int x) { return x * 2; });

    if (!result2) {
        std::cout << "\nError in chain: " << result2.error() << "\n";
    }

    return 0;
}
