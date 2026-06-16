---
chapter: 8
cpp_standard:
- 11
- 14
- 17
- 20
description: Comprehensively apply inheritance, polymorphism, and operator overloading
  to implement a complete shape drawing system, and discuss the design choice between
  inheritance versus composition.
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 多继承与虚继承
reading_time_minutes: 14
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: OOP in Practice
translation:
  source: documents/vol1-fundamentals/ch08/05-oop-in-practice.md
  source_hash: 5950795ab4c4eea079d00f78a4c8444713e5d063593f5a9a23084a8749eeb7a7
  translated_at: '2026-06-16T03:45:50.156835+00:00'
  engine: anthropic
  token_count: 3239
---
# OOP in Practice

So far, we have dismantled all the core components of OOP—classes and objects, construction and destruction, inheritance and polymorphism, operator overloading, and virtual inheritance. Each concept individually isn't overly complex, but in real-world projects, these components appear simultaneously and collaborate. In this chapter, we switch gears: instead of discussing scattered concepts, we will implement a complete graphics rendering system from start to finish, stringing together all the OOP techniques we've learned. Finally, we will discuss the design choice between inheritance versus composition.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Design a complete class inheritance hierarchy based on requirements
> - [ ] Comprehensively use abstract base classes, pure virtual functions, and `override` to implement polymorphism
> - [ ] Use `unique_ptr` to manage containers of polymorphic objects
> - [ ] Understand "Is-a" vs. "Has-a" design principles and make reasonable choices between inheritance and composition

## Design First—The Class Hierarchy of a Graphics System

Before writing code, let's clarify the requirements. Don't just start coding immediately; halfway through, you might find the class relationship designed incorrectly, and then you'll be adding `dynamic_cast` and `static_cast` everywhere—we don't do that.

> **Pitfall Warning**: When designing an inheritance hierarchy, the easiest mistake to make is using "sharing some implementation details" as a reason for inheritance. Inheritance expresses an "Is-a" relationship—a Circle **is a kind of** Shape, so `Circle` inheriting from `Shape` is reasonable. But if you make `Circle` inherit from `Canvas` just because "both Circle and Canvas need `draw()`", that is abusing inheritance. Before drawing an inheritance arrow, ask yourself: Is Derived **a kind of** Base? If not, don't inherit.

Based on requirements, our class hierarchy looks roughly like this:

```text
Shape (抽象基类)
  |-- Circle
  |-- Rectangle
  |-- Triangle

Canvas (管理类，持有 vector<unique_ptr<Shape>>)
ShapeSerializer (工具类，负责序列化)
ColoredShape (装饰类，组合持有 Shape)
```

`Shape` is the abstract base class, defining interfaces shared by all shapes. Three concrete shape classes inherit from `Shape` and implement their respective calculation logic. `Canvas` is not a shape; it **contains** shapes—this is a typical scenario of composition over inheritance. `Canvas` utilizes the polymorphic interface of `Shape` through composition. `ColoredShape` also uses composition to add color to any shape, which we will detail later.

## Starting with the Abstract Base Class

The foundation of the class hierarchy is `Shape`. Its responsibility is simple—define "what a shape should do" without providing any specific implementation. We give it four pure virtual functions: calculate area, calculate perimeter, draw, and report the name. Additionally, we add a set of `operator==` and `operator!=`, using default implementations for equality comparison based on name and area.

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

`virtual ~Shape()` looks insignificant, but forgetting to write it has serious consequences—when holding a `Shape` via `unique_ptr<Shape>`, the destructor used is `Shape`'s destructor. If it isn't virtual, the derived class's destructor will never be called, and a resource leak is imminent. This is a baseline requirement for polymorphic class hierarchies, with no exceptions.

The four pure virtual functions make `Shape` an abstract class, preventing instantiation. Any class that wants to be a "Shape" must implement these four interfaces—this is the "interface contract". As for `operator==` in `Shape`, we use an epsilon tolerance instead of direct `==` because floating-point arithmetic has precision errors. Two mathematically equal values might differ by a tiny amount after different calculation paths; using direct `==` could cause two circles with the same radius to be judged as "unequal".

