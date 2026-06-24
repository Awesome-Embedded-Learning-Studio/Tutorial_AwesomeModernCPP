// Standard: C++20
// 二分 vs 线性查找：一千万个有序元素，最坏情况（目标在末尾）对比
// std::find（O(n)）和 std::binary_search（O(log n)）。
// 线性 find 扫到底落毫秒级，二分几次比较落微秒级——这是「有序」的红利（前提是真的有序）。
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

int main() {
    constexpr int kN = 10'000'000;
    std::vector<int> v(kN);
    for (int i = 0; i < kN; ++i)
        v[i] = i; // 已升序

    int target = kN - 1; // 最坏情况：在末尾

    auto t1 = std::chrono::high_resolution_clock::now();
    bool found_lin = std::find(v.begin(), v.end(), target) != v.end();
    auto t2 = std::chrono::high_resolution_clock::now();
    bool found_bin = std::binary_search(v.begin(), v.end(), target);
    auto t3 = std::chrono::high_resolution_clock::now();

    auto ns_lin = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
    auto ns_bin = std::chrono::duration_cast<std::chrono::nanoseconds>(t3 - t2).count();

    std::cout << "find          (O(n))      " << found_lin << "  耗时 " << ns_lin << " ns\n";
    std::cout << "binary_search (O(log n))  " << found_bin << "  耗时 " << ns_bin << " ns\n";
    std::cout << "倍数差距: " << (ns_bin > 0 ? ns_lin / ns_bin : -1) << "x\n";
    return 0;
}
