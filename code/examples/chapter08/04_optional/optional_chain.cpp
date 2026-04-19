// optional_chain.cpp
// 使用 optional 实现链式转换

#include <optional>
#include <string>
#include <iostream>

// optional_map 工具函数
template<typename T, typename F>
auto optional_map(const std::optional<T>& opt, F f)
    -> std::optional<decltype(f(*opt))>
{
    if (opt) return f(*opt);
    return std::nullopt;
}

// 示例转换函数
std::optional<int> parse_int(const std::string& s) {
    try {
        size_t idx;
        int v = std::stoi(s, &idx);
        if (idx != s.size()) return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

double divide_by_2(int x) {
    return static_cast<double>(x) / 2.0;
}

std::string to_string(double d) {
    return std::to_string(d);
}

int main() {
    std::cout << "=== Optional Chaining ===\n\n";

    // 链式调用
    auto result = optional_map(parse_int("42"), divide_by_2);
    if (result) {
        std::cout << "42 -> divide_by_2: " << result.value() << "\n";
    }

    // 更长的链
    auto result2 = optional_map(
        optional_map(parse_int("100"), divide_by_2),
        to_string
    );
    if (result2) {
        std::cout << "100 -> divide_by_2 -> to_string: " << result2.value() << "\n";
    }

    // 失败情况
    auto result3 = optional_map(parse_int("invalid"), divide_by_2);
    if (!result3) {
        std::cout << "Parsing 'invalid' failed, chain short-circuited\n";
    }

    // 使用 lambda
    auto s = std::optional<std::string>{"42"};
    auto maybe_int = optional_map(s, [](auto& str){ return std::stoi(str); });
    std::cout << "\nString \"42\" -> int: " << maybe_int.value() << "\n";

    return 0;
}
