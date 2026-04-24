#include <string_view>
#include <iostream>
#include <optional>
#include <utility>

/// @brief 从 "key=value" 格式的字符串中提取键值对
/// @param entry 输入字符串视图，如 "host=localhost"
/// @return 成功返回 (key, value) pair，失败返回 std::nullopt
std::optional<std::pair<std::string_view, std::string_view>>
parse_kv(std::string_view entry) {
    auto pos = entry.find('=');
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    auto key = entry.substr(0, pos);
    auto value = entry.substr(pos + 1);
    // 去掉前后空白
    while (!key.empty() && key.front() == ' ') {
        key.remove_prefix(1);
    }
    while (!key.empty() && key.back() == ' ') {
        key.remove_suffix(1);
    }
    while (!value.empty() && value.front() == ' ') {
        value.remove_prefix(1);
    }
    while (!value.empty() && value.back() == ' ') {
        value.remove_suffix(1);
    }
    if (key.empty()) {
        return std::nullopt;
    }
    return std::make_pair(key, value);
}

int main() {
    const char* raw = "  host = localhost ; port = 8080 ";
    std::string_view input(raw);
    // 手动按 ';' 分割，逐个解析键值对
    while (!input.empty()) {
        auto semi = input.find(';');
        auto segment = (semi == std::string_view::npos)
                           ? input
                           : input.substr(0, semi);
        auto result = parse_kv(segment);
        if (result) {
            std::cout << "key=[" << result->first << "] "
                      << "value=[" << result->second << "]\n";
        }
        if (semi == std::string_view::npos) {
            break;
        }
        input.remove_prefix(semi + 1);
    }
    return 0;
}
