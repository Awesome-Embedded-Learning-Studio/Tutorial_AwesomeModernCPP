/**
 * @file ex01_frequency_count.cpp
 * @brief 练习：频率统计
 *
 * 给定 vector<int>{3,1,4,1,5,9,2,6,5,3,5}，
 * 先排序再线性扫描统计每个值的出现次数。
 */

#include <algorithm>
#include <iostream>
#include <vector>

/// @brief 统计已排序 vector 中每个值的频率
/// @param sorted_data 已排序的数据
void print_frequency(const std::vector<int>& sorted_data)
{
    if (sorted_data.empty()) {
        return;
    }

    int current_value = sorted_data[0];
    int count = 1;

    for (std::size_t i = 1; i < sorted_data.size(); ++i) {
        if (sorted_data[i] == current_value) {
            ++count;
        } else {
            std::cout << "  " << current_value << " -> "
                      << count << " 次\n";
            current_value = sorted_data[i];
            count = 1;
        }
    }
    // 输出最后一组
    std::cout << "  " << current_value << " -> "
              << count << " 次\n";
}

int main()
{
    std::cout << "===== 频率统计 =====\n\n";

    std::vector<int> data = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5};

    std::cout << "原始数据: ";
    for (int v : data) {
        std::cout << v << " ";
    }
    std::cout << "\n\n";

    // 先排序
    std::sort(data.begin(), data.end());

    std::cout << "排序后:   ";
    for (int v : data) {
        std::cout << v << " ";
    }
    std::cout << "\n\n";

    // 线性扫描统计频率
    std::cout << "频率统计:\n";
    print_frequency(data);

    std::cout << "\n要点:\n";
    std::cout << "  1. 排序后相同值相邻，一次遍历即可统计\n";
    std::cout << "  2. 时间复杂度 O(n log n)（排序主导）\n";

    return 0;
}
