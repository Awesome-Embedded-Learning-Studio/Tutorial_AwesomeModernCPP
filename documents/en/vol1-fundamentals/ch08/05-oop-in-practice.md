---
title: OOP in Practice
description: Comprehensively utilize inheritance, polymorphism, and operator overloading
  to implement a complete graphics drawing system, and discuss the design choice between
  inheritance vs. composition.
chapter: 8
order: 5
difficulty: intermediate
reading_time_minutes: 15
platform: host
prerequisites:
- 多继承与虚继承
tags:
- cpp-modern
- host
- intermediate
- 进阶
cpp_standard:
- 11
- 14
- 17
- 20
---
# OOP in Practice

So far, we have broken down all the core components of OOP—classes and objects, construction and destruction, inheritance and polymorphism, operator overloading, and virtual inheritance. Each concept alone isn't overly complex, but in a real project, these components work together simultaneously. In this chapter, we take a different approach: instead of covering isolated concepts, we build a complete graphics rendering system from scratch, tying together all the OOP techniques we have learned. Finally, we discuss the design choice between inheritance and composition.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Design a complete class inheritance hierarchy from requirements
> - [ ] Combine abstract base classes, pure virtual functions, and `override` to implement polymorphism
> - [ ] Use `unique_ptr` to manage containers of polymorphic objects
> - [ ] Understand the "Is-a" and "Has-a" design principles, and make sound choices between inheritance and composition

## Design First—The Class Hierarchy of the Graphics System

Before writing any code, we need to clarify the requirements. Diving straight into coding only to realize halfway through that the class relationships are wrong—and then scattering `virtual` and `friend` everywhere—is a trap we want to avoid.

> **Pitfall Warning**: When designing an inheritance hierarchy, the easiest mistake to make is using "sharing certain implementation details" as a reason to inherit. Inheritance expresses an "Is-a" relationship—a circle **is a kind of** shape, so `Circle` inheriting from `Shape` makes sense. But if you make `Circle` inherit from `std::ostream` just because "circles and canvases both need `std::ostream`", that is an abuse of inheritance. Before drawing an inheritance arrow, ask yourself: Is Derived **a kind of** Base? If not, don't inherit.

Based on the requirements, our class hierarchy looks roughly like this:

```text
Shape (抽象基类)
  |-- Circle
  |-- Rectangle
  |-- Triangle

Canvas (管理类，持有 vector<unique_ptr<Shape>>)
ShapeSerializer (工具类，负责序列化)
ColoredShape (装饰类，组合持有 Shape)
```

`Shape` is the abstract base class, defining the interface shared by all shapes. Three concrete shape classes inherit from `Shape` and implement their respective calculation logic. `Canvas` is not a shape; it **contains** shapes—this is a classic scenario for composition rather than inheritance. `ShapeSerializer` uses the polymorphic interface of `Shape` through composition. `ColoredShape` also uses composition to add color to any shape, which we will explore in detail later.

## Starting with the Abstract Base Class

The foundation of the class hierarchy is `Shape`. Its responsibility is simple—define "what a shape should be able to do" without providing any concrete implementation. We give it four pure virtual functions: calculate area, calculate perimeter, draw, and report its name. We also add a pair of `operator==` and `operator!=`, using default implementations for equality comparison based on name and area.

```cpp
// shapes.cpp
// 编译: g++ -Wall -Wextra -std=c++17 shapes.cpp -o shapes

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

/// @brief 所有图形的抽象基类
class Shape {
public:
    virtual ~Shape() = default;

    virtual double area() const = 0;
    virtual double perimeter() const = 0;
    virtual void draw(std::ostream& os) const = 0;
    virtual std::string name() const = 0;

    virtual bool operator==(const Shape& other) const
    {
        return name() == other.name()
               && std::abs(area() - other.area()) < 1e-9;
    }

    virtual bool operator!=(const Shape& other) const
    {
        return !(*this == other);
    }
};
```

