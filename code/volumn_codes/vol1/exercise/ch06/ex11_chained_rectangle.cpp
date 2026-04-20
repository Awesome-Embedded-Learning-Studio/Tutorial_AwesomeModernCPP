/**
 * @file ex11_chained_rectangle.cpp
 * @brief 练习：链式调用的 Rectangle
 *
 * set_width() 和 set_height() 返回 *this 引用，
 * 支持方法链式调用：rect.set_width(3).set_height(4).area()
 */

#include <iostream>

class Rectangle {
private:
    int width_;
    int height_;

public:
    Rectangle() : width_(0), height_(0) {}

    Rectangle(int w, int h) : width_(0), height_(0) {
        set_width(w);
        set_height(h);
    }

    // 链式调用：返回 *this 引用
    Rectangle& set_width(int w) {
        if (w > 0) {
            width_ = w;
        } else {
            std::cout << "  错误: 宽度必须为正数\n";
        }
        return *this;
    }

    Rectangle& set_height(int h) {
        if (h > 0) {
            height_ = h;
        } else {
            std::cout << "  错误: 高度必须为正数\n";
        }
        return *this;
    }

    int area() const {
        return width_ * height_;
    }

    int perimeter() const {
        return 2 * (width_ + height_);
    }

    void print() const {
        std::cout << "Rectangle(" << width_ << " x " << height_ << ")"
                  << " area=" << area() << "\n";
    }

    int width() const { return width_; }
    int height() const { return height_; }
};

int main() {
    std::cout << "===== 链式调用 Rectangle =====\n\n";

    // 链式调用设置宽高
    Rectangle rect;
    std::cout << "链式设置: rect.set_width(3).set_height(4)\n";
    rect.set_width(3).set_height(4);
    rect.print();

    // 链式调用后直接计算面积
    std::cout << "\n链式调用计算面积:\n";
    int area = rect.set_width(3).set_height(4).area();
    std::cout << "  rect.set_width(3).set_height(4).area() = " << area << "\n";

    // 验证
    bool ok = (area == 12);
    std::cout << "  验证 area == 12: " << (ok ? "通过" : "失败") << "\n\n";

    // 多步链式调用
    std::cout << "单行链式调用:\n";
    Rectangle r2;
    r2.set_width(5).set_height(6);
    std::cout << "  5x6 area = " << r2.area() << "\n\n";

    // 链式中包含错误
    std::cout << "链式中包含非法值:\n";
    Rectangle r3;
    r3.set_width(10).set_height(-1).set_height(7);
    std::cout << "  结果: ";
    r3.print();

    // 重新赋值验证
    std::cout << "\n连续修改同一对象:\n";
    Rectangle r4;
    r4.set_width(2).set_height(3);
    std::cout << "  2x3: area=" << r4.area() << "\n";
    r4.set_width(4).set_height(5);
    std::cout << "  4x5: area=" << r4.area() << "\n";

    std::cout << "\n要点:\n";
    std::cout << "  方法返回 *this 引用，支持链式调用\n";
    std::cout << "  每次调用返回同一对象的引用，可继续调用\n";
    std::cout << "  常见用于 Builder 模式和配置式 API\n";

    return 0;
}
