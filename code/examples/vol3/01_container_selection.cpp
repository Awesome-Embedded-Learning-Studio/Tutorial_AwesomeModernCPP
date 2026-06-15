// Standard: C++20
// 容器选择：演示「按操作选容器」——按位置存 vs 按键查，呼应选择决策树
#include <iostream>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

int main() {
    std::cout << "== 按位置存：顺序容器，关心在哪插删 ==\n";
    std::vector<int> v;
    for (int i = 0; i < 5; ++i) {
        v.push_back(i); // 尾部摊还 O(1)
    }
    std::list<int> lt;
    for (int i = 0; i < 5; ++i) {
        lt.push_front(i); // 头部 O(1)
    }
    std::cout << "vector 尾插 5 个（尾部摊还 O(1)），list 头插 5 个（头部 O(1)）\n";
    std::cout << "→ 频繁头尾进出用 deque，主要尾部增长用 vector（务必 reserve）\n";

    std::cout << "\n== 按键查：关联容器，关心按什么查 ==\n";
    constexpr int N = 100'000;
    std::map<int, int> om;
    std::unordered_map<int, int> um;
    for (int i = 0; i < N; ++i) {
        om[i] = i;
        um[i] = i;
    }
    std::cout << "map.find(N/2) 命中: " << (om.find(N / 2) != om.end())
              << "（O(log n) 红黑树，可有序遍历）\n";
    std::cout << "unordered_map.find(N/2) 命中: " << (um.find(N / 2) != um.end())
              << "（平均 O(1) 哈希，最快）\n";
    std::cout << "→ 要有序遍历用 map，只要快查用 unordered（记得 reserve）\n";

    std::cout << "\n== 决策三问（挑容器先问这三件事）==\n";
    std::cout << "1) 大小编译期已知且不变？→ array\n";
    std::cout << "2) 按键查找？→ 有序遍历用 map/set，否则 unordered（平均 O(1)）\n";
    std::cout << "3) 按位置存？→ 头尾用 deque，尾部用 vector，已知位置频繁增删用 list\n";
    std::cout << "拿不准就 vector：连续、尾部摊还 O(1)、接口最全，覆盖面最广的安全牌\n";
    return 0;
}
