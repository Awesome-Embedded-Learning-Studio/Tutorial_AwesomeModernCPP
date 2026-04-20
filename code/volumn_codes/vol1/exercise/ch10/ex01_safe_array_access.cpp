/**
 * @file ex01_safe_array_access.cpp
 * @brief 练习：安全的数组访问
 *
 * 实现 safe_get(const vector<int>&, size_t)，
 * 越界时抛出 std::out_of_range，消息中包含 index 和 size。
 * 在 main 中测试正常访问和越界访问（try/catch）。
 */

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

/// @brief 安全地获取 vector 中指定位置的元素
/// @param vec  目标 vector
/// @param index  要访问的下标
/// @return vec[index]
/// @throw std::out_of_range  当 index >= vec.size() 时抛出
int safe_get(const std::vector<int>& vec, std::size_t index)
{
    if (index >= vec.size()) {
        throw std::out_of_range(
            "safe_get: index " + std::to_string(index)
            + " is out of range for vector of size "
            + std::to_string(vec.size()));
    }
    return vec[index];
}

int main()
{
    std::cout << "===== 安全数组访问 =====\n\n";

    std::vector<int> data = {10, 20, 30, 40, 50};
    std::cout << "vector 内容: ";
    for (int v : data) {
        std::cout << v << " ";
    }
    std::cout << "\n\n";

    // 正常访问
    std::cout << "--- 正常访问 ---\n";
    for (std::size_t i = 0; i < data.size(); ++i) {
        std::cout << "  safe_get(data, " << i << ") = "
                  << safe_get(data, i) << "\n";
    }

    // 越界访问
    std::cout << "\n--- 越界访问 ---\n";
    std::size_t bad_indices[] = {5, 100, 999999};

    for (std::size_t idx : bad_indices) {
        try {
            int val = safe_get(data, idx);
            // 不会到这里
            std::cout << "  safe_get(data, " << idx << ") = " << val << "\n";
        }
        catch (const std::out_of_range& e) {
            std::cout << "  safe_get(data, " << idx << ") 抛出异常: "
                      << e.what() << "\n";
        }
    }

    std::cout << "\n要点:\n";
    std::cout << "  1. 异常消息中应包含 index 和 size 方便调试\n";
    std::cout << "  2. try/catch 让程序不会因越界而崩溃\n";

    return 0;
}
