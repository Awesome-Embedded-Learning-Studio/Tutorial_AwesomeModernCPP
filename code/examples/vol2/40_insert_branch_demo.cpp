// insert_branch_demo.cpp -- if 初始化器声明的变量在 else 分支里也能用
// Standard: C++17

#include <iostream>
#include <map>
#include <string>

int main() {
    std::map<int, std::string> m{{1, "one"}, {2, "two"}};
    // 第一次插新 key
    if (auto [it, ok] = m.insert({3, "three"}); ok) {
        std::cout << "if   分支: Inserted " << it->second << '\n';
    } else {
        std::cout << "else 分支: Existing " << it->second << '\n';
    }
    // 第二次插已存在的 key
    if (auto [it, ok] = m.insert({1, "ONE"}); ok) {
        std::cout << "if   分支: Inserted " << it->second << '\n';
    } else {
        std::cout << "else 分支: Existing " << it->second << " (新值 ONE 未覆盖)\n";
    }
    return 0;
}
