// flat_map 基础:构造(自动排序去重)、查找、operator[]、at、insert_or_assign、sorted_unique
// 来源:flat_map 实战(一)(二)(三)
// 编译:g++ -std=c++20 -Wall -Wextra -I. 19_flat_map_basic.cpp -o 19_flat_map_basic

#include "flat_map.hpp"

#include <iostream>
#include <string>

int main() {
    using namespace tamcpp::chrome;

    std::cout << "=== 构造即排序去重 ===\n";
    flat_map<int, std::string> m{{3, "c"}, {1, "a"}, {3, "dup"}, {2, "b"}};
    for (const auto& [k, v] : m)
        std::cout << "  " << k << " -> " << v << "\n";
    std::cout << "  size=" << m.size() << "(重复的 3 被去重)\n";

    std::cout << "\n=== operator[] / at / contains ===\n";
    m[5] = "e"; // 缺失插入默认,再赋值
    std::cout << "  m[5]=" << m[5] << "\n";
    std::cout << "  m.at(1)=" << m.at(1) << "\n";
    std::cout << "  contains(4)=" << m.contains(4) << ", contains(5)=" << m.contains(5) << "\n";

    std::cout << "\n=== insert_or_assign(覆写已存在)==\n";
    auto [it1, ins1] = m.insert_or_assign(2, "B");
    std::cout << "  insert_or_assign(2): inserted=" << ins1 << ", m[2]=" << m.at(2) << "\n";
    auto [it2, ins2] = m.insert_or_assign(4, "D");
    std::cout << "  insert_or_assign(4): inserted=" << ins2 << ", m[4]=" << m.at(4) << "\n";

    std::cout << "\n=== sorted_unique 构造(数据已有序,跳过排序)==\n";
    std::vector<std::pair<int, std::string>> raw{{1, "x"}, {2, "y"}, {6, "z"}};
    flat_map<int, std::string> sm(sorted_unique, std::move(raw));
    std::cout << "  sorted_unique 构造,size=" << sm.size() << ", front=" << sm.front().first
              << "\n";

    std::cout << "\n=== flat_set(key=value,std::identity)==\n";
    flat_set<int> s{{5, 3, 1, 3, 2}};
    std::cout << "  size=" << s.size() << "\n";
    for (auto& k : s)
        std::cout << "  " << k;
    std::cout << "\n";

    std::cout << "\n=== sizeof + EBO ===\n";
    std::cout << "  sizeof(flat_map<int,int>)=" << sizeof(flat_map<int, int>)
              << "(vector 三指针)\n";
    return 0;
}