## Three Concrete Shapes—The `override` Defense

With the base class set up, we now implement the concrete shapes. Each is marked with `override` for virtual function overrides—this isn't optional decoration. If you misspell the signature (e.g., typing `area` as `arae`), without `override` the compiler will silently create a new virtual function, polymorphism will fail directly without any warning. With `override`, a signature mismatch results in a direct compilation error.

First up, `Circle`, the most intuitive one:

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

The constructor performs defensive checks—radius cannot be negative. Area uses the classic $\pi r^2$, perimeter uses $2\pi r$, and `draw` outputs shape information to a stream. These are very straightforward implementations.

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

Width and height undergo similar defensive checks. Area is $w \times h$, perimeter is $2(w+h)$, nothing fancy.

Finally, `Triangle`, defined by three vertex coordinates, where the calculation is slightly more complex:

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

Area uses the cross-product formula—construct vectors AB and AC, and the absolute value of the cross product divided by 2 is the triangle's area. This formula is more stable than Heron's formula, avoiding the need to calculate side lengths first and then take a square root. Perimeter is the sum of the distances of the three sides, using the private static member function `distance` to avoid code duplication.

## Global `operator<<`—Enabling Direct `cout` for Shapes

Calling `draw()` every time is slightly annoying, so let's overload a global `operator<<` to allow any `Shape` to be used directly with `cout`:

```cpp
std::ostream& operator<<(std::ostream& os, const Shape& shape)
{
    shape.draw(os);
    return os;
}
```

Just four lines, delegating to `Shape`'s virtual function `draw`. Because `draw` is a virtual function, we enjoy polymorphism here too—pass in a `Circle` and `Circle::draw` is called, pass in a `Rectangle` and `Rectangle::draw` is called. Returning `ostream&` supports chaining, like `cout << shape << endl`.

## Canvas—`unique_ptr` Managing Polymorphic Objects

With the three shape classes written, we now need a "canvas" to manage them uniformly. `Canvas` is the class that best reflects "polymorphism in action"—it holds various shape objects using `unique_ptr`, and all operations are performed through virtual function interfaces.

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

Right at the start, there's a hurdle: because `Canvas` holds `unique_ptr<Shape>`, and `unique_ptr` is not copyable, the copy constructor and copy assignment must be deleted. If you forget to disable them, the compiler will try to generate default copies, then produce a dazzling string of template errors when copying the `unique_ptr`. Explicitly deleting them not only avoids errors but also clearly expresses design intent—the canvas shouldn't be copied, and ownership of shape objects is unique. Move operations are safe, so `= default` works.

Next, look at `addShape`—a template member function that makes adding shapes very convenient:

```cpp
    template <typename ConcreteShape, typename... Args>
    void emplace(Args&&... args)
    {
        shapes_.push_back(
            std::make_unique<ConcreteShape>(std::forward<Args>(args)...));
    }
```

Usage is as simple as `canvas.addShape<Circle>(5.0)`, much more concise than `canvas.addShape(std::make_unique<Circle>(5.0))`. Template argument deduction combined with perfect forwarding (`std::forward`) passes arguments intact to the specific shape's constructor.

Then there are several utility methods:

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

`drawAll` iterates through all shapes and calls `draw`—dynamic dispatch calls the corresponding version based on the actual object type; this is runtime polymorphism at work. `totalArea` sums the areas, and `maxAreaShape` finds the shape with the largest area and returns a raw pointer (note that this returns a non-owning pointer, the caller should not `delete` it).

## ShapeSerializer—Utility Class

Serialization is an independent feature, so we extract it into a utility class rather than stuffing it into `Canvas`. This follows the Single Responsibility Principle—the canvas manages shapes, the serializer handles output formatting.

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

All static methods, no instantiation needed. It retrieves information through `Shape`'s public interface, requiring no access to internal data—this is the power of good encapsulation.

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

