/**
 * @file ex02_parse_doubles.cpp
 * @brief 练习：逗号分隔的 double 解析
 *
 * 实现 parse_doubles(const string& input)，解析逗号分隔的 double 列表。
 * 空输入抛出 std::runtime_error，格式错误抛出 std::invalid_argument。
 * 在 main 中分别 catch 两种异常。
 */

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

/// @brief 解析逗号分隔的 double 字符串
/// @param input  如 "1.5,2.0,3.14"
/// @return 解析后的 double 列表
/// @throw std::runtime_error  当 input 为空时
/// @throw std::invalid_argument  当某个字段无法转换为 double 时
std::vector<double> parse_doubles(const std::string& input)
{
    if (input.empty()) {
        throw std::runtime_error("parse_doubles: input is empty");
    }

    std::vector<double> result;
    std::stringstream ss(input);
    std::string token;
    int field_index = 0;

    while (std::getline(ss, token, ',')) {
        ++field_index;
        // 尝试转换
        try {
            std::size_t pos = 0;
            double val = std::stod(token, &pos);
            // 如果 token 中有未消费的字符，说明格式不纯
            if (pos != token.size()) {
                throw std::invalid_argument("trailing chars");
            }
            result.push_back(val);
        }
        catch (const std::invalid_argument&) {
            throw std::invalid_argument(
                "parse_doubles: field #" + std::to_string(field_index)
                + " '" + token + "' is not a valid double");
        }
        catch (const std::out_of_range&) {
            throw std::invalid_argument(
                "parse_doubles: field #" + std::to_string(field_index)
                + " '" + token + "' is out of range for double");
        }
    }

    return result;
}

int main()
{
    std::cout << "===== 逗号分隔 double 解析 =====\n\n";

    struct TestCase {
        std::string input;
        bool should_fail;
        std::string description;
    };

    TestCase tests[] = {
        {"1.5,2.0,3.14", false, "正常输入"},
        {"0,-1.2,999.999", false, "包含负数和大数"},
        {"3.14", false, "单个值"},
        {"", true, "空字符串"},
        {"1.0,abc,3.0", true, "中间字段非法"},
        {"1.0,,3.0", true, "空字段"},
        {"1.0,2.0x,3.0", true, "尾部有多余字符"},
    };

    for (const auto& test : tests) {
        std::cout << "  输入: \"" << test.input << "\" (" << test.description
                  << ")\n";
        try {
            auto values = parse_doubles(test.input);
            std::cout << "    解析成功: ";
            for (double v : values) {
                std::cout << v << " ";
            }
            std::cout << "\n";
        }
        catch (const std::runtime_error& e) {
            std::cout << "    runtime_error: " << e.what() << "\n";
        }
        catch (const std::invalid_argument& e) {
            std::cout << "    invalid_argument: " << e.what() << "\n";
        }
        std::cout << "\n";
    }

    std::cout << "要点:\n";
    std::cout << "  1. 空输入和格式错误是两类不同的问题，用不同异常类型区分\n";
    std::cout << "  2. stod 本身会抛异常，需要包装后补充上下文信息\n";

    return 0;
}
