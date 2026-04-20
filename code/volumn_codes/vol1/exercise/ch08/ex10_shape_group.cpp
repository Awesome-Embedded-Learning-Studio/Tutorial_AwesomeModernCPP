/**
 * @file ex10_shape_group.cpp
 * @brief 练习：组合模式 —— ShapeGroup
 *
 * ShapeGroup 继承 Shape，持有 vector<unique_ptr<Shape>>。
 * area() 返回子图形面积之和，perimeter() 返回 0（组合无意义）。
 * 支持嵌套：ShapeGroup 内可以包含另一个 ShapeGroup。
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
    virtual void print(int indent = 0) const = 0;
};

// 辅助：输出缩进
static void print_indent(int n)
{
    for (int i = 0; i < n; ++i) {
        std::cout << "  ";
    }
}

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

    void print(int indent) const override
    {
        print_indent(indent);
        std::cout << "Circle(r=" << radius_
                  << ") area=" << area() << "\n";
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

    void print(int indent) const override
    {
        print_indent(indent);
        std::cout << "Rectangle(" << width_ << "x"
                  << height_ << ") area=" << area() << "\n";
    }
};

class Triangle : public Shape {
private:
    double base_;
    double height_;
    double side_a_;
    double side_b_;
    double side_c_;

public:
    Triangle(double b, double h, double a, double bb, double c)
        : base_(b), height_(h),
          side_a_(a), side_b_(bb), side_c_(c) {}

    double area() const override
    {
        return 0.5 * base_ * height_;
    }

    double perimeter() const override
    {
        return side_a_ + side_b_ + side_c_;
    }

    void print(int indent) const override
    {
        print_indent(indent);
        std::cout << "Triangle(base=" << base_
                  << ", h=" << height_
                  << ") area=" << area() << "\n";
    }
};

class ShapeGroup : public Shape {
private:
    std::string name_;
    std::vector<std::unique_ptr<Shape>> children_;

public:
    explicit ShapeGroup(const std::string& name)
        : name_(name) {}

    void add(std::unique_ptr<Shape> shape)
    {
        children_.push_back(std::move(shape));
    }

    double area() const override
    {
        double total = 0.0;
        for (const auto& child : children_) {
            total += child->area();
        }
        return total;
    }

    // 组合图形的"周长"语义不明确，返回 0
    double perimeter() const override
    {
        return 0.0;
    }

    void print(int indent = 0) const override
    {
        print_indent(indent);
        std::cout << "Group(\"" << name_ << "\") area="
                  << area() << ", children="
                  << children_.size() << ":\n";
        for (const auto& child : children_) {
            child->print(indent + 1);
        }
    }
};

// ============================================================
// main
// ============================================================
int main()
{
    std::cout << "===== 组合模式 ShapeGroup =====\n\n";

    // 创建一些基本图形
    auto c1 = std::make_unique<Circle>(3.0);
    auto r1 = std::make_unique<Rectangle>(4.0, 5.0);
    auto t1 = std::make_unique<Triangle>(6.0, 4.0, 6.0, 5.0, 5.0);

    // 创建子组
    auto subgroup = std::make_unique<ShapeGroup>("sub-shapes");
    subgroup->add(std::make_unique<Circle>(2.0));
    subgroup->add(std::make_unique<Rectangle>(3.0, 3.0));

    // 创建顶层组
    ShapeGroup top("scene");
    top.add(std::move(c1));
    top.add(std::move(r1));
    top.add(std::move(t1));
    top.add(std::move(subgroup));

    top.print();

    std::cout << "\n顶层组合面积 = " << top.area() << "\n";
    std::cout << "顶层组合周长 = " << top.perimeter()
              << " (组合图形无周长定义)\n";

    // 验证嵌套：subgroup 的面积被正确汇总
    std::cout << "\n--- 验证嵌套 ---\n";
    ShapeGroup inner("inner");
    inner.add(std::make_unique<Circle>(1.0));  // area = pi
    std::cout << "inner area = " << inner.area()
              << " (应为 " << kPi << ")\n";

    ShapeGroup outer("outer");
    outer.add(std::make_unique<Circle>(2.0));  // area = 4*pi
    outer.add(std::make_unique<ShapeGroup>(std::move(inner))); // + pi
    std::cout << "outer area = " << outer.area()
              << " (应为 " << 5.0 * kPi << ")\n";

    return 0;
}
