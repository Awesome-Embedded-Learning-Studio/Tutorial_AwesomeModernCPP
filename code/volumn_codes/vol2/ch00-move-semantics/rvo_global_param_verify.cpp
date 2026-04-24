/// @file rvo_global_param_verify.cpp
/// @brief 验证返回全局变量（拷贝）vs 返回参数（隐式移动）的行为
/// @note GCC 15, -std=c++17 -O2

#include <iostream>

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

Point global_point(1.0, 2.0);

Point return_global()
{
    return global_point;  // copy, no implicit move
}

Point return_param(Point p)
{
    return p;  // implicit move
}

int main()
{
    std::cout << "=== 返回全局变量 ===\n";
    auto g = return_global();
    (void)g;

    std::cout << "\n=== 返回参数（隐式移动）===\n";
    Point p(5.0, 6.0);
    auto r = return_param(std::move(p));
    (void)r;

    return 0;
}
