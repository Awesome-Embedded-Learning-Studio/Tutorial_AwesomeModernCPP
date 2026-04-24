// 验证 TRY 宏的实现（模拟 Rust 的 ? 操作符）
// 编译: g++ -std=c++23 -Wall -Wextra verify_try_macro.cpp -o verify_try_macro
// 运行: ./verify_try_macro
//
// TRY 宏使用 GCC/Clang 的 statement expression 语法
// 如果需要在 MSVC 上使用，需要使用可移植版本（见文章中的 TRY_OUT 宏）

#include <expected>
#include <iostream>
#include <string>

enum class ConfigError {
    kFileNotFound,
    kParseError,
};

std::expected<std::string, ConfigError> read_file(const std::string& path) {
    // 模拟：假设总是成功
    return "config content";
}

std::expected<int, ConfigError> parse_config(const std::string& content) {
    // 模拟：解析成功
    return 42;
}

// TRY 宏：使用 GCC/Clang 的 statement expression 语法
// 如果表达式返回错误，直接向上传播错误；否则返回值
#define TRY(expr)                                           \
    ({                                                      \
        auto _result = (expr);                              \
        if (!_result) return std::unexpected(_result.error()); \
        std::move(_result.value());                         \
    })

// 使用 TRY 宏的函数
std::expected<int, ConfigError> load_config(const std::string& path) {
    // 如果 read_file 失败，错误会自动传播
    auto content = TRY(read_file(path));

    // 如果 parse_config 失败，错误会自动传播
    auto config = TRY(parse_config(content));

    return config;
}

int main() {
    auto result = load_config("test.cfg");
    if (result) {
        std::cout << "Config loaded successfully: " << *result << "\n";
    } else {
        std::cout << "Failed to load config\n";
    }

    return 0;
}
