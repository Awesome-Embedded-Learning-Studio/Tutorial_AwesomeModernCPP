/**
 * @file ex02_fix_slicing.cpp
 * @brief 练习：对象切片（Object Slicing）的Bug与修复
 *
 * 演示按值传递派生类对象时发生的切片问题，
 * 然后用引用传递修复，使多态行为正确生效。
 */

#include <iostream>
#include <string>

class Person {
protected:
    std::string name_;

public:
    explicit Person(const std::string& name) : name_(name) {}
    virtual ~Person() = default;

    virtual std::string info() const
    {
        return "Person: " + name_;
    }
};

class Student : public Person {
private:
    std::string school_;

public:
    Student(const std::string& name, const std::string& school)
        : Person(name), school_(school) {}

    std::string info() const override
    {
        return "Student: " + name_ + " @ " + school_;
    }
};

// ----- 按值传递：会发生对象切片 -----
void print_info_sliced(Person p)
{
    std::cout << "  [按值] " << p.info() << "\n";
}

// ----- 按引用传递：多态正常工作 -----
void print_info_ref(const Person& p)
{
    std::cout << "  [引用] " << p.info() << "\n";
}

// ============================================================
// main
// ============================================================
int main()
{
    std::cout << "===== 对象切片与修复 =====\n\n";

    Student alice("Alice", "MIT");

    std::cout << "--- 直接调用 ---\n";
    std::cout << "  alice.info() = " << alice.info() << "\n\n";

    std::cout << "--- 按值传递 (发生切片) ---\n";
    print_info_sliced(alice);
    std::cout << "  注意：Student 的 school_ 信息被切掉了！\n";
    std::cout << "  原因：按值传递只拷贝了 Person 子对象，\n";
    std::cout << "        派生类部分被截断，vtable 也被替换。\n\n";

    std::cout << "--- 按引用传递 (多态正确) ---\n";
    print_info_ref(alice);
    std::cout << "  Student 的完整信息保留，虚函数正确分派。\n\n";

    std::cout << "--- 总结 ---\n";
    std::cout << "  修复方法：将 print_info(Person p)\n";
    std::cout << "           改为 print_info(const Person& p)\n";
    std::cout << "  按引用/指针传递是多态的基础前提。\n";

    return 0;
}
