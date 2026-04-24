// 验证 lambda 对结构化绑定变量的捕获
// 修正文档中的错误断言：C++17 就可以直接捕获结构化绑定变量

#include <iostream>
#include <map>
#include <string>

int main() {
    std::map<int, std::string> m = {{1, "one"}, {2, "two"}};

    // C++17 中可以直接捕获结构化绑定变量
    for (const auto& [k, v] : m) {
        auto callback1 = [k, v] {  // 直接捕获，在 C++17 中就能工作
            std::cout << k << ": " << v << '\n';
        };
        callback1();
    }

    // C++20 的初始化捕获也能工作
    for (const auto& [k, v] : m) {
        auto callback2 = [key = k, value = v] {
            std::cout << key << ": " << value << '\n';
        };
        callback2();
    }

    // 但 [=] 不会自动捕获结构化绑定变量（这点文档是正确的）
    for (const auto& [k, v] : m) {
        // auto callback3 = [=] {  // 编译错误：k 和 v 未被捕获
        //     std::cout << k << ": " << v << '\n';
        // };
        // callback3();
    }

    return 0;
}
