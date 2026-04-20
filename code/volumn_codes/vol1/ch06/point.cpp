// point.cpp
#include <cmath>
#include <iostream>
#include <string>

/// @brief 二维平面上的点，演示类的基本定义与封装
class Point {
private:
    double x_;
    double y_;

public:
    /// @brief 设置坐标
    /// @param new_x 新的 x 坐标
    /// @param new_y 新的 y 坐标
    void set(double new_x, double new_y)
    {
        x_ = new_x;
        y_ = new_y;
    }

    /// @brief 获取 x 坐标
    /// @return x 坐标的值
    double get_x() const { return x_; }

    /// @brief 获取 y 坐标
    /// @return y 坐标的值
    double get_y() const { return y_; }

    /// @brief 计算到另一个点的欧几里得距离
    /// @param other 目标点
    /// @return 两点之间的距离
    double distance_to(const Point& other) const
    {
        double dx = x_ - other.x_;
        double dy = y_ - other.y_;
        return std::sqrt(dx * dx + dy * dy);
    }

    /// @brief 计算到原点的距离
    /// @return 到原点 (0, 0) 的距离
    double distance_to_origin() const
    {
        return std::sqrt(x_ * x_ + y_ * y_);
    }

    /// @brief 打印坐标到标准输出
    void print() const
    {
        std::cout << "Point(" << x_ << ", " << y_ << ")";
    }
};

int main()
{
    Point p1;
    p1.set(3.0, 4.0);

    Point p2;
    p2.set(6.0, 8.0);

    // 打印两个点
    std::cout << "p1 = ";
    p1.print();
    std::cout << "\n";

    std::cout << "p2 = ";
    p2.print();
    std::cout << "\n";

    // 计算距离
    std::cout << "distance(p1, p2) = " << p1.distance_to(p2) << "\n";
    std::cout << "distance(p1, origin) = " << p1.distance_to_origin() << "\n";

    // 尝试访问 private 成员——取消下面的注释会编译报错
    // p1.x_ = 100.0;  // error: 'double Point::x_' is private

    return 0;
}
