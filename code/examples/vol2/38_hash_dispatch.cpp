// hash_dispatch.cpp -- switch 初始化器 + 编译期字符串哈希做命令分发
// Standard: C++17

#include <cstddef>
#include <cstdio>
#include <string_view>

// 编译期哈希（用户定义字面量），让 case 标签能用 "start"_hash 这种写法
constexpr std::size_t operator""_hash(const char* s, std::size_t n) {
    std::size_t h = 0;
    for (std::size_t i = 0; i < n; ++i)
        h = h * 31 + std::size_t(s[i]);
    return h;
}
constexpr std::size_t hash_string(std::string_view s) {
    std::size_t h = 0;
    for (char c : s)
        h = h * 31 + std::size_t(c);
    return h;
}

int dispatch(std::string_view input) {
    switch (auto hash = hash_string(input); hash) {
        case "start"_hash:
            return 1;
        case "stop"_hash:
            return 2;
        case "status"_hash:
            return 3;
        default:
            return 0;
    }
}

int main() {
    std::printf("dispatch(\"start\")  = %d\n", dispatch("start"));
    std::printf("dispatch(\"status\") = %d\n", dispatch("status"));
    std::printf("dispatch(\"reboot\") = %d\n", dispatch("reboot"));
    return 0;
}
