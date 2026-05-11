---
title: "OOP 实战"
description: "综合运用继承、多态和运算符重载，实现一个完整的图形绘制系统，并讨论继承 vs 组合的设计选择"
chapter: 8
order: 5
difficulty: intermediate
reading_time_minutes: 15
platform: host
prerequisites:
  - "多继承与虚继承"
tags:
  - cpp-modern
  - host
  - intermediate
  - 进阶
cpp_standard: [11, 14, 17, 20]
---

# OOP 实战

到目前为止，我们已经把 OOP 的核心零件全部拆解过一遍了——类与对象、构造与析构、继承与多态、运算符重载、虚继承。每个知识点单独拿出来不算复杂，但真正写项目的时候，这些零件是同时出场、互相配合的。这一章我们换一种玩法：不再零散地讲知识点，而是从头到尾实现一个完整的图形绘制系统，把前面学的所有 OOP 技术一次性串起来，最后还会讨论继承 vs 组合的设计选择。

> **学习目标**
>
> 完成本章后，你将能够：
>
> - [ ] 从需求出发设计完整的类继承体系
> - [ ] 综合运用抽象基类、纯虚函数和 `override` 实现多态
> - [ ] 使用 `unique_ptr` 管理多态对象的容器
> - [ ] 理解"Is-a"与"Has-a"的设计原则，在继承和组合之间做出合理选择

## 设计先行——图形系统的类体系

动手写代码之前，先把需求理清楚。拿到需求直接开撸，写到一半发现类关系设计错了，然后到处加 `virtual`、到处加 `friend`——这种事我们不做。

> **踩坑预警**：在设计继承体系时，最容易犯的错误是把"共享某些实现细节"作为继承的理由。继承表达的是"Is-a"关系——圆形**是一种**图形，所以 `Circle` 继承 `Shape` 合理。但如果你因为"圆形和画布都需要 `std::ostream`"就让 `Circle` 继承 `std::ostream`，那就是滥用继承。每次画继承箭头之前，先问自己：Derived **是一种** Base 吗？如果不是，别继承。

基于需求，我们的类体系大概长这样：

```text
Shape (抽象基类)
  |-- Circle
  |-- Rectangle
  |-- Triangle

Canvas (管理类，持有 vector<unique_ptr<Shape>>)
ShapeSerializer (工具类，负责序列化)
ColoredShape (装饰类，组合持有 Shape)
```

`Shape` 是抽象基类，定义所有图形共享的接口。三个具体图形类继承 `Shape` 并实现各自的计算逻辑。`Canvas` 不是一个图形，它**包含**图形——这是组合而非继承的典型场景。`ShapeSerializer` 通过组合使用 `Shape` 的多态接口。`ColoredShape` 也用组合的方式给任意图形添加颜色，后面会详细展开。

## 从抽象基类开始

类体系的根基是 `Shape`。它的职责很简单——定义"一个图形应该能做什么"，但不提供任何具体实现。我们给它四个纯虚函数：求面积、求周长、绘制、报上名来。另外再加一组 `operator==` 和 `operator!=`，用默认实现做基于名称和面积的相等比较。

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

`virtual ~Shape() = default;` 看起来不起眼，但忘了写 `virtual` 后果严重——通过 `unique_ptr<Shape>` 持有 `Circle` 时，析构走的是 `Shape` 的析构函数，如果不是 virtual 的，派生类析构根本不会被调用，资源泄漏就在眼前。这是多态类体系的底线要求，没有例外。

四个 `= 0` 的纯虚函数让 `Shape` 成为抽象类，无法实例化。任何想要成为"图形"的类都必须实现这四个接口——这就是"接口契约"。至于 `operator==` 里的 `std::abs(area() - other.area()) < 1e-9`，这里用 epsilon 容差而不是直接 `==`，是因为浮点运算存在精度误差。两个数学上相等的值经过不同的计算路径，可能差了 `1e-15` 这么多，直接写 `area() == other.area()` 会导致半径相同的两个圆被判为"不等"。

## 三个具体图形——override 防线

