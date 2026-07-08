// 批量构造模式 + extract/replace 批量重建:绕开单元素 O(n) shift 与迭代器失效
// 来源:flat_map 实战(五)
// 编译:g++ -std=c++20 -Wall -Wextra -I. 22_bulk_build_extract.cpp -o 22_bulk_build_extract

#include "flat_map.hpp"

#include <algorithm>
#include <iostream>
#include <vector>

int main() {
    using namespace tamcpp::chrome;

    std::cout << "=== 批量构造(推荐):先填 vector,再 move 进 flat_map ===\n";
    std::vector<std::pair<int, std::string>> batch;
    batch.reserve(5);
    for (int i : {3, 1, 4, 1, 5, 9, 2, 6})
        batch.emplace_back(i, std::to_string(i));
    flat_map<int, std::string> m(std::move(batch)); // 一次 sort_and_unique,O(N log N)
    std::cout << "  size=" << m.size() << "(重复的 1 被去重)\n";
    for (const auto& [k, v] : m)
        std::cout << "  " << k << ":" << v;
    std::cout << "\n";

    std::cout << "\n=== extract + replace 批量重建 ===\n";
    // 1. extract 出 vector(对 rvalue 调)
    std::vector<std::pair<int, std::string>> raw = std::move(m).extract();
    std::cout << "  extract 后 m.size=" << m.size() << ", raw.size=" << raw.size() << "\n";

    // 2. 在 vector 上自由批量改(无有序约束,无 shift 代价)
    raw.emplace_back(7, "7");
    raw.emplace_back(8, "8");
    raw.emplace_back(0, "0");
    std::cout << "  加 3 个元素后 raw.size=" << raw.size() << "\n";

    // 3. 排序去重
    std::sort(raw.begin(), raw.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    raw.erase(std::unique(raw.begin(), raw.end(),
                          [](const auto& a, const auto& b) { return a.first == b.first; }),
              raw.end());

    // 4. replace 回 flat_map(sorted_unique 式校验)
    flat_map<int, std::string> m2;
    m2.replace(std::move(raw));
    std::cout << "  replace 后 m2.size=" << m2.size() << "\n";
    for (const auto& [k, v] : m2)
        std::cout << "  " << k << ":" << v;
    std::cout << "\n";

    std::cout << "\n=== 对比:逐个 insert 是 O(N²) 陷阱(避免!)==\n";
    std::cout << "  错误姿势:for(...) m.insert(x)  // 每次 O(n) shift,总 O(N²)\n";
    std::cout << "  正确姿势:先攒 vector(push_back 摊还 O(1)),再 move 进 flat_map\n";
    return 0;
}
