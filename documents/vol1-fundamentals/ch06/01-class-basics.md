---
title: "类的定义"
description: "从 struct 到 class：掌握 C++ 类的定义、成员变量与函数、访问控制的基本用法"
chapter: 6
order: 1
difficulty: beginner
reading_time_minutes: 15
platform: host
prerequisites:
  - "std::string"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# 类的定义

在前面的章节里，我们用 `std::string` 处理文本、用 `std::array` 管理固定大小的集合——这些类型用起来方便，但它们到底是怎么被"发明"出来的？答案是类。`std::string` 本身就是一个类，`std::array` 也是一个类，C++ 标准库里几乎所有的工具都是用类来构建的。可以说，类是 C++ 最核心的抽象机制：它把"数据"和"操作数据的函数"打包成一个整体，让我们能够像使用内置类型一样使用自定义类型。

这一章我们从 C 语言的 `struct` 出发，搞清楚 C++ 的 `class` 到底多了什么、为什么需要访问控制、成员函数怎么定义和使用，最后用一个完整的 `Point` 类把所有知识串起来。

> **学习目标**
>
> 完成本章后，你将能够：
>
> - [ ] 理解从 C struct 到 C++ class 的演进动机
> - [ ] 定义包含成员变量和成员函数的类
> - [ ] 使用 `public`、`private`、`protected` 控制成员的访问权限
> - [ ] 在类外定义成员函数，理解 `::` 作用域解析运算符
> - [ ] 区分 `class` 和 `struct` 的语义差异并合理选用

## 环境说明

- 平台：Linux x86\_64（WSL2 也可以）
- 编译器：GCC 13+ 或 Clang 17+
- 编译选项：`-Wall -Wextra -std=c++17`

## 第一步——从 struct 到 class

在 C 语言里，我们用 `struct` 把相关的数据字段归拢到一起。比如一个二维平面上的点：

```c
// C 风格：只有数据，没有行为
struct Point {
    double x;
    double y;
};
```

然后用独立的函数来操作这个结构体：

```c
double point_distance(struct Point a, struct Point b)
{
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return sqrt(dx * dx + dy * dy);
}

void point_print(struct Point p)
{
    printf("(%g, %g)", p.x, p.y);
}
```

这种写法能工作，但有一个根本性的问题：`point_distance`、`point_print` 这些函数和 `struct Point` 之间的关联完全靠命名约定来维持。没有任何语法层面的机制能阻止你写出 `point_distance(some_circle, some_triangle)` 这种荒谬的调用——只要参数类型碰巧匹配，编译器一声不吭就让你通过了。更要命的是，结构体的所有字段都是公开的，任何人都可以直接写 `p.x = -999999;`，把一个本该表示平面坐标的点搞成一个完全无意义的值——而没有任何代码能站出来说"等等，这个值不合理"。

C++ 的类同时解决了这两个问题。它把数据和操作数据的函数收拢到同一个语法单元里，而且允许你控制哪些成员对外可见、哪些是内部实现细节。在 C++ 中，`struct` 其实也能包含成员函数——`struct` 和 `class` 在语法上几乎完全等价，唯一的区别是默认的访问权限不同。我们先来看最基本的形式：

```cpp
// C++ 风格：数据 + 行为绑定在一起
class Point {
private:
    double x;
    double y;

public:
    void set(double new_x, double new_y)
    {
        x = new_x;
        y = new_y;
    }

    double distance_to(const Point& other) const
    {
        double dx = x - other.x;
        double dy = y - other.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    void print() const
    {
        std::cout << "(" << x << ", " << y << ")";
    }
};
```

现在 `distance_to` 和 `print` 作为 `Point` 的成员函数，天然就知道自己操作的是哪个点——不需要把结构体地址传来传去。而 `x` 和 `y` 被 `private` 保护起来，外部代码不能直接修改它们。

## 第二步——定义一个类

我们来逐项拆解类的定义语法。

### 成员变量和成员函数

