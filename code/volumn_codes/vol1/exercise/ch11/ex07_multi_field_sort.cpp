/**
 * @file ex07_multi_field_sort.cpp
 * @brief 练习：多字段排序
 *
 * 定义 Employee 结构体，包含 name、department、salary 三个字段。
 * 使用 std::sort 配合 lambda 实现多字段排序：
 * 先按部门名字典序升序，同部门内按薪资降序。
 */

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

/// @brief 员工结构体
struct Employee {
    std::string name;
    std::string department;
    int salary;
};

/// @brief 打印员工信息
/// @param e      员工
/// @param indent 缩进前缀
void print_employee(const Employee& e, const std::string& indent = "  ")
{
    std::cout << indent << e.department << " | "
              << e.name << " | 薪资: " << e.salary << "\n";
}

/// @brief 打印员工列表
/// @param staff  员工列表
/// @param title  标题
void print_staff(const std::vector<Employee>& staff, const std::string& title)
{
    std::cout << title << ":\n";
    for (const auto& e : staff) {
        print_employee(e);
    }
    std::cout << "\n";
}

int main()
{
    std::cout << "===== ex07: 多字段排序 =====\n\n";

    std::vector<Employee> staff = {
        {"Alice",   "Engineering", 95000},
        {"Bob",     "Engineering", 88000},
        {"Charlie", "Marketing",   72000},
        {"Diana",   "Engineering", 95000},
        {"Eve",     "Marketing",   68000},
        {"Frank",   "Sales",       82000},
        {"Grace",   "Sales",       76000},
        {"Henry",   "Marketing",   85000},
    };

    print_staff(staff, "原始数据");

    // --- 排序规则 1：先按部门字典序升序，同部门按薪资降序 ---
    std::sort(staff.begin(), staff.end(),
        [](const Employee& a, const Employee& b) {
            if (a.department != b.department) {
                return a.department < b.department;  // 部门升序
            }
            return a.salary > b.salary;  // 薪资降序
        });

    print_staff(staff, "按部门升序 + 薪资降序");

    // --- 排序规则 2：按薪资降序，薪资相同按姓名字典序升序 ---
    std::vector<Employee> staff2 = staff;

    std::sort(staff2.begin(), staff2.end(),
        [](const Employee& a, const Employee& b) {
            if (a.salary != b.salary) {
                return a.salary > b.salary;  // 薪资降序
            }
            return a.name < b.name;  // 姓名升序
        });

    print_staff(staff2, "按薪资降序 + 姓名升序");

    // --- 用 stable_sort 演示稳定性 ---
    std::vector<Employee> staff3 = staff;

    // 第一步：先按薪资降序
    std::stable_sort(staff3.begin(), staff3.end(),
        [](const Employee& a, const Employee& b) {
            return a.salary > b.salary;
        });

    // 第二步：再按部门升序——同部门内保持薪资降序的相对顺序
    std::stable_sort(staff3.begin(), staff3.end(),
        [](const Employee& a, const Employee& b) {
            return a.department < b.department;
        });

    print_staff(staff3, "stable_sort: 先按薪资降序，再按部门升序");

    std::cout << "要点:\n";
    std::cout << "  1. lambda 中逐级比较不同字段，实现多字段排序\n";
    std::cout << "  2. 比较函数必须满足严格弱序（用 < 或 >，不要 <= 或 >=）\n";
    std::cout << "  3. stable_sort 可保证相等元素的原始相对顺序\n";
    std::cout << "  4. 多次 stable_sort 可实现「多层优先级」排序\n";

    return 0;
}
