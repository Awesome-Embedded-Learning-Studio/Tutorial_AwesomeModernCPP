/**
 * @file ex05_icomparable.cpp
 * @brief 练习：模板接口 IComparable<T>
 *
 * IComparable<T> 定义纯虚 compare_to，Student 实现该接口，
 * 按 ID 排序。演示接口多态与模板的结合使用。
 */

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

template <typename T>
class IComparable {
public:
    virtual ~IComparable() = default;

    // 返回 <0 表示 this < other, 0 表示相等, >0 表示 this > other
    virtual int compare_to(const T& other) const = 0;
};

class Student : public IComparable<Student> {
private:
    int id_;
    std::string name_;

public:
    Student(int id, const std::string& name)
        : id_(id), name_(name) {}

    int id() const { return id_; }
    const std::string& name() const { return name_; }

    int compare_to(const Student& other) const override
    {
        if (id_ < other.id_) return -1;
        if (id_ > other.id_) return 1;
        return 0;
    }

    void print() const
    {
        std::cout << "  Student(id=" << id_
                  << ", name=\"" << name_ << "\")\n";
    }
};

// 通用的排序函数，适用于任何 IComparable<T>
template <typename T>
void sort_by_comparable(std::vector<T>& items)
{
    std::sort(items.begin(), items.end(),
        [](const T& a, const T& b) {
            return a.compare_to(b) < 0;
        });
}

// 通用的二分查找
template <typename T>
int binary_search_comparable(
    const std::vector<T>& items, const T& target)
{
    int lo = 0;
    int hi = static_cast<int>(items.size()) - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int cmp = items[mid].compare_to(target);
        if (cmp == 0) return mid;
        if (cmp < 0)  lo = mid + 1;
        else           hi = mid - 1;
    }
    return -1;
}

// ============================================================
// main
// ============================================================
int main()
{
    std::cout << "===== IComparable<Student> 接口 =====\n\n";

    std::vector<Student> students = {
        Student(105, "Eve"),
        Student(102, "Bob"),
        Student(108, "Alice"),
        Student(101, "Charlie"),
        Student(110, "Diana"),
    };

    std::cout << "--- 排序前 ---\n";
    for (const auto& s : students) {
        s.print();
    }

    sort_by_comparable(students);

    std::cout << "\n--- 按 ID 排序后 ---\n";
    for (const auto& s : students) {
        s.print();
    }

    // 二分查找
    Student target(108, "");
    int idx = binary_search_comparable(students, target);
    std::cout << "\n查找 id=108: ";
    if (idx >= 0) {
        std::cout << "找到，索引=" << idx << "\n";
        students[idx].print();
    } else {
        std::cout << "未找到\n";
    }

    // 查找不存在的
    Student missing(999, "");
    idx = binary_search_comparable(students, missing);
    std::cout << "查找 id=999: 索引=" << idx << " (未找到)\n";

    return 0;
}
