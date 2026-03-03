// optional_basics.cpp
// std::optional 基本用法示例

#include <optional>
#include <string>
#include <iostream>

std::optional<std::string> read_env(const std::string& key) {
    // 模拟从环境变量读取
    if (key == "HOME") return std::string("/home/alice");
    return std::nullopt;
}

int main() {
    std::cout << "=== std::optional Basics ===\n\n";

    auto maybe_home = read_env("HOME");
    if (maybe_home) {
        std::cout << "home: " << *maybe_home << "\n";
    } else {
        std::cout << "no home\n";
    }

    // value_or 提供默认值
    std::string home = maybe_home.value_or("/tmp");
    std::cout << "home-or-fallback: " << home << "\n";

    // 测试不存在的键
    auto maybe_path = read_env("NONEXISTENT");
    if (!maybe_path.has_value()) {
        std::cout << "\nNONEXISTENT key not found\n";
    }

    // value() 会抛出异常
    std::cout << "\nTrying value() on empty optional:\n";
    try {
        std::cout << maybe_path.value() << "\n";
    } catch (const std::bad_optional_access&) {
        std::cout << "Caught bad_optional_access\n";
    }

    // 演示构造方式
    std::cout << "\n=== Construction ===\n";
    std::optional<int> a;                    // empty
    std::cout << "a: " << a.has_value() << "\n";

    std::optional<int> b = 42;               // contains 42
    std::cout << "b: " << b.value() << "\n";

    std::optional<int> c = std::nullopt;     // empty
    std::cout << "c: " << c.has_value() << "\n";

    // 就地构造
    std::optional<std::string> s(std::in_place, 10, 'x');
    std::cout << "s: " << s.value() << "\n";

    // emplace
    s.emplace("hello");
    std::cout << "s after emplace: " << s.value() << "\n";

    // reset
    s.reset();
    std::cout << "s after reset: " << s.has_value() << "\n";

    return 0;
}
