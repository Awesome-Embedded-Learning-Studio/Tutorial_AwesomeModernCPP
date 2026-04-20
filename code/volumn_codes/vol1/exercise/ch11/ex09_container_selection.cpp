/**
 * @file ex09_container_selection.cpp
 * @brief 练习：容器选择实战
 *
 * 针对四个实际场景选择最合适的 STL 容器，并用代码演示。
 * (a) 游戏角色背包——末尾频繁增删 → vector
 * (b) 拼写检查词典——频繁判断存在性 → unordered_set
 * (c) 学号-姓名映射——按学号顺序输出 → map
 * (d) 3x3 矩阵——编译期确定大小 → array
 */

#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

// ============================================================
// 场景 (a): 游戏角色背包 —— 末尾频繁增删 → vector
// ============================================================

/// @brief 演示用 vector 管理背包物品
void demo_backpack()
{
    std::cout << "--- 场景 (a): 游戏角色背包 (vector) ---\n\n";

    std::vector<std::string> backpack;
    backpack.reserve(10);  // 预分配空间

    // 捡起物品（末尾添加）
    auto pickup = [&backpack](const std::string& item) {
        backpack.push_back(item);
        std::cout << "  捡起: " << item << "\n";
    };

    // 丢弃最后一个物品（末尾删除）
    auto drop_last = [&backpack]() {
        if (!backpack.empty()) {
            std::cout << "  丢弃: " << backpack.back() << "\n";
            backpack.pop_back();
        }
    };

    pickup("铁剑");
    pickup("生命药水");
    pickup("皮甲");
    pickup("蓝宝石");

    std::cout << "  背包物品: ";
    for (const auto& item : backpack) {
        std::cout << "[" << item << "] ";
    }
    std::cout << "\n";

    drop_last();    // 丢弃蓝宝石
    pickup("红宝石");

    std::cout << "  背包物品: ";
    for (const auto& item : backpack) {
        std::cout << "[" << item << "] ";
    }
    std::cout << "\n\n";

    std::cout << "  选择理由: 背包物品按顺序排列，末尾增删是 O(1)，"
              << "vector 是最佳选择\n\n";
}

// ============================================================
// 场景 (b): 拼写检查词典 —— 频繁判断存在性 → unordered_set
// ============================================================

/// @brief 演示用 unordered_set 做拼写检查
void demo_spell_checker()
{
    std::cout << "--- 场景 (b): 拼写检查词典 (unordered_set) ---\n\n";

    std::unordered_set<std::string> dictionary = {
        "hello", "world", "the", "is", "a",
        "spell", "check", "program", "test"
    };

    auto check_word = [&dictionary](const std::string& word) {
        if (dictionary.count(word)) {
            std::cout << "  \"" << word << "\" ✓ 在词典中\n";
        } else {
            std::cout << "  \"" << word << "\" ✗ 不在词典中\n";
        }
    };

    check_word("hello");
    check_word("world");
    check_word("speling");  // 拼写错误
    check_word("program");
    check_word("wrld");     // 拼写错误

    std::cout << "\n  选择理由: 只需判断\"在不在\"，不需要有序，"
              << "unordered_set 平均 O(1) 查找\n\n";
}

// ============================================================
// 场景 (c): 学号-姓名映射 —— 按学号顺序输出 → map
// ============================================================

/// @brief 演示用 map 管理学号-姓名映射
void demo_student_registry()
{
    std::cout << "--- 场景 (c): 学号-姓名映射 (map) ---\n\n";

    std::map<int, std::string> students;

    // 插入时不需要按学号顺序
    students[2023005] = "Eve";
    students[2023001] = "Alice";
    students[2023003] = "Charlie";
    students[2023002] = "Bob";
    students[2023004] = "Diana";

    // 遍历时自动按学号排序
    std::cout << "  按学号顺序输出:\n";
    for (const auto& [id, name] : students) {
        std::cout << "    " << id << " -> " << name << "\n";
    }

    std::cout << "\n  选择理由: 需要按 key（学号）顺序输出，"
              << "map 用红黑树自动维护有序性\n\n";
}

// ============================================================
// 场景 (d): 3x3 矩阵 —— 编译期确定大小 → array
// ============================================================

/// @brief 3x3 矩阵类型
using Matrix3x3 = std::array<std::array<double, 3>, 3>;

/// @brief 打印矩阵
/// @param m  3x3 矩阵
void print_matrix(const Matrix3x3& m)
{
    for (const auto& row : m) {
        std::cout << "    ";
        for (double val : row) {
            std::cout << val << "  ";
        }
        std::cout << "\n";
    }
}

/// @brief 演示用 array 存储 3x3 矩阵
void demo_matrix()
{
    std::cout << "--- 场景 (d): 3x3 矩阵 (array) ---\n\n";

    Matrix3x3 mat = {{
        {{1.0, 2.0, 3.0}},
        {{4.0, 5.0, 6.0}},
        {{7.0, 8.0, 9.0}}
    }};

    std::cout << "  矩阵:\n";
    print_matrix(mat);

    // 用下标访问
    std::cout << "\n  mat[1][2] = " << mat[1][2] << "\n";

    // 用 std::array 的 size 方法——编译期已知
    std::cout << "  行数: " << mat.size()
              << ", 列数: " << mat[0].size() << "\n";

    std::cout << "\n  选择理由: 大小在编译期确定（3x3），"
              << "array 零开销、栈分配、与 C 数组一样高效\n\n";
}

int main()
{
    std::cout << "===== ex09: 容器选择实战 =====\n\n";

    demo_backpack();
    demo_spell_checker();
    demo_student_registry();
    demo_matrix();

    std::cout << "要点:\n";
    std::cout << "  1. 顺序存储 + 末尾增删 → vector\n";
    std::cout << "  2. 快速判断存在性 + 无需有序 → unordered_set\n";
    std::cout << "  3. 按 key 有序遍历 → map\n";
    std::cout << "  4. 编译期确定大小 → array\n";
    std::cout << "  5. 拿不准就用 vector，Bjarne Stroustrup 也这么建议\n";

    return 0;
}
