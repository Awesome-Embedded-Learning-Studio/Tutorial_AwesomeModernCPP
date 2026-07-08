// NoDestructor 基础:函数局部静态用法 + "不析构"验证(Noisy 类型)
// 来源:NoDestructor 实战(一)(二)
// 编译:g++ -std=c++20 -Wall -Wextra -I. 23_no_destructor_basic.cpp -o 23_no_destructor_basic

#include "no_destructor.hpp"

#include <iostream>
#include <string>

// 析构会打 log 的类型——验证 ~T() 永不调用
struct Noisy {
    int v;
    explicit Noisy(int x) : v(x) { std::puts("  Noisy()"); }
    ~Noisy() { std::puts("  ~Noisy()  ← 这个永不打印(NoDestructor 跳过 ~T)"); }
};

// 标准用法:函数局部静态 + NoDestructor + 非平凡析构 T
const std::string& DefaultName() {
    static const tamcpp::chrome::NoDestructor<std::string> s("chromium");
    return *s;
}

const Noisy& GlobalNoisy() {
    static const tamcpp::chrome::NoDestructor<Noisy> n(42);
    return *n;
}

int main() {
    std::cout << "=== 函数局部静态 + NoDestructor ===\n";
    std::cout << "  DefaultName() = " << DefaultName() << "\n";
    std::cout << "  调用多次(构造只跑一次,magic statics):\n";
    DefaultName();
    DefaultName();
    std::cout << "  GlobalNoisy().v = " << GlobalNoisy().v << "\n";

    std::cout << "\n=== 智能指针风格访问 ===\n";
    static const tamcpp::chrome::NoDestructor<std::string> s("hello");
    std::cout << "  *s = " << *s << "\n";               // operator*
    std::cout << "  s->size() = " << s->size() << "\n"; // operator->
    std::cout << "  s.get() = " << s.get() << "\n";     // get()

    std::cout << "\n=== 程序退出前:注意没有 ~Noisy() / ~string() 的析构输出 ===\n";
    std::cout << "(如果看到析构输出,说明 NoDestructor 失效了)\n";
    return 0;
}