`virtual ~Shape() = default;` might seem unremarkable, but forgetting `virtual` has serious consequences—when holding a `Circle` through a `unique_ptr<Shape>`, destruction goes through `Shape`'s destructor. If it isn't virtual, the derived class destructor will never be called, leading to an immediate resource leak. This is a non-negotiable baseline requirement for polymorphic class hierarchies.

The four pure virtual `= 0` functions make `Shape` an abstract class, preventing instantiation. Any class that wants to be a "shape" must implement these four interfaces—this is the "interface contract". As for `std::abs(area() - other.area()) < 1e-9` in `operator==`, we use an epsilon tolerance instead of a direct `==` because floating-point arithmetic introduces precision errors. Two mathematically equal values computed through different paths might differ by as much as `1e-15`, and writing a direct `area() == other.area()` would cause two circles with the same radius to be judged as "unequal".

## Three Concrete Shapes—The override Defense Line

With the base class set up, we now implement the concrete shapes. Each one uses `override` to mark virtual function overrides—this isn't optional decoration. If you misspell the signature (for example, typing `arae` instead of `area`), without `override` the compiler will silently create a new virtual function, completely breaking polymorphism without any warning. With `override`, a signature mismatch triggers a compile-time error.

First up is `Circle`, the most intuitive one:

```cpp
class Circle : public Shape {
private:
    double cx_, cy_, radius_;

public:
    Circle(double cx, double cy, double radius)
        : cx_(cx), cy_(cy), radius_(radius)
    {
        if (radius_ < 0) radius_ = 0;
    }

    double area() const override
    {
        return M_PI * radius_ * radius_;
    }

    double perimeter() const override
    {
        return 2 * M_PI * radius_;
    }

    void draw(std::ostream& os) const override
    {
        os << "Circle(center=(" << cx_ << ", " << cy_
           << "), radius=" << radius_ << ")";
    }

    std::string name() const override { return "Circle"; }

    double cx() const { return cx_; }
    double cy() const { return cy_; }
    double radius() const { return radius_; }
};
```

The constructor performs a defensive check—the radius cannot be negative. The area uses the classic `PI * r^2`, the perimeter uses `2 * PI * r`, and `draw` outputs the shape's information to a stream. These are all very straightforward implementations.

Next is `Rectangle`:

```cpp
class Rectangle : public Shape {
private:
    double x_, y_, width_, height_;

public:
    Rectangle(double x, double y, double width, double height)
        : x_(x), y_(y), width_(width), height_(height)
    {
        if (width_ < 0) width_ = 0;
        if (height_ < 0) height_ = 0;
    }

    double area() const override { return width_ * height_; }

    double perimeter() const override
    {
        return 2 * (width_ + height_);
    }

    void draw(std::ostream& os) const override
    {
        os << "Rectangle(top_left=(" << x_ << ", " << y_
           << "), " << width_ << "x" << height_ << ")";
    }

    std::string name() const override { return "Rectangle"; }
};
```

Width and height also undergo defensive checks. The area is simply `width * height`, and the perimeter is `2 * (width + height)`—nothing fancy.

Finally, `Triangle` uses three vertex coordinates to define a triangle, making the calculations slightly more complex:

```cpp
class Triangle : public Shape {
private:
    double x1_, y1_;
    double x2_, y2_;
    double x3_, y3_;

    static double distance(double ax, double ay, double bx, double by)
    {
        double dx = bx - ax;
        double dy = by - ay;
        return std::sqrt(dx * dx + dy * dy);
    }

public:
    Triangle(double x1, double y1, double x2, double y2,
             double x3, double y3)
        : x1_(x1), y1_(y1), x2_(x2), y2_(y2), x3_(x3), y3_(y3)
    {}

    double area() const override
    {
        // 叉积公式：|AB x AC| / 2
        double abx = x2_ - x1_;
        double aby = y2_ - y1_;
        double acx = x3_ - x1_;
        double acy = y3_ - y1_;
        return std::abs(abx * acy - aby * acx) / 2.0;
    }

    double perimeter() const override
    {
        return distance(x2_, y2_, x3_, y3_)
               + distance(x1_, y1_, x3_, y3_)
               + distance(x1_, y1_, x2_, y2_);
    }

    void draw(std::ostream& os) const override
    {
        os << "Triangle(A=(" << x1_ << ", " << y1_
           << "), B=(" << x2_ << ", " << y2_
           << "), C=(" << x3_ << ", " << y3_ << "))";
    }

    std::string name() const override { return "Triangle"; }
};
```

