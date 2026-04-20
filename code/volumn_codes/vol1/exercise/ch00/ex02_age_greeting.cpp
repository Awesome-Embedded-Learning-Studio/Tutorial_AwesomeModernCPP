/**
 * @file ex02_age_greeting.cpp
 * @brief 练习：读取年龄并问候
 *
 * 从 std::cin 读取用户年龄，输出问候语 "你今年 XX 岁了！"
 * 练习基本的输入和输出操作。
 */

#include <iostream>

int main() {
    std::cout << "===== ex02: 年龄问候 =====\n\n";

    // 读取用户年龄
    std::cout << "请输入你的年龄: ";
    int age = 0;
    std::cin >> age;

    // 输出问候
    std::cout << "你今年 " << age << " 岁了！\n";

    // 额外：根据年龄段给出不同反馈
    if (age < 0) {
        std::cout << "年龄不能为负数哦！\n";
    } else if (age <= 12) {
        std::cout << "你是个小朋友，加油学习！\n";
    } else if (age <= 18) {
        std::cout << "少年时期，正是学编程的好时候！\n";
    } else if (age <= 30) {
        std::cout << "青年才俊，未来可期！\n";
    } else {
        std::cout << "活到老学到老，永远不晚！\n";
    }

    return 0;
}
