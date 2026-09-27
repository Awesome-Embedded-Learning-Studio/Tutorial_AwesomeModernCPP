---
chapter: 8
cpp_standard:
- 11
- 14
- 17
- 20
description: Combine inheritance, polymorphism, and operator overloading to build
  a complete shape-drawing system, and revisit the inheritance vs composition design
  choice.
difficulty: intermediate
order: 6
platform: host
prerequisites:
- Multiple Inheritance and Virtual Inheritance
reading_time_minutes: 14
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: OOP in Practice
translation:
  source: documents/vol1-fundamentals/ch08/06-oop-in-practice.md
  source_hash: 2e777d0f35eef612f64cca8c0b3c9cc7684ef7dd14b73b883a7a87a611372114
  translated_at: '2026-09-25T11:39:25+00:00'
  engine: anthropic
  token_count: 7800
---
# OOP in Practice: Building a Shape-Drawing System from Scratch

By this point we have taken every core OOP topic apart—classes and objects, construction and destruction, inheritance and polymorphism, operator overloading, virtual inheritance. Each topic on its own is not that complicated, but in a real project they all walk on stage at the same time and have to cooperate. So this chapter plays the game differently: instead of covering knowledge points in scattered pieces, we implement a complete shape-drawing system from start to finish, stringing together all the OOP techniques we have learned in one go, and at the end we also discuss the design choice of inheritance vs composition.

## Design First: The Shape System's Class Hierarchy

Before writing any code, let's get the requirements straight. Grabbing the requirements and charging straight into code, discovering halfway through that the class relationships were designed wrong, then sprinkling `virtual` and `friend` everywhere—that is not how we do things.

The easiest mistake to make when designing an inheritance hierarchy is treating "shares some implementation details" as a reason to inherit. Inheritance expresses an is-a relationship—a circle **is a** shape, so having `Circle` inherit `Shape` is reasonable. But if you make `Circle` inherit from `std::ostream` just because "both circles and canvases need `std::ostream`", that is inheritance abuse. Before drawing every inheritance arrow, we ask ourselves first: is Derived **a kind of** Base? If not, don't inherit.

Based on the requirements, our class hierarchy looks roughly like this:

```text
Shape (abstract base class)
  |-- Circle
  |-- Rectangle
  |-- Triangle

Canvas (manager class, holds vector<unique_ptr<Shape>>)
ShapeSerializer (utility class, handles serialization)
ColoredShape (decorator class, holds a Shape by composition)
```

`Shape` is the abstract base class that defines the interface shared by all shapes. The three concrete shape classes inherit `Shape` and implement their own computation logic. `Canvas` is not a shape—it **contains** shapes, a textbook case of composition rather than inheritance. `ShapeSerializer` uses `Shape`'s polymorphic interface through composition. `ColoredShape` likewise uses composition to add color to any shape; we will unpack it in detail later.

## Starting from the Abstract Base Class

The root of the class hierarchy is `Shape`. Its responsibility is simple—define "what a shape should be able to do" without providing any concrete implementation. We give it four pure virtual functions: compute the area, compute the perimeter, draw itself, and report its name. On top of those, a pair of `operator==` and `operator!=` with default implementations doing equality comparison based on name and area.

