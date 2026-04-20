// static_demo.cpp
// static 成员综合演练：自动 ID 分配、实例计数、静态常量

#include <iostream>
#include <string>

class Employee {
private:
    int id_;
    std::string name_;
    static int next_id_;
    static int active_count_;

public:
    static constexpr int kMaxNameLength = 50;

    explicit Employee(const std::string& name)
        : id_(next_id_++), name_(name)
    {
        ++active_count_;
        std::cout << "[construct] Employee #" << id_
                  << " \"" << name_ << "\" created. "
                  << "Active: " << active_count_ << std::endl;
    }

    ~Employee()
    {
        --active_count_;
        std::cout << "[destruct]  Employee #" << id_
                  << " \"" << name_ << "\" destroyed. "
                  << "Active: " << active_count_ << std::endl;
    }

    int id() const { return id_; }
    const std::string& name() const { return name_; }

    static int get_active_count() { return active_count_; }
    static int peek_next_id() { return next_id_; }
};

int Employee::next_id_ = 1;
int Employee::active_count_ = 0;

/// @brief 创建一些临时对象，观察计数变化
void demo_scope()
{
    std::cout << "\n--- Enter demo_scope ---" << std::endl;
    Employee temp1("Zhang San");
    Employee temp2("Li Si");
    std::cout << "Inside scope, active count: "
              << Employee::get_active_count() << std::endl;
    std::cout << "--- Leave demo_scope ---" << std::endl;
    // temp1, temp2 离开作用域，析构
}

int main()
{
    std::cout << "=== Static Member Demo ===" << std::endl;
    std::cout << "Max name length: " << Employee::kMaxNameLength << std::endl;
    std::cout << "Next ID before any creation: "
              << Employee::peek_next_id() << std::endl;

    Employee emp1("Wang Wu");
    Employee emp2("Zhao Liu");

    std::cout << "\nCurrent active count: "
              << Employee::get_active_count() << std::endl;
    std::cout << "Next ID to be assigned: "
              << Employee::peek_next_id() << std::endl;

    demo_scope();

    std::cout << "\nAfter demo_scope, active count: "
              << Employee::get_active_count() << std::endl;
    std::cout << "Next ID to be assigned: "
              << Employee::peek_next_id() << std::endl;

    return 0;
}
