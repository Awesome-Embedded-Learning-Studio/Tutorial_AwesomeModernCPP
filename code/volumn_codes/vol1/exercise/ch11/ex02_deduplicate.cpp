/**
 * @file ex02_deduplicate.cpp
 * @brief 练习：手动去重
 *
 * 不使用 std::unique，手动实现已排序 vector 的去重，
 * 返回一个不含重复元素的新 vector。
 */

#include <iostream>
#include <vector>

/// @brief 对已排序的 vector 去重，返回新 vector
/// @param sorted_data 已排序的输入数据
/// @return 去重后的新 vector
std::vector<int> deduplicate(const std::vector<int>& sorted_data)
{
    std::vector<int> result;
    if (sorted_data.empty()) {
        return result;
    }

    // 始终保留第一个元素
    result.push_back(sorted_data[0]);

    for (std::size_t i = 1; i < sorted_data.size(); ++i) {
        // 只在与前一个元素不同时才添加
        if (sorted_data[i] != sorted_data[i - 1]) {
            result.push_back(sorted_data[i]);
        }
    }

    return result;
}

int main()
{
    std::cout << "===== 手动去重 =====\n\n";

    // 测试用例 1：有重复
    std::vector<int> data1 = {1, 1, 2, 3, 3, 3, 4, 5, 5, 9};
    std::cout << "输入: ";
    for (int v : data1) {
        std::cout << v << " ";
    }
    std::cout << "\n";

    auto result1 = deduplicate(data1);
    std::cout << "去重: ";
    for (int v : result1) {
        std::cout << v << " ";
    }
    std::cout << "  (" << data1.size() << " -> " << result1.size()
              << " 个元素)\n\n";

    // 测试用例 2：无重复
    std::vector<int> data2 = {1, 2, 3, 4, 5};
    std::cout << "输入: ";
    for (int v : data2) {
        std::cout << v << " ";
    }
    std::cout << "\n";

    auto result2 = deduplicate(data2);
    std::cout << "去重: ";
    for (int v : result2) {
        std::cout << v << " ";
    }
    std::cout << "  (" << data2.size() << " -> " << result2.size()
              << " 个元素)\n\n";

    // 测试用例 3：全部相同
    std::vector<int> data3 = {7, 7, 7, 7};
    std::cout << "输入: ";
    for (int v : data3) {
        std::cout << v << " ";
    }
    std::cout << "\n";

    auto result3 = deduplicate(data3);
    std::cout << "去重: ";
    for (int v : result3) {
        std::cout << v << " ";
    }
    std::cout << "  (" << data3.size() << " -> " << result3.size()
              << " 个元素)\n\n";

    // 测试用例 4：空 vector
    std::vector<int> data4;
    auto result4 = deduplicate(data4);
    std::cout << "空 vector 去重后大小: " << result4.size() << "\n\n";

    std::cout << "要点:\n";
    std::cout << "  1. 排序后的去重只需比较相邻元素\n";
    std::cout << "  2. 时间复杂度 O(n)，空间复杂度 O(n)\n";

    return 0;
}
