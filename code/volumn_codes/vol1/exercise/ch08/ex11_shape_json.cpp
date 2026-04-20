/**
 * @file ex11_shape_json.cpp
 * @brief 练习：Shape 层次的 JSON 序列化
 *
 * 在 Shape 继承层次中添加虚函数 to_json()，
 * 每种派生类序列化为 JSON 对象字符串。
 * 最终将 vector<unique_ptr<Shape>> 输出为 JSON 数组。
 */

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

constexpr double kPi = 3.14159265358979323846;

class Shape {
public:
    virtual ~Shape() = default;

    virtual double area() const = 0;
    virtual double perimeter() const = 0;
    virtual std::string to_json() const = 0;
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

    std::string to_json() const override
    {
        return std::string("{\"type\":\"Rectangle\",")
            + "\"width\":" + std::to_string(width_) + ","
            + "\"height\":" + std::to_string(height_) + ","
            + "\"area\":" + std::to_string(area()) + ","
            + "\"perimeter\":" + std::to_string(perimeter())
            + "}";
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

    std::string to_json() const override
    {
        return std::string("{\"type\":\"Circle\",")
            + "\"radius\":" + std::to_string(radius_) + ","
            + "\"area\":" + std::to_string(area()) + ","
            + "\"perimeter\":" + std::to_string(perimeter())
            + "}";
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

    std::string to_json() const override
    {
        return std::string("{\"type\":\"Triangle\",")
            + "\"base\":" + std::to_string(base_) + ","
            + "\"height\":" + std::to_string(height_) + ","
            + "\"area\":" + std::to_string(area()) + ","
            + "\"perimeter\":" + std::to_string(perimeter())
            + "}";
    }
};

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

    std::string to_json() const override
    {
        return std::string("{\"type\":\"Square\",")
            + "\"side\":" + std::to_string(side_) + ","
            + "\"area\":" + std::to_string(area()) + ","
            + "\"perimeter\":" + std::to_string(perimeter())
            + "}";
    }
};

// 将形状列表序列化为 JSON 数组字符串
std::string shapes_to_json_array(
    const std::vector<std::unique_ptr<Shape>>& shapes)
{
    std::string result = "[\n";
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        result += "  " + shapes[i]->to_json();
        if (i + 1 < shapes.size()) {
            result += ",";
        }
        result += "\n";
    }
    result += "]";
    return result;
}

// ============================================================
// main
// ============================================================
int main()
{
    std::cout << "===== Shape JSON 序列化 =====\n\n";

    std::vector<std::unique_ptr<Shape>> shapes;
    shapes.push_back(std::make_unique<Rectangle>(4.0, 6.0));
    shapes.push_back(std::make_unique<Circle>(3.0));
    shapes.push_back(std::make_unique<Triangle>(6.0, 4.0, 6.0, 5.0, 5.0));
    shapes.push_back(std::make_unique<Square>(5.0));

    std::cout << "--- 单个形状 JSON ---\n";
    for (const auto& s : shapes) {
        std::cout << "  " << s->to_json() << "\n";
    }

    std::cout << "\n--- JSON 数组 ---\n";
    std::string json_array = shapes_to_json_array(shapes);
    std::cout << json_array << "\n";

    return 0;
}