```cpp
// shapes.cpp
// Compile: g++ -Wall -Wextra -std=c++17 shapes.cpp -o shapes

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

/// @brief Abstract base class for all shapes
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

`virtual ~Shape() = default;` looks unremarkable, but forgetting the `virtual` has serious consequences—when a `Circle` is held through a `unique_ptr<Shape>`, destruction goes through `Shape`'s destructor, and if that destructor is not virtual, the derived class's destructor never gets called; a resource leak is staring us in the face. This is the bottom-line requirement for a polymorphic class hierarchy, no exceptions.

The four `= 0` pure virtual functions make `Shape` an abstract class that cannot be instantiated. Any class that wants to count as a "shape" must implement these four functions—this is the "interface contract". As for the `std::abs(area() - other.area()) < 1e-9` inside `operator==`: we use an epsilon tolerance instead of a plain `==` because floating-point arithmetic carries precision error. Two mathematically equal values that traveled different computation paths can differ by as much as `1e-15`, so writing `area() == other.area()` directly would get two circles of the same radius judged "not equal".

## Three Concrete Shapes: The override Line of Defense

With the base class in place, we now start implementing the concrete shapes. Every one of them marks its virtual function overrides with `override`—this is not optional decoration. If we mistype a signature (say, typing `arae` for `area`), then without `override` the compiler silently creates a brand-new virtual function; polymorphism quietly stops working, without a single warning. With `override`, a mismatched signature is a compile error on the spot.

Let's write `Circle` first, the most intuitive one:

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

We run a defensive check in the constructor—the radius cannot be negative. Area uses the classic `PI * r^2`, perimeter uses `2 * PI * r`, and `draw` writes the shape's information to a stream. All very straightforward implementations.

Next, `Rectangle`:

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

Width and height get the same defensive check. Area is just `width * height`, perimeter is `2 * (width + height)`—no tricks here.

Finally we write `Triangle`, where three vertex coordinates pin down a triangle and the math gets slightly more involved:

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
        // Cross product formula: |AB x AC| / 2
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

The area uses the cross product formula—build vectors AB and AC, and the absolute value of their cross product divided by 2 is the triangle's area. This formula is more stable than Heron's formula, which requires computing the side lengths first and then taking a square root. The perimeter is the sum of the three side lengths, and we use a private static member function `distance` to avoid duplicated code.

## A Global operator<<: Streaming Shapes Straight to cout

Calling `shape.draw(std::cout)` every single time is a bit annoying, so let's overload a global `operator<<` so every `Shape` can be written directly as `cout << shape`:

```cpp
std::ostream& operator<<(std::ostream& os, const Shape& shape)
{
    shape.draw(os);
    return os;
}
```

Four short lines, and all they do is delegate to `Shape`'s virtual function `draw`. Because `draw` is virtual, we enjoy polymorphism here too—pass in a `Circle` and `Circle::draw` gets called; pass in a `Triangle` and `Triangle::draw` gets called. Returning `os` is what supports chained calls, like `cout << shape1 << " and " << shape2`.

## Canvas: Managing Polymorphic Objects with unique_ptr

With the three shape classes written, we now need a "canvas" to manage them uniformly. `Canvas` is the class that best embodies "polymorphism in practice"—it holds a variety of shape objects in a `vector<unique_ptr<Shape>>`, and every operation goes through the virtual function interface.

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

There is a hurdle right at the top: because `Canvas` holds `unique_ptr`s, and `unique_ptr` is not copyable, the copy constructor and copy assignment must be `= delete`d. If we forget to disable them, the compiler tries to generate the default copies and then throws a dizzying wall of template errors while copying the `unique_ptr`s. Actively writing `= delete` not only avoids the errors but also expresses the design intent clearly—a canvas should not be copied; ownership of the shape objects is exclusive. Move operations, on the other hand, are safe, so `= default` is fine.

Next up is `emplace`—a template member function that makes adding shapes very smooth:

```cpp
    template <typename ConcreteShape, typename... Args>
    void emplace(Args&&... args)
    {
        shapes_.push_back(
            std::make_unique<ConcreteShape>(std::forward<Args>(args)...));
    }