基类搭好了，现在开始实现具体图形。每一个都用 `override` 标注虚函数重写——这不是可选装饰，如果你拼错了签名（比如把 `area` 打成 `arae`），没有 `override` 的话编译器会默默创建一个新的虚函数，多态直接失效且不会有任何警告。加了 `override`，签名不匹配直接编译报错。

先来 `Circle`，最直观的一个：

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

构造函数里做了防御性检查——半径不能为负。面积用经典的 `PI * r^2`，周长用 `2 * PI * r`，`draw` 把图形信息输出到流。这些都是非常直接的实现。

接下来是 `Rectangle`：

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

宽和高同样做了防御性检查。面积就是 `width * height`，周长是 `2 * (width + height)`，没什么花活。

最后是 `Triangle`，三个顶点坐标确定一个三角形，计算稍微复杂一点：

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

面积用了叉积公式——构造向量 AB 和 AC，叉积的绝对值除以 2 就是三角形面积。这个公式比海伦公式更稳定，不需要先算边长再开根号。周长则是三条边的距离之和，用私有静态成员函数 `distance` 来避免重复代码。

## 全局 operator<<——让图形能直接 cout

每次都调用 `shape.draw(std::cout)` 稍微有点烦，我们来重载一个全局的 `operator<<`，让所有 `Shape` 都能直接 `cout << shape`：

```cpp
std::ostream& operator<<(std::ostream& os, const Shape& shape)
{
    shape.draw(os);
    return os;
}
```

短短四行，做的事情就是委托给 `Shape` 的虚函数 `draw`。因为 `draw` 是虚函数，这里同样享受多态——传入 `Circle` 就调用 `Circle::draw`，传入 `Triangle` 就调用 `Triangle::draw`。返回 `os` 是为了支持链式调用，比如 `cout << shape1 << " and " << shape2`。

## Canvas——unique_ptr 管理多态对象

三个图形类写完了，现在需要一个"画布"来统一管理它们。`Canvas` 是最能体现"多态实战"的类——它用 `vector<unique_ptr<Shape>>` 持有多种图形对象，所有操作通过虚函数接口完成。

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

开头就有一道坎：因为 `Canvas` 持有 `unique_ptr`，而 `unique_ptr` 不可拷贝，所以拷贝构造和拷贝赋值必须 `= delete`。如果你忘了禁用，编译器会尝试生成默认拷贝，然后在拷贝 `unique_ptr` 时报出一串让人眼花缭乱的模板错误。主动 `= delete` 不仅避免报错，更清晰地表达了设计意图——画布不应该被拷贝，图形对象的所有权是唯一的。移动操作倒是安全的，`= default` 就行。

接下来看 `emplace`——一个模板成员函数，让添加图形变得很顺手：

```cpp
    template <typename ConcreteShape, typename... Args>
    void emplace(Args&&... args)
    {
        shapes_.push_back(
            std::make_unique<ConcreteShape>(std::forward<Args>(args)...));
    }
```

用的时候写 `canvas.emplace<Circle>(0, 0, 5)` 就行，比 `canvas.add(make_unique<Circle>(0, 0, 5))` 简洁不少。模板参数推导配合完美转发（`std::forward`），参数原封不动地传给具体图形的构造函数。

然后是几个功能方法：

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

`draw_all` 遍历所有图形并调用 `draw`——`shape->draw(os)` 根据实际对象类型调用对应版本，这就是运行时多态在干活。`total_area` 汇总面积，`find_largest` 找出面积最大的图形并返回裸指针（注意这里返回的是非拥有指针，调用者不应该 `delete` 它）。

## ShapeSerializer——工具类

序列化是个独立的功能，我们把它抽成一个工具类，而不是塞进 `Canvas` 里。这遵循单一职责原则——画布负责管理图形，序列化器负责输出格式。

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

全静态方法，不需要实例化。通过 `Canvas` 的公有接口获取信息，完全不需要访问内部数据——这就是良好封装的威力。

## ColoredShape——组合优于继承

到目前为止我们用的都是继承。现在来看一个用组合更合适的场景：给任意图形添加颜色。

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

