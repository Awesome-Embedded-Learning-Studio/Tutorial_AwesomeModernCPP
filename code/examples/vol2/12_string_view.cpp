// 12_string_view.cpp
// string_view 非拥有视图：split 分割、key-value 解析与零拷贝操作

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

std::vector<std::string_view> split(std::string_view input, char delim) {
    std::vector<std::string_view> tokens;
    while (true) {
        auto pos = input.find(delim);
        if (pos == std::string_view::npos) {
            if (!input.empty())
                tokens.push_back(input);
            break;
        }
        tokens.push_back(input.substr(0, pos));
        input.remove_prefix(pos + 1);
    }
    return tokens;
}

std::optional<std::pair<std::string_view, std::string_view>> parse_kv(std::string_view entry) {
    auto pos = entry.find('=');
    if (pos == std::string_view::npos)
        return std::nullopt;
    auto key = entry.substr(0, pos);
    auto value = entry.substr(pos + 1);
    while (!key.empty() && key.front() == ' ')
        key.remove_prefix(1);
    while (!key.empty() && key.back() == ' ')
        key.remove_suffix(1);
    while (!value.empty() && value.front() == ' ')
        value.remove_prefix(1);
    if (key.empty())
        return std::nullopt;
    return std::make_pair(key, value);
}

int main() {
    std::cout << "=== string_view 演示 ===\n\n";

    std::cout << "1. 基本操作:\n";
    std::string_view sv = "hello, world";
    std::cout << "  sv = \"" << sv << "\" (size=" << sv.size() << ")\n";
    sv.remove_prefix(7);
    std::cout << "  remove_prefix(7) -> \"" << sv << "\"\n\n";

    std::cout << "2. O(1) substr:\n";
    std::string_view original = "key1=val1;key2=val2";
    std::cout << "  substr(0, 9) = \"" << original.substr(0, 9) << "\"\n\n";

    std::cout << "3. split 分割:\n";
    auto tokens = split("name=Alice;age=30;city=Beijing", ';');
    for (auto tk : tokens) {
        std::cout << "  [" << tk << "]\n";
    }
    std::cout << "\n";

    std::cout << "4. key-value 解析:\n";
    const char* config = "host=localhost;port=8080";
    for (auto seg : split(config, ';')) {
        auto result = parse_kv(seg);
        if (result) {
            std::cout << "  key=[" << result->first << "] value=[" << result->second << "]\n";
        }
    }

    return 0;
}
