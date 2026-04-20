// constructors.cpp
// 构造函数综合演练：默认构造、参数化构造、拷贝构造、委托构造

#include <iostream>
#include <string>

class Student {
private:
    std::string name_;
    int age_;
    double score_;

public:
    Student() : name_("Unknown"), age_(0), score_(0.0)
    {
        std::cout << "[默认构造] " << name_ << ", "
                  << age_ << " 岁, " << score_ << " 分" << std::endl;
    }

    Student(const std::string& name, int age, double score)
        : name_(name), age_(age), score_(score)
    {
        std::cout << "[参数化构造] " << name_ << ", "
                  << age_ << " 岁, " << score_ << " 分" << std::endl;
    }

    // 委托构造：只用名字，其余委托给上面的参数化构造
    Student(const std::string& name) : Student(name, 18, 0.0)
    {
        std::cout << "[委托构造] 只指定姓名" << std::endl;
    }

    Student(const Student& other)
        : name_(other.name_), age_(other.age_), score_(other.score_)
    {
        std::cout << "[拷贝构造] 复制: " << name_ << std::endl;
    }

    void print() const
    {
        std::cout << "  " << name_ << ", " << age_
                  << " 岁, " << score_ << " 分" << std::endl;
    }
};

/// @brief 按值传递，触发拷贝构造
void enroll(Student s)
{
    std::cout << "  注册: ";
    s.print();
}

int main()
{
    std::cout << "=== 默认构造 ===" << std::endl;
    Student s1;
    s1.print();

    std::cout << "\n=== 参数化构造 ===" << std::endl;
    Student s2("Alice", 20, 92.5);
    s2.print();

    std::cout << "\n=== 委托构造 ===" << std::endl;
    Student s3("Bob");
    s3.print();

    std::cout << "\n=== 拷贝构造（拷贝初始化）===" << std::endl;
    Student s4 = s2;
    s4.print();

    std::cout << "\n=== 拷贝构造（按值传参）===" << std::endl;
    enroll(s2);

    return 0;
}
