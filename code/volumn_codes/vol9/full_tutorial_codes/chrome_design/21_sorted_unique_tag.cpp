// sorted_unique tag dispatch:零成本跳过排序 + DCHECK 诚实契约
// 来源:flat_map 实战(四)+ 前置知识(四)
// 编译:g++ -std=c++20 -Wall -Wextra -I. 21_sorted_unique_tag.cpp -o 21_sorted_unique_tag

#include "flat_map.hpp"

#include <chrono>
#include <iostream>
#include <vector>

int main() {
    using namespace tamcpp::chrome;
    constexpr int N = 100'000;

    // 准备一份已有序无重复的数据
    std::vector<std::pair<int, int>> sorted_raw;
    sorted_raw.reserve(N);
    for (int i = 0; i < N; ++i)
        sorted_raw.emplace_back(i, i);

    std::cout << "=== 普通构造:内部 sort_and_unique(O(N log N))==\n";
    auto t0 = std::chrono::steady_clock::now();
    flat_map<int, int> m1(sorted_raw); // 普通:即便已有序也会再排一次
    auto t1 = std::chrono::steady_clock::now();
    std::cout << "  普通:       "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() << " ms\n";

    std::cout << "\n=== sorted_unique 构造:跳过排序(O(N))==\n";
    auto t2 = std::chrono::steady_clock::now();
    flat_map<int, int> m2(sorted_unique, sorted_raw); // sorted_unique:不排,只 DCHECK(debug)
    auto t3 = std::chrono::steady_clock::now();
    std::cout << "  sorted_unique: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count() << " ms\n";
    std::cout << "  (release 下 DCHECK 消失,真 O(N);debug 下 DCHECK 仍校验你诚实)\n";

    std::cout << "\n=== 诚实契约:撒谎会被 DCHECK/debug assert 抓住 ===\n";
    std::cout << "  若取消下面注释(传未排序数据却宣誓 sorted_unique),debug 下 abort:\n";
    // flat_map<int,int> bad(sorted_unique, std::vector<std::pair<int,int>>{{3,3}, {1,1}, {2,2}});

    std::cout << "\n  何时用 sorted_unique:数据来源可信地保证有序无重复\n";
    std::cout << "    - 来自另一个有序容器\n";
    std::cout << "    - 你刚 sort+unique 过\n";
    std::cout << "    - 编译期常量 initializer_list\n";
    return 0;
}
