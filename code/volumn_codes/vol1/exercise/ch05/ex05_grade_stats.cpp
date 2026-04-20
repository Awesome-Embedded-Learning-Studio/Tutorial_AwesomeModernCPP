/**
 * @file ex05_grade_stats.cpp
 * @brief 练习：成绩排序与统计
 *
 * 使用 std::array<int, 8> 存储 8 名学生的成绩，
 * 用 std::sort 排序后输出最高分、最低分和平均分。
 */

#include <algorithm>
#include <array>
#include <iostream>
#include <numeric>

int main() {
    std::cout << "===== 成绩排序与统计 =====\n\n";

    std::array<int, 8> grades = {85, 92, 78, 95, 88, 76, 91, 83};

    // 打印原始成绩
    std::cout << "原始成绩: [";
    for (std::size_t i = 0; i < grades.size(); ++i) {
        std::cout << grades[i];
        if (i + 1 < grades.size()) {
            std::cout << ", ";
        }
    }
    std::cout << "]\n";

    // 使用 std::sort 排序（默认升序）
    std::sort(grades.begin(), grades.end());

    std::cout << "升序排序: [";
    for (std::size_t i = 0; i < grades.size(); ++i) {
        std::cout << grades[i];
        if (i + 1 < grades.size()) {
            std::cout << ", ";
        }
    }
    std::cout << "]\n";

    // 使用 <algorithm> 和 <numeric> 进行统计
    int min_val = grades.front();    // 排序后首元素即最小
    int max_val = grades.back();     // 排序后末元素即最大
    int sum = std::accumulate(grades.begin(), grades.end(), 0);
    double avg = static_cast<double>(sum) / static_cast<double>(grades.size());

    std::cout << "\n统计结果:\n";
    std::cout << "  最高分: " << max_val << "\n";
    std::cout << "  最低分: " << min_val << "\n";
    std::cout << "  总  分: " << sum << "\n";
    std::cout << "  平均分: " << avg << "\n";

    // 也可以用 std::min_element / std::max_element 在未排序数组上查找
    std::cout << "\n使用 <algorithm> 在未排序数组上查找:\n";
    std::array<int, 8> grades2 = {85, 92, 78, 95, 88, 76, 91, 83};

    auto max_it = std::max_element(grades2.begin(), grades2.end());
    auto min_it = std::min_element(grades2.begin(), grades2.end());
    std::cout << "  max_element: " << *max_it << "\n";
    std::cout << "  min_element: " << *min_it << "\n";

    return 0;
}
