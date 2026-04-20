/**
 * @file ex11_perf_comparison.cpp
 * @brief 练习：性能对比
 *
 * 使用 <chrono> 计时，对比以下操作的性能差异：
 * (a) vector::push_back 有无 reserve
 * (b) vector vs list 的顺序遍历求和
 * (c) vector vs list 的排序耗时
 * (d) find（线性） vs binary_search（二分）查找
 *
 * 体会缓存友好性和算法选择对实际性能的影响。
 */

#include <algorithm>
#include <chrono>
#include <iostream>
#include <list>
#include <random>
#include <vector>

/// @brief 生成随机整数 vector
/// @param count  元素数量
/// @param seed   随机种子
/// @return       随机整数 vector
std::vector<int> generate_random_vector(std::size_t count, unsigned seed = 42)
{
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(1, 10000);

    std::vector<int> v;
    v.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        v.push_back(dist(rng));
    }
    return v;
}

/// @brief 将 vector 内容复制到 list
/// @param v  源 vector
/// @return   包含相同元素的 list
std::list<int> vector_to_list(const std::vector<int>& v)
{
    return std::list<int>(v.begin(), v.end());
}

/// @brief 计时辅助：执行 func 并返回微秒数
/// @param func  待执行的函数
/// @return      耗时（微秒）
template <typename Func>
long long measure_microseconds(Func func)
{
    auto start = std::chrono::high_resolution_clock::now();
    func();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        end - start).count();
}

// ============================================================
// 对比 (a): vector push_back 有无 reserve
// ============================================================

void compare_reserve()
{
    constexpr std::size_t kCount = 1000000;

    std::cout << "--- 对比 (a): push_back 有无 reserve ---\n\n";

    // 无 reserve
    auto ms_no_reserve = measure_microseconds([kCount]() {
        std::vector<int> v;
        for (std::size_t i = 0; i < kCount; ++i) {
            v.push_back(static_cast<int>(i));
        }
    });

    // 有 reserve
    auto ms_with_reserve = measure_microseconds([kCount]() {
        std::vector<int> v;
        v.reserve(kCount);
        for (std::size_t i = 0; i < kCount; ++i) {
            v.push_back(static_cast<int>(i));
        }
    });

    std::cout << "  插入 " << kCount << " 个元素:\n";
    std::cout << "    无 reserve:  " << ms_no_reserve << " us\n";
    std::cout << "    有 reserve:  " << ms_with_reserve << " us\n";
    if (ms_with_reserve > 0) {
        std::cout << "    加速比:      "
                  << static_cast<double>(ms_no_reserve) / ms_with_reserve
                  << "x\n";
    }
    std::cout << "\n";
}

// ============================================================
// 对比 (b): vector vs list 顺序遍历求和
// ============================================================

void compare_traversal()
{
    constexpr std::size_t kCount = 100000;

    std::cout << "--- 对比 (b): vector vs list 顺序遍历求和 ---\n\n";

    auto data_vec = generate_random_vector(kCount);
    auto data_list = vector_to_list(data_vec);

    // vector 遍历求和
    long long vec_sum = 0;
    auto ms_vec = measure_microseconds([&data_vec, &vec_sum]() {
        vec_sum = 0;
        for (int v : data_vec) {
            vec_sum += v;
        }
    });

    // list 遍历求和
    long long list_sum = 0;
    auto ms_list = measure_microseconds([&data_list, &list_sum]() {
        list_sum = 0;
        for (int v : data_list) {
            list_sum += v;
        }
    });

    std::cout << "  遍历 " << kCount << " 个元素求和:\n";
    std::cout << "    vector: " << ms_vec << " us（sum = " << vec_sum << "）\n";
    std::cout << "    list:   " << ms_list << " us（sum = " << list_sum << "）\n";
    if (ms_vec > 0) {
        std::cout << "    list / vector = "
                  << static_cast<double>(ms_list) / ms_vec << "x\n";
    }
    std::cout << "\n";
}

// ============================================================
// 对比 (c): vector vs list 排序
// ============================================================

void compare_sort()
{
    constexpr std::size_t kCount = 100000;

    std::cout << "--- 对比 (c): vector vs list 排序耗时 ---\n\n";

    auto data_vec = generate_random_vector(kCount);
    auto data_list = vector_to_list(data_vec);

    // vector 排序
    auto ms_vec = measure_microseconds([&data_vec]() {
        std::sort(data_vec.begin(), data_vec.end());
    });

    // list 排序（list 自带的 sort 成员函数）
    auto ms_list = measure_microseconds([&data_list]() {
        data_list.sort();
    });

    std::cout << "  排序 " << kCount << " 个元素:\n";
    std::cout << "    vector (std::sort):  " << ms_vec << " us\n";
    std::cout << "    list (list::sort):   " << ms_list << " us\n";
    if (ms_vec > 0) {
        std::cout << "    list / vector = "
                  << static_cast<double>(ms_list) / ms_vec << "x\n";
    }
    std::cout << "\n";
}

// ============================================================
// 对比 (d): 线性查找 vs 二分查找
// ============================================================

void compare_search()
{
    constexpr std::size_t kCount = 100000;
    constexpr int kSearchTrials = 1000;

    std::cout << "--- 对比 (d): 线性查找 vs 二分查找 ---\n\n";

    auto data = generate_random_vector(kCount);
    std::sort(data.begin(), data.end());  // 先排序

    // 生成待查找的目标值
    std::mt19937 rng(123);
    std::uniform_int_distribution<int> dist(1, 10000);
    std::vector<int> targets;
    targets.reserve(kSearchTrials);
    for (int i = 0; i < kSearchTrials; ++i) {
        targets.push_back(dist(rng));
    }

    // 线性查找（std::find）
    int linear_found = 0;
    auto ms_linear = measure_microseconds([&]() {
        for (int t : targets) {
            auto it = std::find(data.begin(), data.end(), t);
            if (it != data.end()) {
                ++linear_found;
            }
        }
    });

    // 二分查找（std::binary_search）
    int binary_found = 0;
    auto ms_binary = measure_microseconds([&]() {
        for (int t : targets) {
            if (std::binary_search(data.begin(), data.end(), t)) {
                ++binary_found;
            }
        }
    });

    std::cout << "  在 " << kCount << " 个元素中查找 "
              << kSearchTrials << " 次:\n";
    std::cout << "    std::find (O(n)):          " << ms_linear << " us"
              << "（找到 " << linear_found << " 个）\n";
    std::cout << "    std::binary_search (O(log n)): " << ms_binary << " us"
              << "（找到 " << binary_found << " 个）\n";
    if (ms_binary > 0) {
        std::cout << "    线性 / 二分 = "
                  << static_cast<double>(ms_linear) / ms_binary << "x\n";
    }
    std::cout << "\n";
}

int main()
{
    std::cout << "===== ex11: 性能对比 =====\n\n";

    compare_reserve();
    compare_traversal();
    compare_sort();
    compare_search();

    std::cout << "要点:\n";
    std::cout << "  1. reserve 能消除 vector 扩容开销，数据量大时效果显著\n";
    std::cout << "  2. vector 连续内存带来缓存友好，遍历通常比 list 更快\n";
    std::cout << "  3. 即使理论复杂度相同，缓存机制也会影响实际性能\n";
    std::cout << "  4. 有序数据上二分查找 O(log n) 远优于线性查找 O(n)\n";
    std::cout << "  5. <chrono> 是 C++ 标准的计时工具，精度可达纳秒级\n";

    return 0;
}
