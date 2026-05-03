// 智能指针：unique_ptr 与 shared_ptr
// 来源：OnceCallback 前置知识速查 (pre-00)
// 编译：g++ -std=c++17 -Wall -Wextra 02_smart_pointers.cpp -o 02_smart_pointers

#include <memory>
#include <iostream>
#include <string>

int main() {
    std::cout << "=== std::unique_ptr：独占所有权 ===\n";
    {
        auto p = std::make_unique<int>(42);
        std::cout << "  *p = " << *p << "\n";
        // auto p2 = p;             // 编译错误：不可拷贝
        auto p3 = std::move(p);    // OK：移动转移所有权
        std::cout << "  after move, p3 = " << *p3 << "\n";
        std::cout << "  p is " << (p ? "not null" : "nullptr") << "\n";
    }

    std::cout << "\n=== std::shared_ptr：共享所有权 ===\n";
    {
        auto p1 = std::make_shared<std::string>("hello");
        auto p2 = p1;   // OK：拷贝，引用计数 +1
        std::cout << "  *p1 = " << *p1 << "\n";
        std::cout << "  *p2 = " << *p2 << "\n";
        std::cout << "  use_count = " << p1.use_count() << "\n";
    }

    std::cout << "\n=== unique_ptr 捕获到 lambda (move-only) ===\n";
    {
        auto ptr = std::make_unique<int>(99);
        // 移动捕获 unique_ptr 到 lambda
        auto f = [p = std::move(ptr)]() { return *p; };
        std::cout << "  f() = " << f() << "\n";
        std::cout << "  ptr is " << (ptr ? "not null" : "nullptr") << "\n";
    }

    return 0;
}