Note that `ColoredShape` **does not** inherit from `Shape`. It holds a `unique_ptr<Shape>` internally and delegates area and perimeter calculations directly to it, while managing color information itself. Why not use inheritance? Because if we used inheritance, `ColoredShape` wouldn't know what kind of shape it is and couldn't calculate area or perimeter. With composition, you can add color to any shape without creating subclasses like `ColoredCircle`, `ColoredRectangle` for every shape type. In the future, if you want to add "transparency" or "borders", you simply layer on more composition; the class hierarchy won't bloat.

## Live Fire—Testing in `main`

All components are in place; let's write a `main` function to tie them together:

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

`main` stuffs a circle with radius 5, a 10x4 rectangle, and a right-angled triangle into the canvas. `drawAll` draws all shapes at once, `maxAreaShape` finds the largest one—using `cout << *` works because it returns a `Shape*`, and dereferencing it automatically calls the correct version of the virtual function `draw`. Finally, we test `operator==` and `operator!=`.

## Verification

Compile and run:

```bash
g++ -Wall -Wextra -std=c++17 shapes.cpp -o shapes && ./shapes
```

Verify output:

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

Check key values: Circle area ~78.54, Rectangle area 40.00, Triangle area 6.00, Total Area ~124.54 match. The largest area is the circle. Two circles with radius 5 are judged equal, and different radii are judged unequal.

## Inheritance vs Composition—A Design Choice You Must Get Right

With the system implemented, let's step back and discuss a higher-level topic. You will notice two types of relationships in the code: `Circle` inherits from `Shape` (Inheritance), while `Canvas` uses shape functionality by holding a `unique_ptr<Shape>` (Composition). When do you use which?

Inheritance expresses an "Is-a" relationship: A Circle **is a kind of** Shape, so `Circle` inheriting from `Shape` is natural. Composition expresses a "Has-a" relationship: A Canvas **contains** Shapes, but a Canvas is not itself a Shape. Inheritance is high coupling—derived classes depend on the base class's interface and implementation details. Composition is loose coupling—`Canvas` only uses shapes through `Shape`'s public interface.

The key is judging the **stability** of the relationship: Essential, stable relationships (Circle is a Shape) use inheritance; Accidental, variable relationships (Shape has a color) use composition. `ColoredShape` is a practical example of the latter—you can add color to any shape without creating new subclasses, and adding transparency or borders later just requires another layer of composition.

## Exercises

### Exercise 1: Add New Shapes

Add `Square` and `Ellipse` classes. Should `Square` inherit from `Rectangle`? Hint: A square requires width and height to always be equal, but `Rectangle`'s interface allows modifying width or height independently. Inheritance would lead to a semantic contradiction.

### Exercise 2: Shape Grouping

Implement a `ShapeGroup` class that **inherits from `Shape`** and internally holds a `vector<unique_ptr<Shape>>`. Its area is the sum of all sub-shape areas, and its perimeter returns 0. It can be added to a `Canvas` or even nested. This is a classic case where inheritance and composition are used simultaneously.

### Exercise 3: JSON Serialization

Add a `toJson()` virtual function to `Shape`, where each concrete class overrides it to output JSON. Then add a `toJson()` method in `Canvas` to output the canvas as a JSON array. No third-party libraries are needed; manually splicing strings is sufficient.

## Summary

In this chapter, we implemented a complete graphics rendering system from scratch. The abstract base class `Shape` defined the polymorphic interface, three concrete shape classes implemented their respective calculation logic through inheritance and `override`, `Canvas` used `unique_ptr` to uniformly manage all shape objects, and `ColoredShape` demonstrated the practice of composition over inheritance.

A few core takeaways: Virtual destructors are a baseline requirement for polymorphic class hierarchies; `override` is a free error-checking tool; `unique_ptr` is the best choice for managing polymorphic objects. When hesitating between inheritance and composition, ask yourself "Is-a or Has-a?"—if the relationship isn't stable, use composition.

The OOP section ends here. The next chapter enters Template Basics—the core mechanism of C++ generic programming. If OOP is "organizing code with inheritance hierarchies," then templates are "generating code with type parameters"—two completely different abstraction methods, and both are essential weapons for a C++ programmer.