```

In use we just write `canvas.emplace<Circle>(0, 0, 5)`, which is a good deal tidier than `canvas.add(make_unique<Circle>(0, 0, 5))`. Template argument deduction teams up with perfect forwarding (`std::forward`) so the arguments reach the concrete shape's constructor untouched.

Then we write a few functional methods:

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
            sum += shape->area();
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

We have `draw_all` iterate over all shapes and call `draw`—`shape->draw(os)` dispatches to the corresponding version based on the actual object's type; this is runtime polymorphism doing real work. `total_area` sums up the areas, and `find_largest` finds the shape with the largest area and returns a raw pointer (note that what we return here is a non-owning pointer; the caller should not `delete` it).

## ShapeSerializer: A Utility Class

Serialization is an independent piece of functionality, so we pull it out into a utility class instead of stuffing it into `Canvas`. This follows the single responsibility principle—the canvas manages shapes, and the serializer owns the output format.

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

All static methods, no instantiation needed. It gathers information through `Canvas`'s public interface, so we never need to touch the internal data—this is the power of good encapsulation.

## ColoredShape: Composition over Inheritance

Everything so far has been inheritance. Now let's look at a scenario where composition is the better fit: adding color to an arbitrary shape.

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

Notice that `ColoredShape` does **not** inherit from `Shape`. It holds a `unique_ptr<Shape>` internally, delegates area and perimeter computation straight to it, and manages the color information itself. Why not inheritance? Because with inheritance, `ColoredShape` would not know which shape it is and could not compute the area or perimeter. With composition, we can add color to any shape without creating subclasses like `ColoredCircle` and `ColoredRectangle` for every kind of shape. Later, when we want "with transparency" or "with a border", composition stacks on one more layer the same way, and the class hierarchy never bloats.

## Time to Log On: Taking main for a Test Drive

All the parts are in place; let's write a `main` to string them together:

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

`canvas.emplace<Circle>(0, 0, 5)` drops a circle of radius 5 onto the canvas, followed by a 10x4 rectangle and a right triangle. `draw_all` draws all the shapes in one go, and `find_largest` picks out the one with the largest area—printed directly with `operator<<`, because it returns a `Shape*`, and once dereferenced the virtual function `draw` automatically calls the right version. At the end we test `ColoredShape` and `operator==`.

## Verifying the Run

Compile and run:

```bash
g++ -Wall -Wextra -std=c++17 shapes.cpp -o shapes && ./shapes
```

Check the output:

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

Let's verify the key numbers: circle area `PI * 25 = 78.5398`, rectangle area `40`, triangle area `6`—the total `124.5398` checks out. The circle has the largest area. The two circles with radius 5 compare equal, and circles with different radii compare unequal.

## Inheritance vs Composition: The Criteria Confirmed in Practice

In article 1 we established the order of judgment: first ask whether composition works, then ask about is-a, and finally ask whether the behavior is substitutable. Looking back after finishing this system, the code contains both relationships side by side, and each passes its own test. `Circle` inherits `Shape`: a circle **is a** shape—an essential, stable relationship—and it needs to be operated uniformly by `Canvas` through base-class pointers, so both the is-a and the polymorphism requirements hold. `Canvas` holds shapes: a canvas **contains** shapes, but a canvas is not a shape; has-a means composition, and it uses shapes only through `Shape`'s public interface, so the coupling is naturally low.

Then look at `ColoredShape`, which confirms the other half: color is an accidental, changeable attribute of a shape, not an essential relationship, so composition layers it on—adding color to any shape requires no new subclass, and when transparency or borders come along later, one more layer does the job, and the class hierarchy does not bloat.

## Exercises

### Exercise 1: Add New Shapes

Please add two classes, `Square` and `Ellipse`. Can `Square` inherit from `Rectangle`? Hint: a square requires width and height to stay equal at all times, but `Rectangle`'s interface allows width or height to be modified independently—inheriting would lead to a semantic contradiction.

### Exercise 2: Grouping Shapes

Please implement a `ShapeGroup` class that **inherits from `Shape`** and internally holds a `vector<unique_ptr<Shape>>`. Its area is the sum of all child shapes' areas, and its perimeter returns 0. It can be added to a `Canvas`, and it can even be nested. This is a classic case of inheritance and composition being used at the same time.

### Exercise 3: JSON Serialization

Please add a `to_json()` virtual function to `Shape`, with each concrete class overriding it to output JSON. Then add a `serialize_json()` method to `ShapeSerializer` that outputs the canvas as a JSON array. No third-party library is needed—hand-stitching strings is enough.
