// ex05_generic_comparator.cpp
// 练习：通用比较器函数对象
// 模板 GenericComparator 类，支持升序/降序排序，并统计比较次数。
// 编译：g++ -Wall -Wextra -std=c++17 -o ex05 ex05_generic_comparator.cpp

#include <algorithm>
#include <array>
#include <iostream>
#include <string>
#include <vector>

template <typename T>
class GenericComparator {
public:
    enum class Order { kAscending, kDescending };

private:
    Order order_;
    mutable std::size_t count_;  // mutable 允许在 const 方法中修改

public:
    explicit GenericComparator(Order order = Order::kAscending)
        : order_(order), count_(0)
    {}

    // 函数调用运算符
    bool operator()(const T& a, const T& b) const
    {
        ++count_;
        if (order_ == Order::kAscending) {
            return a < b;
        }
        return b < a;
    }

    // 获取比较次数
    std::size_t count() const { return count_; }

    // 重置计数
    void reset() { count_ = 0; }
};

// 打印容器内容的辅助函数
template <typename Container>
void print_container(const Container& c, const std::string& label)
{
    std::cout << label << ": [";
    for (std::size_t i = 0; i < c.size(); ++i) {
        std::cout << c[i];
        if (i + 1 < c.size()) {
            std::cout << ", ";
        }
    }
    std::cout << "]\n";
}

// ============================================================
// main
// ============================================================
int main()
{
    // --- int 升序排序 ---
    std::cout << "=== int 升序 ===\n";
    std::vector<int> nums = {5, 2, 8, 1, 9, 3, 7, 4, 6};

    GenericComparator<int> asc_cmp(GenericComparator<int>::Order::kAscending);
    std::sort(nums.begin(), nums.end(), asc_cmp);
    print_container(nums, "升序结果");
    std::cout << "比较次数: " << asc_cmp.count() << "\n";

    // --- int 降序排序 ---
    std::cout << "\n=== int 降序 ===\n";
    std::vector<int> nums2 = {5, 2, 8, 1, 9, 3, 7, 4, 6};

    GenericComparator<int> desc_cmp(GenericComparator<int>::Order::kDescending);
    std::sort(nums2.begin(), nums2.end(), desc_cmp);
    print_container(nums2, "降序结果");
    std::cout << "比较次数: " << desc_cmp.count() << "\n";

    // --- string 排序 ---
    std::cout << "\n=== string 升序 ===\n";
    std::vector<std::string> names = {"Charlie", "Alice", "Bob", "Diana"};

    GenericComparator<std::string> str_cmp;
    std::sort(names.begin(), names.end(), str_cmp);
    print_container(names, "字符串排序");
    std::cout << "比较次数: " << str_cmp.count() << "\n";

    // --- 重置后再次使用 ---
    std::cout << "\n=== 重置计数后 ===\n";
    str_cmp.reset();
    std::vector<std::string> names2 = {"Zoe", "Amy", "Mike"};
    std::sort(names2.begin(), names2.end(), str_cmp);
    print_container(names2, "再次排序");
    std::cout << "比较次数: " << str_cmp.count() << "\n";

    return 0;
}
