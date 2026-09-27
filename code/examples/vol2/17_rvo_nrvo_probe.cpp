// rvo_nrvo_probe.cpp -- RVO/NRVO 最小验证：返回 prvalue vs 返回命名局部变量
// Standard: C++17

#include <iostream>

struct Point {
    double x, y;

    Point(double x, double y) : x(x), y(y) {
        // 我知道好像在这里塞中文可能会造成问题，但是怕啥，demo而已
        std::cout << "  构造 Point(" << x << ", " << y << ")\n";
    }

    Point(const Point& other) : x(other.x), y(other.y) {
        std::cout << "  拷贝 Point(" << x << ", " << y << ")\n";
    }

    Point(Point&& other) noexcept : x(other.x), y(other.y) {
        std::cout << "  移动 Point(" << x << ", " << y << ")\n";
    }
};

// RVO 场景：返回 prvalue（临时对象）
Point make_point_rvo(double x, double y) {
    return Point(x, y); // 返回一个临时对象
}

// NRVO 场景：返回命名局部变量
Point make_point_nrvo(double x, double y) {
    Point p(x, y); // 命名局部变量
    // ... 可能还有一些对 p 的操作 ...
    return p; // 返回命名变量
}

int main() {
    std::cout << "=== RVO ===\n";
    Point a = make_point_rvo(1.0, 2.0);

    std::cout << "\n=== NRVO ===\n";
    Point b = make_point_nrvo(3.0, 4.0);

    return 0;
}
