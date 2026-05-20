// 12_class_point.cpp
// 演示类的定义、封装与成员函数

#include <cmath>
#include <iostream>

class Point {
  private:
    double x_;
    double y_;

  public:
    void set(double new_x, double new_y) {
        x_ = new_x;
        y_ = new_y;
    }

    double get_x() const { return x_; }
    double get_y() const { return y_; }

    double distance_to(const Point& other) const {
        double dx = x_ - other.x_;
        double dy = y_ - other.y_;
        return std::sqrt(dx * dx + dy * dy);
    }

    double distance_to_origin() const { return std::sqrt(x_ * x_ + y_ * y_); }

    void print() const { std::cout << "Point(" << x_ << ", " << y_ << ")"; }
};

int main() {
    Point p1;
    p1.set(3.0, 4.0);

    Point p2;
    p2.set(6.0, 8.0);

    std::cout << "p1 = ";
    p1.print();
    std::cout << "\n";

    std::cout << "p2 = ";
    p2.print();
    std::cout << "\n";

    std::cout << "distance(p1, p2) = " << p1.distance_to(p2) << "\n";
    std::cout << "distance(p1, origin) = " << p1.distance_to_origin() << "\n";

    return 0;
}
