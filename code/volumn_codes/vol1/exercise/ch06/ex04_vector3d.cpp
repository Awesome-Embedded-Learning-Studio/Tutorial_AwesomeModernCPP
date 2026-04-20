/**
 * @file ex04_vector3d.cpp
 * @brief 练习：实现 Vector3D 类
 *
 * 实现三维向量类，包含：
 * - 委托构造函数（delegating constructor）
 * - 拷贝构造函数
 * - length() 计算向量长度
 */

#include <cmath>
#include <iostream>

class Vector3D {
private:
    double x_;
    double y_;
    double z_;

public:
    // 默认构造函数：零向量
    Vector3D() : Vector3D(0.0, 0.0, 0.0) {}  // 委托构造

    // 带参数的构造函数（目标构造函数）
    Vector3D(double x, double y, double z)
        : x_(x), y_(y), z_(z) {}

    // 拷贝构造函数
    Vector3D(const Vector3D& other)
        : x_(other.x_), y_(other.y_), z_(other.z_) {}

    // 计算向量长度（模）
    double length() const {
        return std::sqrt(x_ * x_ + y_ * y_ + z_ * z_);
    }

    // 打印向量
    void print() const {
        std::cout << "Vector3D(" << x_ << ", " << y_ << ", " << z_ << ")"
                  << " |v| = " << length();
    }

    // Getter
    double x() const { return x_; }
    double y() const { return y_; }
    double z() const { return z_; }
};

int main() {
    std::cout << "===== Vector3D 类 =====\n\n";

    // 默认构造（委托到三参数构造函数）
    Vector3D v0;
    std::cout << "默认构造: ";
    v0.print();
    std::cout << "\n\n";

    // 三参数构造
    Vector3D v1(3.0, 4.0, 0.0);
    std::cout << "v1(3, 4, 0): ";
    v1.print();
    std::cout << "\n";

    Vector3D v2(1.0, 2.0, 2.0);
    std::cout << "v2(1, 2, 2): ";
    v2.print();
    std::cout << "\n\n";

    // 拷贝构造
    Vector3D v3(v1);   // 或 Vector3D v3 = v1;
    std::cout << "拷贝 v1 -> v3: ";
    v3.print();
    std::cout << "\n\n";

    // 单位向量
    Vector3D unit_x(1.0, 0.0, 0.0);
    Vector3D unit_y(0.0, 1.0, 0.0);
    Vector3D unit_z(0.0, 0.0, 1.0);

    std::cout << "单位向量:\n";
    std::cout << "  "; unit_x.print(); std::cout << "\n";
    std::cout << "  "; unit_y.print(); std::cout << "\n";
    std::cout << "  "; unit_z.print(); std::cout << "\n";

    return 0;
}