类体内部可以包含两类东西：成员变量（也叫数据成员，描述对象的"状态"）和成员函数（也叫方法，描述对象能"做什么"）。注意类定义结束的大括号后面**必须加分号**——忘记写分号是新手最容易犯的错误之一，而且编译器给出的错误信息往往指向下一行，非常具有迷惑性。

> ⚠️ **踩坑预警**
> 类定义结束时的大括号后面**必须加分号**。忘记写分号是 C++ 新手最容易犯的错误之一，而且编译器给出的错误信息往往指向下一行，非常具有迷惑性。比如你写 `class Foo { ... }` 后面忘了分号，紧跟着写 `int main() { ... }`，编译器可能报 `error: expected ';' after class definition` 或者更离谱的 `error: 'main' does not name a type`——让你满世界找 `main` 的毛病，实际上问题出在上一行。

### 访问控制：public、private、protected

C++ 提供了三个访问控制关键字：`public`、`private` 和 `protected`。它们后面的所有成员都拥有对应的访问权限，直到遇到下一个访问控制关键字或类体结束。

`public` 成员对所有代码可见，构成类的外部接口。任何人都可以调用 `public` 的成员函数、读写 `public` 的成员变量。`private` 成员只有类自己的成员函数（以及友元）可以访问，外部代码碰都碰不到。`protected` 和 `private` 类似，但派生类也能访问——这个我们在后续讲继承的时候再展开，现在只需要知道有这个存在就行。

```cpp
class BankAccount {
private:
    std::string owner;
    double balance;

public:
    void deposit(double amount)
    {
        if (amount > 0) {
            balance += amount;
        }
    }

    bool withdraw(double amount)
    {
        if (amount > 0 && amount <= balance) {
            balance -= amount;
            return true;
        }
        return false;
    }

    double get_balance() const
    {
        return balance;
    }

    const std::string& get_owner() const
    {
        return owner;
    }
};
```

在这个 `BankAccount` 类里，`owner` 和 `balance` 是 `private` 的，外部代码无法直接读取或修改余额。唯一的途径是通过 `deposit`（存款）、`withdraw`（取款）和 `get_balance`（查询余额）这几个 `public` 接口。这样做的好处是，`deposit` 和 `withdraw` 内部可以加入校验逻辑——比如存款金额必须为正数、取款不能透支。如果 `balance` 是 `public` 的，谁都能写 `account.balance = -999999;`，那这些校验就形同虚设了。

这就是封装的核心价值：不是"防黑客"，而是在语法层面告诉使用者——这些内部细节你不该碰，你只应该通过我提供的接口来操作。对于类的作者来说，只要接口不变，内部实现怎么改都行，完全不影响使用者的代码。

> ⚠️ **踩坑预警**
> 从类外部访问 `private` 成员会导致编译错误，而且这个错误信息在不同编译器上差异很大。GCC 可能报 `error: 'double BankAccount::balance' is private within this context`，Clang 报 `error: 'balance' is a private member of 'BankAccount'`，MSVC 报 `error C2248: 'BankAccount::balance': cannot access private member declared in class 'BankAccount'`。如果你看到这类信息，先检查是不是试图从类外面碰了不该碰的成员。

## 第三步——成员函数的定义方式

成员函数有两种定义方式：在类体内部直接定义，或者在类体内声明、在类体外定义。

### 类体内定义

在类体内部直接写出函数的实现，这种写法最简洁，适合逻辑简单的一两行函数：

```cpp
class Point {
private:
    double x;
    double y;

public:
    double get_x() const { return x; }
    double get_y() const { return y; }
};
```

在类体内部定义的成员函数默认是 `inline` 的——编译器会尝试在调用处直接展开函数体，省去函数调用的开销。对于像 `get_x` 这样只返回一个成员变量的小函数来说，`inline` 的效果非常好。

### 类体外定义——作用域解析运算符

对于逻辑较长的函数，我们通常在类体内只写声明，把定义移到类体外面。这时候必须使用作用域解析运算符 `::` 来告诉编译器"这个函数属于哪个类"：

