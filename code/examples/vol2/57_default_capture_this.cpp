// C++20 里 [=] 隐式捕获 this 被弃用:点「运行」,诊断区躺着 -Wdeprecated 警告,
// 程序照常跑(行为暂兼容)。想看 C++17 下它安静的样子:
// 点「在 Godbolt 中打开」,把 -std=c++20 改成 c++17 再编译一次,警告就没了。
#include <iostream>

class Sensor {
    int reading_ = 42;

  public:
    auto legacy() {
        return [=] { return reading_; };
    } // C++20: 弃用警告
    auto by_this() {
        return [=, this] { return reading_; };
    } // 显式捕获 this 指针
    auto by_copy() {
        return [*this] { return reading_; };
    } // 把整个对象拷进来
};

int main() {
    Sensor s;
    std::cout << "legacy [=]:     " << s.legacy()() << "\n";
    std::cout << "explicit this:  " << s.by_this()() << "\n";
    std::cout << "copy *this:     " << s.by_copy()() << "\n";
    std::cout << "all three print 42 -- the warning is about style, not behavior\n";
    return 0;
}
