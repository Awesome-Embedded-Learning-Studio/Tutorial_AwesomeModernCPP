#include <iostream>
#include <vector>
#include <list>
#include <concepts>
#include <algorithm>
#include <iterator>

// 自定义 concept：随机访问迭代器范围
template<typename Iter>
concept RandomAccessRange =
    std::random_access_iterator<Iter> && std::sentinel_for<Iter, Iter>;

// 修正版：使用 requires 子句替代模板参数约束
template<std::random_access_iterator It, typename Comp = std::less<>>
    requires std::sortable<It, Comp>
void my_sort(It first, It last, Comp comp = {}) {
    std::sort(first, last, comp);
}

// 使用自定义 concept 的版本
template<RandomAccessRange Iter, typename Comp = std::less<>>
    requires std::indirect_strict_weak_order<Comp, Iter>
void safe_sort(Iter first, Iter last, Comp comp = {}) {
    std::sort(first, last, comp);
}

int main() {
    std::vector<int> v = {5, 3, 1, 4, 2};
    safe_sort(v.begin(), v.end());
    for (int x : v) std::cout << x << " ";
    std::cout << "\n";

    my_sort(v.begin(), v.end(), std::greater<>{});
    for (int x : v) std::cout << x << " ";
    std::cout << "\n";

    // 以下代码编译失败，list 迭代器不满足 random_access_iterator
    // std::list<int> lst = {5, 3, 1, 4, 2};
    // safe_sort(lst.begin(), lst.end());

    return 0;
}