```cpp
// point.hpp
class Point {
private:
    double x;
    double y;

public:
    void set(double new_x, double new_y);
    double distance_to(const Point& other) const;
    void print() const;
};
```

```cpp
// point.cpp
#include <cmath>
#include <iostream>

#include "point.hpp"

void Point::set(double new_x, double new_y)
{
    x = new_x;
    y = new_y;
}

double Point::distance_to(const Point& other) const
{
    double dx = x - other.x;
    double dy = y - other.y;
    return std::sqrt(dx * dx + dy * dy);
}

void Point::print() const
{
    std::cout << "(" << x << ", " << y << ")";
}
```

`Point::set` 中的 `Point::` 就是作用域解析——"这个 `set` 函数不是全局函数，它是 `Point` 类的成员函数"。如果你忘了写 `Point::`，编译器会认为你在定义一个普通的全局函数，然后发现它不知道 `x` 和 `y` 是什么，直接报错。

> ⚠️ **踩坑预警**
> 在类体外定义成员函数时，`const` 限定符不能丢。如果你在类体内声明了 `void print() const;`，在类体外定义时也必须写 `void Point::print() const { ... }`。如果写成 `void Point::print() { ... }`（漏掉 `const`），编译器会认为这是两个不同的函数——一个有 `const` 的声明没有定义，一个没有 `const` 的定义没有声明——链接的时候就会报"undefined reference"错误。这个坑非常隐蔽，因为编译阶段不一定能发现，要等到链接时才炸。

## 第四步——class 和 struct 到底有什么区别

说了这么多 `class`，那 `struct` 呢？在 C++ 中，`struct` 和 `class` 在功能上几乎完全等价——`struct` 也可以有成员函数、构造函数、访问控制关键字、继承……唯一的区别是**默认的访问权限**：`class` 的成员默认是 `private` 的，`struct` 的成员默认是 `public` 的。

```cpp
class ClassStyle {
    int x;      // 默认 private
    void foo(); // 默认 private
};

struct StructStyle {
    int x;      // 默认 public
    void foo(); // 默认 public
};
```

你当然可以通过显式添加访问控制关键字来改变默认行为——一个 `struct` 加上 `private:` 和一个 `class` 加上 `public:` 在语义上完全等价，编译器生成的代码一模一样。

那什么时候用 `class`、什么时候用 `struct` 呢？C++ 社区有一个广泛认可的惯例：如果一个类型主要用来承载数据、所有成员都是公开的、没有复杂的 invariant 需要维护，就用 `struct`；如果一个类型有自己的 invariant（内部约束条件）、需要通过访问控制来保护数据完整性，就用 `class`。举个例子，一个表示 RGB 颜色的类型可以用 `struct`（`r`、`g`、`b` 三个分量没有任何约束），而一个 `BankAccount` 就应该用 `class`（余额不能为负、不能随意修改）。

## 第五步——实战演练：point.cpp

现在我们把前面学到的所有知识综合起来，写一个完整的 `Point` 类，包含坐标存取、距离计算、输出打印，以及一个简单的 getter/setter 模式。

```cpp
// point.cpp
#include <cmath>
#include <iostream>
#include <string>

/// @brief 二维平面上的点，演示类的基本定义与封装
class Point {
private:
    double x_;
    double y_;

public:
    /// @brief 设置坐标
    /// @param new_x 新的 x 坐标
    /// @param new_y 新的 y 坐标
    void set(double new_x, double new_y)
    {
        x_ = new_x;
        y_ = new_y;
    }

    /// @brief 获取 x 坐标
    /// @return x 坐标的值
    double get_x() const { return x_; }

    /// @brief 获取 y 坐标
    /// @return y 坐标的值
    double get_y() const { return y_; }

    /// @brief 计算到另一个点的欧几里得距离
    /// @param other 目标点
    /// @return 两点之间的距离
    double distance_to(const Point& other) const
    {
        double dx = x_ - other.x_;
        double dy = y_ - other.y_;
        return std::sqrt(dx * dx + dy * dy);
    }

    /// @brief 计算到原点的距离
    /// @return 到原点 (0, 0) 的距离
    double distance_to_origin() const
    {
        return std::sqrt(x_ * x_ + y_ * y_);
    }

    /// @brief 打印坐标到标准输出
    void print() const
    {
        std::cout << "Point(" << x_ << ", " << y_ << ")";
    }
};

int main()
{
    Point p1;
    p1.set(3.0, 4.0);

    Point p2;
    p2.set(6.0, 8.0);

    // 打印两个点
    std::cout << "p1 = ";
    p1.print();
    std::cout << "\n";

    std::cout << "p2 = ";
    p2.print();
    std::cout << "\n";

    // 计算距离
    std::cout << "distance(p1, p2) = " << p1.distance_to(p2) << "\n";
    std::cout << "distance(p1, origin) = " << p1.distance_to_origin() << "\n";

    // 尝试访问 private 成员——取消下面的注释会编译报错
    // p1.x_ = 100.0;  // error: 'double Point::x_' is private

    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra -o point point.cpp
./point
```

