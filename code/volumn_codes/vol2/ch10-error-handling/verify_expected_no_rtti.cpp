// 验证 std::expected 不需要 RTTI
// 编译（禁用RTTI）: g++ -fno-rtti -std=c++23 verify_expected_no_rtti.cpp -o verify_expected_no_rtti
// 运行: ./verify_expected_no_rtti
//
// 本测试证明：std::expected 可以在禁用 RTTI 的环境下正常工作
// 这使得它适用于嵌入式开发等对二进制体积敏感的场景

#include <expected>
#include <iostream>
#include <string>

enum class Error {
    kFailed,
    kInvalidInput,
};

std::expected<int, Error> get_value() {
    return 42;
}

std::expected<std::string, Error> get_string() {
    return "Hello, expected!";
}

int main() {
    auto result = get_value();
    if (result) {
        std::cout << "Value: " << *result << "\n";
    } else {
        std::cout << "Error occurred\n";
    }

    auto str_result = get_string();
    if (str_result) {
        std::cout << "String: " << *str_result << "\n";
    }

    // 注意：即使禁用了 RTTI，std::expected 仍然正常工作
    // 这是因为它不依赖 typeid 或 dynamic_cast 等 RTTI 特性

    return 0;
}
