---
title: "虚函数与多态"
description: "理解 virtual、override 和 vtable 机制，掌握运行时多态的实现原理与正确用法"
chapter: 8
order: 2
difficulty: intermediate
reading_time_minutes: 15
platform: host
prerequisites:
  - "单继承"
tags:
  - cpp-modern
  - host
  - intermediate
  - 进阶
cpp_standard: [11, 14, 17, 20]
---

# 虚函数与多态

上一篇我们学了单继承——派生类继承了基类的成员，可以在其基础上扩展新的行为。但继承本身只解决了一半的问题：如果我们拿着一个基类指针去操作派生类对象，调用的却永远是基类版本的函数，那继承的表达力就被砍了一大截。虚函数正是补上这另一半的关键——它让"通过基类接口调用派生类实现"成为可能，这就是运行时多态。

我们今天要把这件事彻底搞透：`virtual` 到底做了什么，`override` 为什么应该永远写上，编译器在幕后搞的那张 vtable 是怎么运作的，以及一个忘写 `virtual` 析构函数会带来什么样的灾难。

## 没有 virtual 的世界——基类指针的"近视眼"

先直面问题。假设有一个简单的图形类层次：

```cpp
#include <cstdio>

class Shape {
public:
    void draw() const { printf("Shape::draw()\n"); }
};

class Circle : public Shape {
public:
    void draw() const { printf("Circle::draw()\n"); }
};

class Rectangle : public Shape {
public:
    void draw() const { printf("Rectangle::draw()\n"); }
};
```

三个类，`Circle` 和 `Rectangle` 都定义了自己的 `draw()`。看起来没什么问题——但当我们通过基类指针来调用时，事情就不对了：

```cpp
int main() {
    Shape* shapes[3];
    shapes[0] = new Shape();
    shapes[1] = new Circle();
    shapes[2] = new Rectangle();

    for (int i = 0; i < 3; ++i) {
        shapes[i]->draw();
    }

    for (int i = 0; i < 3; ++i) {
        delete shapes[i];
    }
    return 0;
}
```

你期望输出三种不同的绘制行为，但实际运行结果是：

```text
Shape::draw()
Shape::draw()
Shape::draw()
```

三次全都是 `Shape::draw()`。编译器在编译 `shapes[i]->draw()` 时，只看到 `shapes[i]` 的静态类型是 `Shape*`，于是老老实实地绑定了 `Shape::draw()`。它根本不知道、也不关心这个指针在运行时实际指向的是 `Circle` 还是 `Rectangle`——这就是**静态绑定**（也叫早绑定）。在我们需要"统一接口、不同行为"的时候，静态绑定就是绊脚石，而 `virtual` 正是打破它的关键。

## virtual 关键字——让函数调用"等到运行时再定"

在基类的成员函数前面加上 `virtual`，一切就不一样了：

```cpp
class Shape {
public:
    virtual void draw() const {   // 加上 virtual
        printf("Shape::draw()\n");
    }
};

class Circle : public Shape {
public:
    void draw() const override {  // 隐式虚函数
        printf("Circle::draw()\n");
    }
};

class Rectangle : public Shape {
public:
    void draw() const override {
        printf("Rectangle::draw()\n");
    }
};
```

只需要在基类的 `draw()` 前面加一个 `virtual`，派生类中签名匹配的同名函数自动也是虚函数。现在我们再跑一遍刚才的循环：

输出变成了：

```text
Shape::draw()
Circle::draw()
Rectangle::draw()
```

每个对象根据自己**实际的类型**调用了对应版本的 `draw()`——这就是**动态绑定**（也叫晚绑定），即**运行时多态**。多态的核心价值在于：调用者不需要知道对象的具体类型，只需要知道"这个对象能做什么"。这种"接口统一、行为各异"的能力，是面向对象设计中解耦的基石。

## override 关键字 (C++11)——编译器帮你盯着的"安全带"

C++11 引入了 `override` 关键字，它不改变任何运行时行为，但它是你写虚函数重写时**必须加上**的东西。原因很简单：它会强制编译器检查你是否真的正确重写了基类的虚函数。

来看一个不加 `override` 时的经典翻车场景：

```cpp
class Shape {
public:
    virtual void draw() const { printf("Shape::draw()\n"); }
};

class Circle : public Shape {
public:
    void draw() {   // 忘了 const！签名不匹配，不是重写
        printf("Circle::draw()\n");
    }
};
```

