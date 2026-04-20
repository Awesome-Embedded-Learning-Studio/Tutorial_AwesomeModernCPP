/**
 * @file ex01_rectangle.cpp
 * @brief 练习：Rectangle 类
 *
 * 实现一个简单的 Rectangle 类，包含：
 * - 私有成员 width_ / height_
 * - set_size() 拒绝非正值
 * - area()、perimeter()、print() 方法
 */

#include <iostream>

class Rectangle {
private:
    int width_;
    int height_;

public:
    // 默认构造函数
    Rectangle() : width_(0), height_(0) {}

    // 带参数的构造函数
    Rectangle(int w, int h) : width_(0), height_(0) {
        set_size(w, h);
    }

    // 设置宽高，拒绝非正值
    bool set_size(int w, int h) {
        if (w <= 0 || h <= 0) {
            std::cout << "  错误: 宽高必须为正数 ("
                      << w << ", " << h << ")\n";
            return false;
        }
        width_ = w;
        height_ = h;
        return true;
    }

    int area() const {
        return width_ * height_;
    }

    int perimeter() const {
        return 2 * (width_ + height_);
    }

    void print() const {
        std::cout << "Rectangle(" << width_ << " x " << height_ << ")"
                  << " 面积=" << area()
                  << " 周长=" << perimeter() << "\n";
    }

    // Getter
    int width() const { return width_; }
    int height() const { return height_; }
};

int main() {
    std::cout << "===== Rectangle 类 =====\n\n";

    // 默认构造
    Rectangle r1;
    std::cout << "默认构造: ";
    r1.print();

    // 带参数构造
    Rectangle r2(5, 3);
    std::cout << "带参构造: ";
    r2.print();

    // set_size 正常设置
    std::cout << "\n设置大小 (10, 6):\n";
    r1.set_size(10, 6);
    r1.print();

    // set_size 拒绝非正值
    std::cout << "\n尝试设置非法值:\n";
    r1.set_size(-1, 5);
    r1.set_size(0, 3);
    r1.set_size(4, -2);

    // 验证非法设置后矩形不变
    std::cout << "\n非法设置后:\n";
    r1.print();

    return 0;
}
