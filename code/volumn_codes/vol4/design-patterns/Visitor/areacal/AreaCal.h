#pragma once

struct Circle;
struct Rectangle;
struct Triangle;

struct ShapeVisitor {
    virtual ~ShapeVisitor() = default;
    virtual void visit(Circle& c) = 0;
    virtual void visit(Rectangle& r) = 0;
    virtual void visit(Triangle& t) = 0;
};

struct Shape {
    virtual ~Shape() = default;
    virtual void accept(ShapeVisitor& visitor) = 0;
};

struct Circle : public Shape {
    double radius;

    explicit Circle(double r) : radius(r) {}

    void accept(ShapeVisitor& visitor) override { visitor.visit(*this); }
};

struct Rectangle : public Shape {
    double width, height;

    Rectangle(double w, double h) : width(w), height(h) {}

    void accept(ShapeVisitor& visitor) override { visitor.visit(*this); }
};

struct Triangle : public Shape {
    double base, height;

    Triangle(double b, double h) : base(b), height(h) {}

    void accept(ShapeVisitor& visitor) override { visitor.visit(*this); }
};

struct AreaCalculatorVisitor : public ShapeVisitor {
    double total_area = 0.0;

    void visit(Circle& c) override { total_area += 3.14 * c.radius * c.radius; }

    void visit(Rectangle& r) override { total_area += r.width * r.height; }

    void visit(Triangle& t) override { total_area += 0.5 * t.base * t.height; }
};
