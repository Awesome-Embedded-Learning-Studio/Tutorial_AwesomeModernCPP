/**
 * @file ex06_serializer.cpp
 * @brief 练习：模板全特化 — 实现 Serializer<T>
 *
 * 通用版本使用 std::ostringstream 将值序列化为字符串。
 * 为 int 提供全特化（使用 std::to_string），
 * 为 std::string 提供全特化（在字符串前后添加引号）。
 */

#include <iostream>
#include <sstream>
#include <string>

/**
 * @brief 通用序列化器——使用 ostringstream 转字符串
 *
 * @tparam T 待序列化的类型
 */
template <typename T>
struct Serializer
{
    static std::string serialize(const T& value)
    {
        std::ostringstream oss;
        oss << value;
        return oss.str();
    }
};

/**
 * @brief int 全特化——使用 std::to_string
 */
template <>
struct Serializer<int>
{
    static std::string serialize(int value)
    {
        return std::to_string(value);
    }
};

/**
 * @brief std::string 全特化——添加引号
 *
 * serialize("hi") 返回 "\"hi\""
 */
template <>
struct Serializer<std::string>
{
    static std::string serialize(const std::string& value)
    {
        return "\"" + value + "\"";
    }
};

// ============================================================
// main
// ============================================================
int main()
{
    std::cout << "===== ex06: Serializer<T> 模板特化 =====\n\n";

    // --- int 全特化 ---
    {
        std::string result = Serializer<int>::serialize(42);
        std::cout << "Serializer<int>::serialize(42):\n";
        std::cout << "  结果: \"" << result << "\"\n";
        std::cout << "  验证: " << (result == "42" ? "PASS" : "FAIL")
                  << "\n\n";
    }

    // --- std::string 全特化 ---
    {
        std::string result = Serializer<std::string>::serialize(
            std::string("hi"));
        std::cout << "Serializer<std::string>::serialize(\"hi\"):\n";
        std::cout << "  结果: " << result << "\n";
        std::cout << "  验证: "
                  << (result == "\"hi\"" ? "PASS" : "FAIL") << "\n\n";
    }

    // --- 通用版本（double）---
    {
        std::string result = Serializer<double>::serialize(3.14);
        std::cout << "Serializer<double>::serialize(3.14):\n";
        std::cout << "  结果: \"" << result << "\"\n\n";
    }

    // --- 通用版本（char）---
    {
        std::string result = Serializer<char>::serialize('X');
        std::cout << "Serializer<char>::serialize('X'):\n";
        std::cout << "  结果: \"" << result << "\"\n\n";
    }

    // --- 边界值测试 ---
    {
        std::cout << "边界值测试:\n";
        std::cout << "  serialize(0): \""
                  << Serializer<int>::serialize(0) << "\"\n";
        std::cout << "  serialize(-123): \""
                  << Serializer<int>::serialize(-123) << "\"\n";
        std::cout << "  serialize(\"\"): "
                  << Serializer<std::string>::serialize(std::string(""))
                  << "\n";
        std::cout << "  serialize(\"hello world\"): "
                  << Serializer<std::string>::serialize(
                         std::string("hello world"))
                  << "\n";
    }

    return 0;
}
