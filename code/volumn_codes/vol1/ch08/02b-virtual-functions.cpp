// 02b-virtual-functions.cpp
// 演示：多态图形系统——虚函数、override、虚析构函数
// 编译: g++ -Wall -Wextra -std=c++17 02b-virtual-functions.cpp -o 02b-virtual-functions

#include <cstdio>
#include <vector>

// 抽象基类
class Shape {
public:
    virtual void draw() const = 0;           // 纯虚函数
    virtual double area() const = 0;         // 纯虚函数
    virtual ~Shape() = default;              // 虚析构函数

    const char* name() const { return name_; }

protected:
    const char* name_;   // 派生类在构造时设置
};

// 圆形
class Circle : public Shape {
private:
    double radius_;

public:
    explicit Circle(double r) : radius_(r) { name_ = "Circle"; }

    void draw() const override {
        printf("  Drawing Circle (r=%.2f)\n", radius_);
    }

    double area() const override {
        return 3.14159265 * radius_ * radius_;
    }
};

// 矩形
class Rectangle : public Shape {
private:
    double width_;
    double height_;

public:
    Rectangle(double w, double h) : width_(w), height_(h) { name_ = "Rectangle"; }

    void draw() const override {
        printf("  Drawing Rectangle (%.2f x %.2f)\n", width_, height_);
    }

    double area() const override {
        return width_ * height_;
    }
};

// 三角形
class Triangle : public Shape {
private:
    double base_;
    double height_;

public:
    Triangle(double b, double h) : base_(b), height_(h) { name_ = "Triangle"; }

    void draw() const override {
        printf("  Drawing Triangle (base=%.2f, height=%.2f)\n", base_, height_);
    }

    double area() const override {
        return 0.5 * base_ * height_;
    }
};

int main() {
    // 用基类指针的 vector 存储所有图形
    std::vector<Shape*> shapes;
    shapes.push_back(new Circle(3.0));
    shapes.push_back(new Rectangle(4.0, 5.0));
    shapes.push_back(new Triangle(6.0, 2.0));
    shapes.push_back(new Circle(1.5));

    printf("=== Drawing all shapes ===\n");
    for (auto* s : shapes) {
        s->draw();   // 多态：调用实际类型的 draw()
    }

    printf("\n=== Areas ===\n");
    double total = 0.0;
    for (auto* s : shapes) {
        double a = s->area();
        printf("  %-12s: %.4f\n", s->name(), a);
        total += a;
    }
    printf("  Total area: %.4f\n", total);

    // 清理——虚析构函数确保每个派生类正确释放
    for (auto* s : shapes) {
        delete s;
    }
    return 0;
}