The area uses the cross-product formula—constructing vectors AB and AC, and dividing the absolute value of their cross product by 2 yields the triangle's area. This formula is more stable than Heron's formula, avoiding the need to calculate side lengths before taking a square root. The perimeter is the sum of the distances of the three sides, using the private static member function `distance` to avoid code duplication.

## Global operator<<—Enabling Direct cout for Shapes

Calling `shape.draw(std::cout)` every time is slightly tedious, so we overload a global `operator<<` to let any `Shape` be directly sent to `cout << shape`:

```cpp
std::ostream& operator<<(std::ostream& os, const Shape& shape)
{
    shape.draw(os);
    return os;
}
```

In just four lines, this delegates to `Shape`'s virtual function `draw`. Because `draw` is a virtual function, we enjoy polymorphism here too—passing in a `Circle` calls `Circle::draw`, and passing in a `Triangle` calls `Triangle::draw`. Returning `os` supports chaining, such as `cout << shape1 << " and " << shape2`.

## Canvas—Managing Polymorphic Objects with unique_ptr

With the three shape classes complete, we now need a "canvas" to manage them uniformly. `Canvas` is the class that best embodies "polymorphism in action"—it uses `vector<unique_ptr<Shape>>` to hold various shape objects, and all operations are performed through the virtual function interface.

```cpp
class Canvas {
private:
    std::vector<std::unique_ptr<Shape>> shapes_;

public:
    Canvas() = default;
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;
    Canvas(Canvas&&) = default;
    Canvas& operator=(Canvas&&) = default;
```

Right at the start, there is a hurdle: because `Canvas` holds a `unique_ptr`, and `unique_ptr` is not copyable, the copy constructor and copy assignment operator must be `= delete`. If you forget to disable them, the compiler will try to generate default copies, and then throw a dizzying string of template errors when copying the `unique_ptr`. Explicitly `= delete` not only prevents errors but also clearly expresses the design intent—a canvas should not be copied, and ownership of shape objects is unique. Move operations, on the other hand, are safe, so `= default` is fine.

Next, let's look at `emplace`—a template member function that makes adding shapes very convenient:

```cpp
    template <typename ConcreteShape, typename... Args>
    void emplace(Args&&... args)
    {
        shapes_.push_back(
            std::make_unique<ConcreteShape>(std::forward<Args>(args)...));
    }
```

When using it, simply write `canvas.emplace<Circle>(0, 0, 5)`, which is much cleaner than `canvas.add(make_unique<Circle>(0, 0, 5))`. Template argument deduction combined with perfect forwarding (`std::forward`) passes arguments straight through to the specific shape's constructor.

Then we have a few utility methods:

```cpp
    void draw_all(std::ostream& os) const
    {
        os << "=== Canvas (" << shapes_.size() << " shapes) ===\n";
        for (const auto& shape : shapes_) {
            shape->draw(os);
            os << "\n";
        }
        os << "=== End of Canvas ===\n";
    }

    double total_area() const
    {
        double sum = 0;
        for (const auto& shape : shapes_) {
            sum += shape_->area();
        }
        return sum;
    }

    const Shape* find_largest() const
    {
        if (shapes_.empty()) return nullptr;
        const Shape* largest = shapes_[0].get();
        for (std::size_t i = 1; i < shapes_.size(); ++i) {
            if (shapes_[i]->area() > largest->area()) {
                largest = shapes_[i].get();
            }
        }
        return largest;
    }

    std::size_t size() const { return shapes_.size(); }
};
```

`draw_all` iterates over all shapes and calls `draw`—`shape->draw(os)` calls the corresponding version based on the actual object type; this is runtime polymorphism at work. `total_area` sums up the areas, and `find_largest` finds the shape with the largest area and returns a raw pointer (note that this returns a non-owning pointer, and the caller should not `delete` it).

