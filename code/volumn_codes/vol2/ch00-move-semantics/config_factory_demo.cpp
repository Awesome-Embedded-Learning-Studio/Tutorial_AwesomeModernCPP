#include <iostream>
#include <string>
#include <map>

using Config = std::map<std::string, std::string>;

/// @brief 将配置映射转换为可读的字符串
/// NRVO 场景：返回命名局部变量
std::string format_config_nrvo(const Config& cfg)
{
    std::string result;
    result.reserve(256);  // 预分配，避免多次扩容

    for (const auto& [key, value] : cfg) {
        result += key;
        result += " = ";
        result += value;
        result += "\n";
    }

    return result;  // NRVO：result 直接在调用者空间构造
}

/// @brief 构建一条简单的配置行
/// RVO 场景：返回 prvalue
std::string make_config_line(const std::string& key, const std::string& value)
{
    return key + " = " + value + "\n";  // C++17 保证消除
}

/// @brief 条件返回——NRVO 可能失效的例子
std::string format_with_default(
    const Config& cfg,
    const std::string& key,
    const std::string& default_value)
{
    auto it = cfg.find(key);
    if (it != cfg.end()) {
        return it->first + " = " + it->second + "\n";  // prvalue，保证消除
    }
    return key + " = " + default_value + " (default)\n";  // prvalue，保证消除
}

int main()
{
    Config cfg = {
        {"host", "localhost"},
        {"port", "8080"},
        {"debug", "true"},
    };

    std::string formatted = format_config_nrvo(cfg);
    std::cout << formatted;

    std::string line = make_config_line("timeout", "30");
    std::cout << line;

    std::string fallback = format_with_default(cfg, "timeout", "60");
    std::cout << fallback;

    return 0;
}
