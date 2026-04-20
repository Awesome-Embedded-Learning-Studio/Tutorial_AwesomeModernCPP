/**
 * @file ex06_set_operations.cpp
 * @brief 练习：集合运算
 *
 * 使用 std::set 手动实现并集、交集、差集运算。
 * 遍历其中一个 set，用 contains 或 find 在另一个 set 中查找。
 */

#include <iostream>
#include <set>
#include <string>

/// @brief 计算两个集合的并集
/// @param a 集合 A
/// @param b 集合 B
/// @return  A ∪ B
std::set<int> set_union(const std::set<int>& a, const std::set<int>& b)
{
    std::set<int> result = a;  // 先拷贝 A 的所有元素
    for (int elem : b) {
        result.insert(elem);   // set 自动去重
    }
    return result;
}

/// @brief 计算两个集合的交集
/// @param a 集合 A
/// @param b 集合 B
/// @return  A ∩ B
std::set<int> set_intersection(const std::set<int>& a, const std::set<int>& b)
{
    std::set<int> result;
    for (int elem : a) {
        if (b.count(elem)) {   // 也可以用 b.find(elem) != b.end()
            result.insert(elem);
        }
    }
    return result;
}

/// @brief 计算两个集合的差集（A - B：在 A 中但不在 B 中）
/// @param a 集合 A
/// @param b 集合 B
/// @return  A - B
std::set<int> set_difference(const std::set<int>& a, const std::set<int>& b)
{
    std::set<int> result;
    for (int elem : a) {
        if (!b.count(elem)) {
            result.insert(elem);
        }
    }
    return result;
}

/// @brief 打印集合内容
/// @param s     集合
/// @param label  标签
void print_set(const std::set<int>& s, const std::string& label)
{
    std::cout << label << " = { ";
    bool first = true;
    for (int elem : s) {
        if (!first) {
            std::cout << ", ";
        }
        std::cout << elem;
        first = false;
    }
    std::cout << " }\n";
}

int main()
{
    std::cout << "===== ex06: 集合运算 =====\n\n";

    std::set<int> a = {1, 2, 3, 4, 5, 6};
    std::set<int> b = {4, 5, 6, 7, 8, 9};

    print_set(a, "A");
    print_set(b, "B");
    std::cout << "\n";

    // 并集
    auto uni = set_union(a, b);
    print_set(uni, "A ∪ B（并集）");

    // 交集
    auto inter = set_intersection(a, b);
    print_set(inter, "A ∩ B（交集）");

    // 差集
    auto diff_ab = set_difference(a, b);
    print_set(diff_ab, "A - B（差集）");

    auto diff_ba = set_difference(b, a);
    print_set(diff_ba, "B - A（差集）");

    std::cout << "\n--- 验证基本性质 ---\n";

    // |A ∪ B| = |A| + |B| - |A ∩ B|
    std::cout << "|A ∪ B| = " << uni.size()
              << "  (|A| + |B| - |A ∩ B| = "
              << a.size() << " + " << b.size()
              << " - " << inter.size()
              << " = " << (a.size() + b.size() - inter.size()) << ")\n";

    // (A - B) ∪ (A ∩ B) = A
    auto reconstructed = set_union(diff_ab, inter);
    std::cout << "(A - B) ∪ (A ∩ B) ";
    std::cout << (reconstructed == a ? "==" : "!=") << " A  ✓\n";

    std::cout << "\n要点:\n";
    std::cout << "  1. set 自动去重，insert 时重复元素会被忽略\n";
    std::cout << "  2. 用 count() 或 find() 判断元素是否在另一个集合中\n";
    std::cout << "  3. set 的元素始终有序，方便调试和输出\n";
    std::cout << "  4. 每种运算的时间复杂度为 O(|A| log |B|)\n";

    return 0;
}
