/**
 * @file ex09_friend_ostream.cpp
 * @brief 练习：友元 operator<<
 *
 * 实现 Student 类，包含私有 id 和 score，
 * 通过友元函数重载 operator<< 使其支持 std::cout << student。
 */

#include <iostream>
#include <string>

class Student {
private:
    int id_;
    double score_;
    std::string name_;

public:
    Student(int id, const std::string& name, double score)
        : id_(id), name_(name), score_(score) {}

    // Getter
    int id() const { return id_; }
    const std::string& name() const { return name_; }
    double score() const { return score_; }

    // 友元声明：允许 operator<< 访问私有成员
    friend std::ostream& operator<<(std::ostream& os,
                                    const Student& student);
};

// 友元函数定义：格式化输出 Student
std::ostream& operator<<(std::ostream& os, const Student& student) {
    os << "Student(id=" << student.id_
       << ", name=\"" << student.name_ << "\""
       << ", score=" << student.score_ << ")";
    return os;
}

int main() {
    std::cout << "===== 友元 operator<< =====\n\n";

    // 创建几个 Student 对象
    Student s1(1001, "Alice", 95.5);
    Student s2(1002, "Bob", 87.0);
    Student s3(1003, "Charlie", 92.3);

    // 使用 << 直接输出
    std::cout << "使用 std::cout << student:\n";
    std::cout << "  " << s1 << "\n";
    std::cout << "  " << s2 << "\n";
    std::cout << "  " << s3 << "\n\n";

    // 支持链式输出
    std::cout << "链式输出:\n  " << s1 << "\n  " << s2 << "\n\n";

    // 可以在表达式中使用
    std::cout << "结合其他输出:\n";
    std::cout << "最高分: " << s1.name() << " (" << s1.score() << ")\n";
    std::cout << "信息: " << s2 << " 成绩良好\n\n";

    std::cout << "要点:\n";
    std::cout << "  friend 声明让外部函数访问私有成员\n";
    std::cout << "  operator<< 返回 ostream& 支持链式调用\n";
    std::cout << "  友元不是成员函数，但有权访问私有成员\n";

    return 0;
}
