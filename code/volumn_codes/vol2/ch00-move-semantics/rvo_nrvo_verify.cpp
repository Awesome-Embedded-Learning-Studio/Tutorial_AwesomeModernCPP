/// @file rvo_nrvo_verify.cpp
/// @brief 验证 RVO/NRVO 行为：-fno-elide-constructors 在 C++17 下对 prvalue 无效
/// @note GCC 15, -std=c++17 -O2

#include <iostream>
#include <string>

struct Point {
    double x, y;

    Point(double x, double y) : x(x), y(y)
    {
        std::cout << "  构造 Point(" << x << ", " << y << ")\n";
    }

    Point(const Point& other) : x(other.x), y(other.y)
    {
        std::cout << "  拷贝 Point(" << x << ", " << y << ")\n";
    }

    Point(Point&& other) noexcept : x(other.x), y(other.y)
    {
        std::cout << "  移动 Point(" << x << ", " << y << ")\n";
    }
};

Point make_point_rvo(double x, double y)
{
    return Point(x, y);  // prvalue return
}

Point make_point_nrvo(double x, double y)
{
    Point p(x, y);  // named local
    return p;
}

int main()
{
    std::cout << "=== RVO (prvalue return) ===\n";
    Point a = make_point_rvo(1.0, 2.0);
    (void)a;

    std::cout << "\n=== NRVO (named return) ===\n";
    Point b = make_point_nrvo(3.0, 4.0);
    (void)b;

    return 0;
}