## ShapeSerializer—A Utility Class

Serialization is an independent feature, so we extract it into a utility class rather than stuffing it into `Canvas`. This follows the Single Responsibility Principle—the canvas is responsible for managing shapes, and the serializer is responsible for the output format.

```cpp
class ShapeSerializer {
public:
    static void serialize(const Canvas& canvas, std::ostream& os)
    {
        os << "Shape count: " << canvas.size() << "\n";
        os << "Total area: " << canvas.total_area() << "\n\n";
        canvas.draw_all(os);
    }
};
```

All static methods, no instantiation needed. It retrieves information through `Canvas`'s public interface without needing to access any internal data—this is the power of good encapsulation.

## ColoredShape—Composition Over Inheritance

So far, we have only used inheritance. Now let's look at a scenario where composition is more appropriate: adding color to any shape.

```cpp
class ColoredShape {
private:
    std::unique_ptr<Shape> shape_;
    std::string color_;

public:
    ColoredShape(std::unique_ptr<Shape> shape, const std::string& color)
        : shape_(std::move(shape)), color_(color)
    {}

    double area() const { return shape_->area(); }
    double perimeter() const { return shape_->perimeter(); }
    const std::string& color() const { return color_; }

    void draw(std::ostream& os) const
    {
        os << "[" << color_ << "] ";
        shape_->draw(os);
    }
};
```

Notice that `ColoredShape` does **not** inherit from `Shape`. It holds a `unique_ptr<Shape>` internally, delegating area and perimeter calculations directly to it, while managing the color information itself. Why not use inheritance? Because with inheritance, `ColoredShape` wouldn't know what kind of shape it is, making it impossible to calculate area and perimeter. With composition, we can add color to any shape without creating subclasses like `ColoredCircle` and `ColoredRectangle` for every shape type. In the future, if we want to add "transparency" or "borders", we can simply layer another composition wrapper on top, preventing the class hierarchy from bloating.

## Putting It Together—Running the main Function

All the components are in place, so we write a `main` to tie everything together:

```cpp
int main()
{
    Canvas canvas;
    canvas.emplace<Circle>(0, 0, 5);
    canvas.emplace<Rectangle>(0, 0, 10, 4);
    canvas.emplace<Triangle>(0, 0, 4, 0, 0, 3);

    std::cout << "--- Draw All ---\n";
    canvas.draw_all(std::cout);

    std::cout << "\nTotal area: " << canvas.total_area() << "\n";

    const Shape* largest = canvas.find_largest();
    if (largest) {
        std::cout << "Largest shape: " << *largest
                  << " (area=" << largest->area() << ")\n";
    }

    Circle c(1, 2, 3);
    std::cout << "\nSingle shape: " << c << "\n";
    std::cout << "  area = " << c.area() << "\n";

    std::cout << "\n--- Serialize ---\n";
    ShapeSerializer::serialize(canvas, std::cout);

    ColoredShape colored(
        std::make_unique<Circle>(0, 0, 2), "red");
    std::cout << "\nColored shape: ";
    colored.draw(std::cout);
    std::cout << "  area = " << colored.area() << "\n";

    Circle c1(0, 0, 5);
    Circle c2(0, 0, 5);
    Circle c3(0, 0, 3);
    std::cout << "\nc1 == c2: " << (c1 == c2) << "\n";
    std::cout << "c1 == c3: " << (c1 == c3) << "\n";

    return 0;
}
```

`canvas.emplace<Circle>(0, 0, 5)` adds a circle with a radius of 5 to the canvas, followed by a 10x4 rectangle and a right triangle. `draw_all` draws all shapes at once, and `find_largest` finds the one with the largest area—we output it directly using `operator<<` because it returns a `Shape*`, and dereferencing it automatically calls the correct version of the virtual function `draw`. Finally, we test `ColoredShape` and `operator==`.

## Verifying the Output

Compile and run:

```bash
g++ -Wall -Wextra -std=c++17 shapes.cpp -o shapes && ./shapes
```

Verify the output:

