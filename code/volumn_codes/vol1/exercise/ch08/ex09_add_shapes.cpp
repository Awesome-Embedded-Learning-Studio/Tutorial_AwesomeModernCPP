/**
 * @file ex09_add_shapes.cpp
 * @brief 练习：Shape 继承层次与 Square/Ellipse
 *
 * Shape 基类含虚函数 area() 和 perimeter()。
 * 添加 Square 和 Ellipse 派生类。
 * 注释说明为什么 Square 不应继承 Rectangle（LSP 违反）。
 */

#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

constexpr double kPi = 3.14159265358979323846;

class Shape {
public:
    virtual ~Shape() = default;

    virtual double area() const = 0;
    virtual double perimeter() const = 0;

    virtual void print() const
    {
        std::cout << "  area=" << area()
                  << ", perimeter=" << perimeter() << "\n";
    }
};

class Rectangle : public Shape {
private:
    double width_;
    double height_;

public:
    Rectangle(double w, double h) : width_(w), height_(h) {}

    double area() const override
    {
        return width_ * height_;
    }

    double perimeter() const override
    {
        return 2.0 * (width_ + height_);
    }

    void set_width(double w) { width_ = w; }
    void set_height(double h) { height_ = h; }
    double width() const { return width_; }
    double height() const { return height_; }

    void print() const override
    {
        std::cout << "  Rectangle(" << width_
                  << " x " << height_ << "): ";
        Shape::print();
    }
};

class Circle : public Shape {
private:
    double radius_;

public:
    explicit Circle(double r) : radius_(r) {}

    double area() const override
    {
        return kPi * radius_ * radius_;
    }

    double perimeter() const override
    {
        return 2.0 * kPi * radius_;
    }

    double radius() const { return radius_; }

    void print() const override
    {
        std::cout << "  Circle(r=" << radius_ << "): ";
        Shape::print();
    }
};

// Square —— 独立继承 Shape，而非继承 Rectangle
//
// 为什么 Square 不应继承 Rectangle？
//
// Rectangle 提供 set_width() 和 set_height()，可以独立修改宽高。
// 如果 Square 继承 Rectangle，那么调用 set_width(5) 后，
// Square 应该仍然保持"正方形"性质（高也变为 5）。
// 但 Rectangle 的使用者期望 set_width 只改宽度、不影响高度。
//
// 这违反了里氏替换原则 (LSP)：在需要 Rectangle 的地方传入
// Square，行为会不符合预期。因此 Square 应该独立实现，
// 或者用不可变设计（不提供 set_width/set_height）。

class Square : public Shape {
private:
    double side_;

public:
    explicit Square(double s) : side_(s) {}

    double area() const override
    {
        return side_ * side_;
    }

    double perimeter() const override
    {
        return 4.0 * side_;
    }

    void set_side(double s) { side_ = s; }
    double side() const { return side_; }

    void print() const override
    {
        std::cout << "  Square(s=" << side_ << "): ";
        Shape::print();
    }
};

class Ellipse : public Shape {
private:
    double semi_major_;  // 长半轴 a
    double semi_minor_;  // 短半轴 b

public:
    Ellipse(double a, double b)
        : semi_major_(a), semi_minor_(b) {}

    double area() const override
    {
        return kPi * semi_major_ * semi_minor_;
    }

    // Ramanujan 近似公式
    double perimeter() const override
    {
        double a = semi_major_;
        double b = semi_minor_;
        double h = ((a - b) * (a - b))
                   / ((a + b) * (a + b));
        return kPi * (a + b)
               * (1.0 + (3.0 * h) / (10.0 + std::sqrt(4.0 - 3.0 * h)));
    }

    double semi_major() const { return semi_major_; }
    double semi_minor() const { return semi_minor_; }

    void print() const override
    {
        std::cout << "  Ellipse(a=" << semi_major_
                  << ", b=" << semi_minor_ << "): ";
        Shape::print();
    }
};

// ============================================================
// main
// ============================================================
int main()
{
    std::cout << "===== Shape 继承层次 =====\n\n";

    std::vector<std::unique_ptr<Shape>> shapes;
    shapes.push_back(std::make_unique<Rectangle>(4.0, 6.0));
    shapes.push_back(std::make_unique<Circle>(3.0));
    shapes.push_back(std::make_unique<Square>(5.0));
    shapes.push_back(std::make_unique<Ellipse>(5.0, 3.0));

    double total_area = 0.0;
    for (const auto& s : shapes) {
        s->print();
        total_area += s->area();
    }

    std::cout << "\n所有图形总面积 = " << total_area << "\n";

    std::cout << "\n--- LSP 说明 ---\n";
    std::cout << "  Square 没有继承 Rectangle，\n";
    std::cout << "  因为 Rectangle 允许独立修改宽高，\n";
    std::cout << "  而 Square 必须保持边长相等。\n";
    std::cout << "  强行继承会违反里氏替换原则。\n";

    return 0;
}