输出：

```text
p1 = Point(3, 4)
p2 = Point(6, 8)
distance(p1, p2) = 5
distance(p1, origin) = 5
```

我们来看一下这段代码的几个设计决策。成员变量 `x_` 和 `y_` 用了下划线后缀——这是一种常见的命名风格，用来区分成员变量和函数参数。`get_x` 和 `get_y` 是典型的 getter 函数，被声明为 `const` 因为读取坐标不需要修改对象。`distance_to` 接受一个 `const Point&` 参数——注意，虽然 `other` 是另一个对象，但 `Point` 的成员函数可以访问同类所有对象的 `private` 成员，所以 `other.x_` 在这里是合法的。测试数据选了 (3, 4) 和 (6, 8) 这两个勾股数，距离都是 5，方便一眼验证结果是否正确。

> ⚠️ **踩坑预警**
> `Point p1;` 能够编译通过，是因为编译器自动生成了一个默认构造函数——一个什么都不做的无参构造函数。这意味着 `x_` 和 `y_` 的初始值是未定义的。如果你在调用 `set` 之前就调用 `print`，会输出垃圾值。下一章我们会讲如何用构造函数确保对象创建时就处于合法状态。

## 练习

这两道练习覆盖了类的定义、访问控制和成员函数设计，建议自己动手写完再对照思路检查。

### 练习 1：Rectangle 类

设计一个 `Rectangle` 类，包含私有成员变量 `width_` 和 `height_`，以及公共成员函数 `set_size(double w, double h)`（设置宽高，参数非正时不修改）、`area()` 计算面积、`perimeter()` 计算周长、`print()` 输出矩形信息。

### 练习 2：Timer 类

设计一个 `Timer` 类模拟简单计时器。私有成员变量包括 `start_time_` 和 `running_`，公共成员函数包括 `start()`、`stop()` 和 `elapsed_seconds()`。提示：用 `<chrono>` 的 `std::chrono::steady_clock` 获取时间点。

## 小结

这一章我们从 C 语言 `struct` 的局限性出发，理解了 C++ 引入 `class` 的动机。核心要点：类通过 `public`、`private`、`protected` 管理成员可见性；成员函数可以在类体内定义（隐式 `inline`），也可以在类体外用 `::` 定义；`class` 和 `struct` 功能等价，区别仅在于默认访问权限——用 `struct` 表达"纯数据"，用 `class` 表达"有行为和约束的类型"。

不过我们故意留下了一个重要问题：对象创建时如何保证处于合法状态？上面的 `Point` 类需要先创建再调用 `set`，如果使用者忘了呢？下一章我们就来解决这个问题——构造函数和析构函数，它们是 RAII 的基石，也是 C++ 资源管理思想的起点。

---

> **难度自评**：如果你对 `private` 和 `public` 的访问边界还不太确定，试着在 `point.cpp` 的 `main` 函数里故意写几条访问私有成员的语句（比如 `p1.x_ = 100;`），看看编译器怎么报错。理解这些错误信息的含义，是掌握 C++ 类的第一步。