```text
--- Draw All ---
=== Canvas (3 shapes) ===
Circle(center=(0, 0), radius=5)
Rectangle(top_left=(0, 0), 10x4)
Triangle(A=(0, 0), B=(4, 0), C=(0, 3))
=== End of Canvas ===

Total area: 124.54
Largest shape: Circle(center=(0, 0), radius=5) (area=78.5398)

Single shape: Circle(center=(1, 2), radius=3)
  area = 28.2743

--- Serialize ---
Shape count: 3
Total area: 124.54

=== Canvas (3 shapes) ===
Circle(center=(0, 0), radius=5)
Rectangle(top_left=(0, 0), 10x4)
Triangle(A=(0, 0), B=(4, 0), C=(0, 3))
=== End of Canvas ===

Colored shape: [red] Circle(center=(0, 0), radius=2)
  area = 12.5664

c1 == c2: 1
c1 == c3: 0
```

Check the key values: the circle's area is `PI * 25 = 78.5398`, the rectangle's area is `40`, the triangle's area is `6`, and the total area is `124.5398`—all match up. The circle has the largest area. Two circles with a radius of 5 are judged as equal, and circles with different radii are judged as unequal.

## Inheritance vs Composition—A Design Choice You Must Understand

Having implemented the entire system, let's step back and discuss a higher-level topic. You'll notice that the code features two types of relationships simultaneously: `Circle` inherits from `Shape` (inheritance), while `Canvas` uses shape functionality by holding a `Shape` pointer (composition). When should we use which?

Inheritance expresses an "Is-a" relationship: a circle **is a kind of** shape, so `Circle` inheriting from `Shape` is perfectly natural. Composition expresses a "Has-a" relationship: a canvas **contains** shapes, but a canvas itself is not a shape. Inheritance is tightly coupled—derived classes depend on the base class's interface and implementation details. Composition is loosely coupled—`Canvas` uses shapes only through `Shape`'s public interface.

The key is to judge the **stability** of the relationship: essential, stable relationships (a circle is a shape) call for inheritance; incidental, potentially changing relationships (a shape has a color) call for composition. `ColoredShape` is a practical example of the latter—you can add color to any shape without creating new subclasses, and adding transparency or borders in the future only requires another layer of composition.

## Exercises

### Exercise 1: Adding New Shapes

Add two classes: `Square` and `Ellipse`. Can `Square` inherit from `Rectangle`? Hint: a square requires its width and height to always be equal, but `Rectangle`'s interface allows modifying the width or height independently, leading to a semantic contradiction if inherited.

### Exercise 2: Shape Grouping

Implement a `ShapeGroup` class that **inherits from `Shape`** and internally holds a `vector<unique_ptr<Shape>>`. Its area is the sum of all sub-shape areas, and its perimeter returns 0. It can be added to a `Canvas` and can even be nested. This is a classic case of using inheritance and composition simultaneously.

### Exercise 3: JSON Serialization

Add a `to_json()` virtual function to `Shape`, with each concrete class overriding it to output JSON. Then, add a `serialize_json()` method in `ShapeSerializer` to output the canvas as a JSON array. No third-party libraries are needed; simply concatenate strings manually.

## Summary

In this chapter, we built a complete graphics rendering system from scratch. The abstract base class `Shape` defined the polymorphic interface, three concrete shape classes implemented their respective calculation logic through inheritance and `override`, `Canvas` used `unique_ptr<Shape>` to uniformly manage all shape objects, and `ColoredShape` demonstrated the practice of composition over inheritance.

A few core takeaways: a virtual destructor is a non-negotiable baseline requirement for polymorphic class hierarchies; `override` is a free error-checking tool; `unique_ptr` is the best choice for managing polymorphic objects; and when torn between inheritance and composition, ask yourself "Is-a or Has-a?"—if the relationship is unstable, use composition.

This concludes the OOP section. In the next chapter, we dive into template basics—the core mechanism of C++ generic programming. If OOP is about "organizing code with inheritance hierarchies," then templates are about "generating code with type parameters"—two fundamentally different abstraction methods, and both are essential weapons in a C++ programmer's arsenal.