注意看 `Circle::draw()` 的签名——少了 `const`。这和基类的 `virtual void draw() const` 签名不同，编译器认为这是 `Circle` 自己新增的一个普通成员函数，跟 `Shape::draw()` 没有任何关系。通过基类指针调用 `draw()` 时走的是静态绑定，调用的还是 `Shape::draw()`。最可怕的是：这段代码**编译完全通过，没有任何警告**。笔者在这里血压拉满过不止一次。

加上 `override` 之后，同样的问题会直接被编译器揪出来：

```cpp
class Circle : public Shape {
public:
    void draw() override {   // 编译错误！签名不匹配
        printf("Circle::draw()\n");
    }
};
```

```text
error: 'void Circle::draw()' marked 'override', but does not override any base class virtual function
```

编译器明确告诉你：你声称在重写基类的虚函数，但签名对不上。`override` 能捕获的错误包括但不限于：基类中根本不存在这个名字的虚函数、函数签名不匹配（`const`、引用限定符等差异）、基类函数不是 `virtual` 的。所以铁律是——**只要你在重写虚函数，就一定写上 `override`**。

> **踩坑预警**：不加 `override` 不会报错，但签名一错就是灾难。养成习惯：每个虚函数重写都加 `override`，把它当作和系安全带一样的强制动作。

## vtable 揭秘——多态背后的跳板

理解了 `virtual` 的效果之后，我们来看编译器在幕后做了什么。每个包含虚函数的类，编译器都会为它生成一张**虚函数表**（virtual table，简称 vtable）——本质上是一个函数指针数组，每个条目对应一个虚函数，存储着**这个类**对该虚函数的实际实现地址。

拿我们的图形类层次来说，编译器大致生成了三张 vtable：

```text
Shape 的 vtable:     [ &Shape::draw ]
Circle 的 vtable:    [ &Circle::draw ]
Rectangle 的 vtable: [ &Rectangle::draw ]
```

而每个包含虚函数的对象，在内存布局中都会多出一个隐藏的成员——**虚表指针**（vptr），指向该对象所属类的 vtable。

当你写下 `shapes[i]->draw()` 时，编译器生成的代码大致做了这几步：先通过对象找到 `vptr`，定位到对应的 vtable，然后从表中取出 `draw()` 对应的函数指针，最后通过这个指针发起间接调用：

```text
shapes[1] (Shape*)  ----->  Circle 对象
                            [ vptr ] -------> Circle 的 vtable: [ &Circle::draw ]
```

这就是虚函数调用比普通函数调用多出来的全部开销——**一次额外的间接跳转**。在 PC 上，这个开销几乎可以忽略不计。但在资源紧张的嵌入式环境里需要认真对待：每个含虚函数的类多一张 vtable（占用 Flash），每个对象多一个 `vptr`（通常 4 或 8 字节，占用 RAM），每次虚函数调用多一次间接跳转（可能影响流水线和分支预测）。好在绝大多数场景下，这些开销和"解耦带来的架构收益"相比微不足道。

> **踩坑预警**：在 RAM 只有几 KB 的 MCU 上，每个对象多一个 `vptr` 可能是致命的。如果你的系统需要创建大量小对象（比如传感器采样数据点），请认真评估多态的内存开销。

## 虚析构函数——多态的最后一道防线

多态使用中有一个细节经常被忽略，但忽略它的后果是**未定义行为**：当你打算通过基类指针 `delete` 一个派生类对象时，基类的析构函数必须是 `virtual` 的。

先看反面教材：

```cpp
class BadBase {
public:
    ~BadBase() { printf("~BadBase()\n"); }   // 非虚析构
};

class BadDerived : public BadBase {
    int* data_;
public:
    BadDerived() : data_(new int[100]) {}
    ~BadDerived() { delete[] data_; printf("~BadDerived(): released\n"); }
};

BadBase* p = new BadDerived();
delete p;    // 只调用了 ~BadBase()，~BadDerived() 被跳过！
```

输出只有 `~BadBase()`，`~BadDerived()` 完全没被调用，`data_` 对应的 400 字节内存直接泄漏。原因和前面一样：`delete p` 时编译器看到 `p` 的静态类型是 `BadBase*`，而 `~BadBase()` 不是虚函数，静态绑定到了基类的析构函数，派生类的析构逻辑被彻底跳过。

解决方案非常简单——给基类析构函数加上 `virtual`：

