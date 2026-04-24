#include <string_view>
#include <vector>
#include <iostream>

std::vector<std::string_view> split(std::string_view input, char delim) {
    std::vector<std::string_view> tokens;
    while (true) {
        auto pos = input.find(delim);
        if (pos == std::string_view::npos) {
            if (!input.empty()) {
                tokens.push_back(input);
            }
            break;
        }
        tokens.push_back(input.substr(0, pos));
        input.remove_prefix(pos + 1);  // 跳过分隔符
    }
    return tokens;
}

int main() {
    std::string line = "name=Alice;age=30;city=Beijing";
    auto tokens = split(line, ';');
    for (auto tk : tokens) {
        std::cout << "[" << tk << "]\n";
    }
    return 0;
}
