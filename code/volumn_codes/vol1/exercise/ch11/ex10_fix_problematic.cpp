/**
 * @file ex10_fix_problematic.cpp
 * @brief 练习：修正有问题的 STL 代码
 *
 * 展示常见 STL 误用，包括：
 * 1. 遍历中 erase 导致迭代器失效
 * 2. map 的 operator[] 意外插入
 * 3. 用 vector 存储大量头部插入（应用 deque）
 * 每个问题先展示错误版本（注释），再给出修正版本。
 */

#include <algorithm>
#include <deque>
#include <iostream>
#include <map>
#include <numeric>
#include <string>
#include <vector>

// ============================================================
// 问题 1: 遍历 vector 时 erase 导致迭代器失效
// ============================================================

void fix_iteration_erase()
{
    std::cout << "--- 问题 1: 遍历中 erase 迭代器失效 ---\n\n";

    // ---- 错误版本（注释）----
    // std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8};
    // for (auto it = data.begin(); it != data.end(); ++it) {
    //     if (*it % 2 == 0) {
    //         data.erase(it);   // BUG: it 已失效，++it 是未定义行为
    //     }
    // }

    // ---- 修正方式 A: 用 erase 返回值 ----
    std::vector<int> data_a = {1, 2, 3, 4, 5, 6, 7, 8};
    for (auto it = data_a.begin(); it != data_a.end(); /* 不在此 ++it */) {
        if (*it % 2 == 0) {
            it = data_a.erase(it);  // erase 返回下一个有效迭代器
        } else {
            ++it;
        }
    }

    std::cout << "  修正 A（erase 返回值）: ";
    for (int v : data_a) {
        std::cout << v << " ";
    }
    std::cout << "\n";

    // ---- 修正方式 B: remove_if + erase（推荐）----
    std::vector<int> data_b = {1, 2, 3, 4, 5, 6, 7, 8};
    auto new_end = std::remove_if(data_b.begin(), data_b.end(),
        [](int x) { return x % 2 == 0; });
    data_b.erase(new_end, data_b.end());

    std::cout << "  修正 B（remove_if + erase）: ";
    for (int v : data_b) {
        std::cout << v << " ";
    }
    std::cout << "\n\n";
}

// ============================================================
// 问题 2: map 的 operator[] 意外插入
// ============================================================

void fix_map_operator_bracket()
{
    std::cout << "--- 问题 2: map 的 operator[] 意外插入 ---\n\n";

    // ---- 错误版本（注释）----
    // std::map<std::string, int> ages = {{"Alice", 22}, {"Bob", 25}};
    // if (ages["Charlie"] > 0) {   // BUG: "Charlie" 不存在，
    //                               //      operator[] 会插入 {"Charlie", 0}
    //     // ...
    // }
    // // ages 现在有三个元素，而不是两个

    // ---- 修正: 使用 find 或 contains ----
    std::map<std::string, int> ages = {{"Alice", 22}, {"Bob", 25}};

    // 用 find 判断是否存在
    auto it = ages.find("Charlie");
    if (it != ages.end()) {
        std::cout << "  Charlie: " << it->second << "\n";
    } else {
        std::cout << "  Charlie 不存在（正确行为）\n";
    }

    // 只读访问：使用 at（存在时返回值，不存在抛异常）
    try {
        int age = ages.at("Alice");
        std::cout << "  Alice: " << age << "\n";
        ages.at("Ghost");  // 不存在，抛出 std::out_of_range
    } catch (const std::out_of_range& e) {
        std::cout << "  at(\"Ghost\") 抛出异常: " << e.what() << "\n";
    }

    // 确认 ages 只有 2 个元素（没有被意外插入）
    std::cout << "  ages.size() = " << ages.size()
              << "（应为 2，无意外插入）\n\n";
}

// ============================================================
// 问题 3: 在 vector 头部频繁插入（应用 deque）
// ============================================================

void fix_wrong_container_choice()
{
    std::cout << "--- 问题 3: 头部频繁插入用错了容器 ---\n\n";

    // ---- 错误版本（注释）----
    // std::vector<int> log;
    // for (int i = 0; i < 5; ++i) {
    //     log.insert(log.begin(), i);  // 每次 O(n)，效率低下
    // }

    // ---- 修正: 使用 deque（双端队列，头部插入 O(1)）----
    std::deque<int> log;
    for (int i = 0; i < 5; ++i) {
        log.push_front(i);  // 头部插入，O(1)
        std::cout << "  push_front(" << i << ") -> ";
        for (int v : log) {
            std::cout << v << " ";
        }
        std::cout << "\n";
    }

    std::cout << "\n  或者：如果只是要逆序结果，用 vector + push_back + reverse:\n";

    // ---- 替代方案: vector + push_back + reverse ----
    std::vector<int> log_vec;
    log_vec.reserve(5);
    for (int i = 0; i < 5; ++i) {
        log_vec.push_back(i);
    }
    std::reverse(log_vec.begin(), log_vec.end());

    std::cout << "  reverse 后: ";
    for (int v : log_vec) {
        std::cout << v << " ";
    }
    std::cout << "\n\n";
}

// ============================================================
// 问题 4: accumulate 初始值类型导致溢出
// ============================================================

void fix_accumulate_overflow()
{
    std::cout << "--- 问题 4: accumulate 初始值类型陷阱 ---\n\n";

    std::vector<int> big_values = {100000, 200000, 300000, 400000};

    // ---- 错误版本（注释）----
    // int sum = std::accumulate(big_values.begin(), big_values.end(), 0);
    // // 100000+200000+300000+400000 = 1000000，虽未溢出
    // // 但数据量更大时 int 会溢出，因为初始值 0 是 int 类型

    // ---- 修正: 使用 long long 初始值 ----
    long long sum_ll = std::accumulate(
        big_values.begin(), big_values.end(), 0LL);

    std::cout << "  accumulate(..., 0LL) = " << sum_ll << "\n";
    std::cout << "  accumulate 的初始值类型决定返回类型\n";
    std::cout << "  传 0 得 int，传 0LL 得 long long，传 0.0 得 double\n\n";
}

int main()
{
    std::cout << "===== ex10: 修正有问题的 STL 代码 =====\n\n";

    fix_iteration_erase();
    fix_map_operator_bracket();
    fix_wrong_container_choice();
    fix_accumulate_overflow();

    std::cout << "要点:\n";
    std::cout << "  1. 遍历中 erase 会使迭代器失效，用 remove_if + erase 替代\n";
    std::cout << "  2. 只读查找不要用 map[key]，用 find/at/contains\n";
    std::cout << "  3. 头部频繁插入用 deque，不要用 vector 的 insert(begin)\n";
    std::cout << "  4. accumulate 初始值类型决定返回类型，注意溢出\n";
    std::cout << "  5. 遇到「能编译但结果不对」，先检查迭代器失效\n";

    return 0;
}