```cpp
class GoodBase {
public:
    virtual ~GoodBase() = default;   // 虚析构函数
};
```

现在再执行同样的操作：

```cpp
GoodBase* p = new GoodDerived();
delete p;
// 输出：
// ~GoodDerived(): data_ released
// ~GoodBase()
```

析构顺序正确：先 `~GoodDerived()`，再 `~GoodBase()`，资源完整释放。这里用了 `= default`，因为基类析构函数本身没什么特殊的清理工作要做。关键在于那个 `virtual`——它让 `delete` 操作也能走动态绑定。

铁律：**只要类里有任何虚函数，析构函数就一定要声明为 `virtual`**。反过来，类里没有虚函数、不打算被继承——那析构函数非虚完全没问题。但一旦你开始了多态设计，这事儿就不能含糊。

> **踩坑预警**：非虚析构函数 + 通过基类指针 delete 派生类对象 = 未定义行为。在嵌入式中这通常表现为"莫名其妙的内存泄漏"或"外设状态异常"，而且定位起来异常困难。看到虚函数，立刻检查析构函数是不是也是虚的。

## 实战演练——多态图形系统

现在我们把前面学的东西串起来，写一个完整的多态图形系统。这个例子展示了虚函数在实际代码中是如何工作的。

```cpp
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
```

注意 `Shape` 的设计：`draw()` 和 `area()` 是纯虚函数（`= 0`），意味着 `Shape` 本身不能被实例化，任何想成为"合法图形"的类都必须提供自己的实现。析构函数声明为 `virtual ... = default`，既保证多态安全又不需要手写清理逻辑。`name_` 放在 `protected` 区域，让派生类在构造函数中设置。

然后在 `main()` 里创建一组不同的图形，用统一的接口来操作它们：

```cpp
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
```

运行结果：

```text
=== Drawing all shapes ===
  Drawing Circle (r=3.00)
  Drawing Rectangle (4.00 x 5.00)
  Drawing Triangle (base=6.00, height=2.00)
  Drawing Circle (r=1.50)

=== Areas ===
  Circle       : 28.2743
  Rectangle    : 20.0000
  Triangle     : 6.0000
  Circle       : 7.0686
  Total area: 61.3429
```

整个循环只依赖 `Shape` 的接口，完全不知道容器里装的是什么具体类型。将来想加一个 `Pentagon` 类，只需要继承 `Shape`、实现 `draw()` 和 `area()`，然后塞进容器——**主循环的代码一行都不用改**。这就是多态带来的扩展性。

## 练习

1. **多态文档打印**：设计一个文档类层次。基类 `Document` 有纯虚函数 `void print() const` 和虚析构函数。派生出 `TextDocument`（打印文本内容）、`ImageDocument`（打印图片描述信息）、`PdfDocument`（打印页数和作者）。在 `main()` 中创建不同类型的文档，存入 `vector<Document*>`，遍历调用 `print()`，验证每个类型都输出了自己的内容。

2. **验证虚析构函数**：在练习 1 的基础上，给每个派生类的析构函数加上 `printf` 输出。先正常清理（`delete` 每个指针），观察析构顺序。然后把基类析构函数的 `virtual` 去掉再跑一次，看看有什么变化——你会亲眼看到派生类的析构函数被跳过的过程。

## 小结

这一章我们围绕虚函数把运行时多态彻底拆了一遍。没有 `virtual` 时，基类指针只能静态绑定到基类的函数实现——这是很多初学者写了继承却发现"多态不生效"的根源。`virtual` 关键字让函数调用变成动态绑定，根据对象的实际类型决定调用哪个版本。`override` 是 C++11 给我们的安全带——永远在每个虚函数重写后面加上它，让编译器帮你检查签名是否真的匹配。虚析构函数是多态使用的安全底线，忘掉它意味着通过基类指针 `delete` 派生类对象时派生类的析构逻辑被跳过，后果是资源泄漏或未定义行为。

底层机制上，编译器通过 vtable 和 vptr 实现了这一切：每个类一张 vtable 存储函数指针，每个对象一个 vptr 指向所属类的 vtable，虚函数调用就是通过这个间接跳板完成的。开销不大，但在资源极端紧张的嵌入式场景中需要心中有数。

下一篇我们将进入抽象类和纯虚函数——把多态推向更严谨的设计层面，用"能力契约"来约束派生类必须提供哪些行为。
