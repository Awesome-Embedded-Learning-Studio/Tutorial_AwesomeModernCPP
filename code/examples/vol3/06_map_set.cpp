// Standard: C++20
// map / set：底层红黑树按键有序、异构查找（transparent 比较器，string_view 直接查）、extract
// 节点搬家
#include <iostream>
#include <map>
#include <string>
#include <string_view>

int main() {
    std::cout << "== map 底层红黑树：按键自动有序 ==\n";
    std::map<int, std::string> m;
    m[3] = "three";
    m[1] = "one";
    m[2] = "two";
    std::cout << "插入 3,1,2 后遍历（自动有序）：";
    for (const auto& [k, v] : m) {
        std::cout << k << ':' << v << ' ';
    }
    std::cout << '\n';

    std::cout << "\n== 异构查找：用 string_view 查 string 的 map，免构造临时 string ==\n";
    std::map<std::string, int, std::less<>> sm; // std::less<> = 透明比较器
    sm["apple"] = 1;
    sm["banana"] = 2;
    std::string_view key = "banana";
    auto it = sm.find(key); // string_view 直接查，不构造临时 string
    if (it != sm.end()) {
        std::cout << "find(string_view) 命中: " << it->second << '\n';
    }
    std::cout << "（用 std::less<> 而非 std::less<std::string>，比较器才支持异构 key）\n";

    std::cout << "\n== extract：把节点从一棵 map 搬到另一棵，零拷贝 ==\n";
    std::map<int, std::string> a{{1, "one"}, {2, "two"}};
    std::map<int, std::string> b;
    auto node = a.extract(1);  // 抽出节点（连带 string，不拷贝）
    b.insert(std::move(node)); // 接到 b
    std::cout << "extract(1) 后 a.size = " << a.size() << ", b.size = " << b.size() << '\n';
    std::cout << "（extract 搬节点不拷贝 string，适合改 key、换分配器、跨 map 转移）\n";

    std::cout << "\n== 复杂度：查找/插入/删除均 O(log n) ==\n";
    std::cout << "要有序遍历用 map/set；只要平均 O(1) 查找用 unordered 版\n";
    return 0;
}
