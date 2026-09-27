#include <iostream>
#include <memory>

struct Widget {
    int value;
    explicit Widget(int v) : value(v) { std::cout << "Widget(" << value << ") 构造\n"; }
    ~Widget() { std::cout << "~Widget(" << value << ") 析构\n"; }
};

void ownership_demo() {
    auto p1 = std::make_unique<Widget>(42);
    // auto p2 = p1;              // 编译错误！unique_ptr 不可拷贝
    auto p2 = std::move(p1); // OK：所有权从 p1 转移到 p2

    // 此时 p1 == nullptr，p2 拥有对象
    std::cout << "p1: " << p1.get() << "\n";         // 输出: 0 或 nullptr
    std::cout << "p2: " << p2.get() << "\n";         // 输出: 有效地址
    std::cout << "p2->value: " << p2->value << "\n"; // 输出: 42
} // p2 析构，Widget 自动被 delete

int main() {
    ownership_demo();
    return 0;
}