注意 `ColoredShape` **没有**继承 `Shape`。它内部持有一个 `unique_ptr<Shape>`，计算面积和周长直接委托给它，颜色信息自己管理。为什么不用继承？因为如果用继承，`ColoredShape` 不知道自己是什么图形，无法计算面积和周长。而用组合，你可以给任何图形加颜色，不需要为每种图形创建 `ColoredCircle`、`ColoredRectangle` 这样的子类。将来想加"带透明度"或"带边框"，同样用组合一层套一层，类体系不会膨胀。

## 上号——main 函数试跑

所有零件都就位了，写个 `main` 来串起来：

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

`canvas.emplace<Circle>(0, 0, 5)` 往画布里塞了一个半径 5 的圆，接着是 10x4 的矩形和直角三角形。`draw_all` 一次性画出所有图形，`find_largest` 找出面积最大的那个——用 `operator<<` 直接输出，因为它返回的是 `Shape*`，解引用后虚函数 `draw` 自动调用正确版本。最后测试了 `ColoredShape` 和 `operator==`。

## 验证运行

编译运行：

```bash
g++ -Wall -Wextra -std=c++17 shapes.cpp -o shapes && ./shapes
```

验证输出：

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

核对关键数值：圆面积 `PI * 25 = 78.5398`，矩形面积 `40`，三角形面积 `6`，总面积 `124.5398` 吻合。面积最大的是圆。两个半径为 5 的圆判等成功，半径不同判不等。

## 继承 vs 组合——必须搞清楚的设计选择

实现完了整个系统，回过头来聊一个更高层次的话题。你会发现代码里同时出现了两种关系：`Circle` 继承了 `Shape`（继承），而 `Canvas` 通过持有 `Shape` 指针来使用图形功能（组合）。什么时候用哪种？

继承表达"Is-a"关系：圆形**是一种**图形，`Circle` 继承 `Shape` 天经地义。组合表达"Has-a"关系：画布**包含**图形，但画布本身不是图形。继承是高耦合的——派生类依赖基类的接口和实现细节。组合是松耦合的——`Canvas` 只通过 `Shape` 的公有接口来使用图形。

关键是判断关系的**稳定性**：本质的、稳定的关系（圆形是图形）用继承；偶然的、可能变化的关系（图形有颜色）用组合。`ColoredShape` 就是后者的实战案例——你给任何图形加颜色都不需要新建子类，将来加透明度、加边框也只需再套一层组合。

## 练习

### 练习 1：添加新图形

添加 `Square` 和 `Ellipse` 两个类。`Square` 可以继承 `Rectangle` 吗？提示：正方形要求宽高始终相等，但 `Rectangle` 的接口允许单独修改宽度或高度，继承会导致语义矛盾。

### 练习 2：图形分组

实现一个 `ShapeGroup` 类，它**继承 `Shape`** 且内部持有 `vector<unique_ptr<Shape>>`。面积是所有子图形面积之和，周长返回 0。它可以被添加到 `Canvas` 中，甚至可以嵌套。这是继承和组合同时使用的经典案例。

### 练习 3：JSON 序列化

为 `Shape` 增加 `to_json()` 虚函数，每个具体类重写它输出 JSON。然后在 `ShapeSerializer` 中添加 `serialize_json()` 方法，将画布输出为 JSON 数组。不需要第三方库，手动拼接字符串即可。

## 小结

这一章我们从头到尾实现了一个完整的图形绘制系统。抽象基类 `Shape` 定义了多态接口，三个具体图形类通过继承和 `override` 实现了各自的计算逻辑，`Canvas` 用 `unique_ptr<Shape>` 统一管理所有图形对象，`ColoredShape` 展示了组合优于继承的实践。

几个核心经验：虚析构函数是多态类体系的底线要求；`override` 是免费的查错工具；`unique_ptr` 是管理多态对象的最佳选择；在继承和组合之间犹豫时，问自己"Is-a 还是 Has-a"，关系不稳定就用组合。

OOP 篇到这里就全部结束了。下一章我们进入模板基础——C++ 泛型编程的核心机制。如果说 OOP 是"用继承体系组织代码"，那模板就是"用类型参数生成代码"——两种完全不同的抽象方式，都是 C++ 程序员必须掌握的武器。
