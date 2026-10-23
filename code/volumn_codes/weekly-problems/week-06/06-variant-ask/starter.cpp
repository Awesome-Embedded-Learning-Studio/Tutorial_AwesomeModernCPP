#include <string>
#include <variant>

// v 里此刻存的是 int 就返回它;存的是 string 就返回 -1
// 例:传 42 返回 42;传字符串 "hi" 返回 -1
int fetch_int(const std::variant<int, std::string>& v) {
    // 在这里写:先问清现在存的是哪个类型
    return -1;
}
