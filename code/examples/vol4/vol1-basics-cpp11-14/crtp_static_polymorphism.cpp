// CRTP 静态多态:派生类把自己传给基类,编译期绑定,无 vtable 无运行时分派
// 对应文章:documents/vol4-advanced/vol1-basics-cpp11-14/09-crtp.md
// 编译运行:g++ -Wall -Wextra -std=c++20 crtp_static_polymorphism.cpp -o crtp && ./crtp
// 看零开销汇编:g++ -O2 -std=c++20 -DCRTP_BENCH -S crtp_static_polymorphism.cpp -o crtp.s
//   use_crtp() 会被完全内联成 mov $0x2a,%eax; ret(对比虚函数版的 vtable 解引用)
#include <iostream>

template <typename Derived> struct Shape {
    const char* name() { return static_cast<Derived*>(this)->name_impl(); }
    double area() { return static_cast<Derived*>(this)->area_impl(); }
};

struct Circle : Shape<Circle> {
    double r;
    explicit Circle(double r_) : r(r_) {}
    const char* name_impl() { return "Circle"; }
    double area_impl() { return 3.14159 * r * r; }
};

struct Square : Shape<Square> {
    double side;
    explicit Square(double s) : side(s) {}
    const char* name_impl() { return "Square"; }
    double area_impl() { return side * side; }
};

#ifdef CRTP_BENCH
// 汇编基准:use_crtp 在 -O2 下被完全内联,直接返回常量 42,零函数调用
template <typename D> struct BenchBase {
    int compute() { return static_cast<D*>(this)->compute_impl(); }
};
struct BenchConcrete : BenchBase<BenchConcrete> {
    int compute_impl() { return 42; }
};
int use_crtp() {
    BenchConcrete c;
    return c.compute();
}
#endif

int main() {
    Circle c{2.0};
    Square s{3.0};
    std::cout << c.name() << " area = " << c.area() << "\n"; // Circle 走 Circle::area_impl
    std::cout << s.name() << " area = " << s.area() << "\n"; // Square 走 Square::area_impl
    return 0;
}
