/**
 * @file ex01_hello_name.cpp
 * @brief 练习：输出你的名字
 *
 * 修改 hello.cpp，使用 std::cout 输出 "你好，<你的名字>！"
 * 练习基本的输出操作。
 */

#include <iostream>
#include <string>

int main() {
    std::cout << "===== ex01: 你好名字 =====\n\n";

    // 方式一：直接输出字符串
    std::cout << "你好，C++ 学习者！\n";

    // 方式二：通过变量拼接
    const std::string kName = "Charlie";
    std::cout << "你好，" << kName << "！\n";

    // 方式三：从用户输入读取名字
    std::cout << "请输入你的名字: ";
    std::string name;
    std::cin >> name;
    std::cout << "你好，" << name << "！\n";

    return 0;
}
