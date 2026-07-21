// Comparable<Derived>:CRTP mixin,只实现 < 和 ==,自动补齐 > <= >= !=
// 对应文章:documents/vol4-advanced/vol1-basics-cpp11-14/09-crtp.md
// 编译:g++ -Wall -Wextra -std=c++20 comparable_mixin.cpp -o comparable && ./comparable
#include <iostream>

template <typename Derived> struct Comparable {
    friend bool operator>(const Derived& a, const Derived& b) { return b < a; }
    friend bool operator<=(const Derived& a, const Derived& b) { return !(b < a); }
    friend bool operator>=(const Derived& a, const Derived& b) { return !(a < b); }
    friend bool operator!=(const Derived& a, const Derived& b) { return !(a == b); }
};

struct Point : Comparable<Point> {
    int x, y;
    Point(int x_, int y_) : x(x_), y(y_) {}
    // 只实现 < 和 ==,其余四个由 Comparable<Point> 自动补齐
    friend bool operator<(const Point& a, const Point& b) { return a.x < b.x; }
    friend bool operator==(const Point& a, const Point& b) { return a.x == b.x; }
};

int main() {
    Point p1{1, 2}, p2{3, 4};
    std::cout << std::boolalpha;
    std::cout << "p1 < p2:  " << (p1 < p2) << "\n";
    std::cout << "p1 > p2:  " << (p1 > p2) << "\n";
    std::cout << "p1 <= p2: " << (p1 <= p2) << "\n";
    std::cout << "p1 >= p2: " << (p1 >= p2) << "\n";
    std::cout << "p1 != p2: " << (p1 != p2) << "\n";
    return 0;
}
