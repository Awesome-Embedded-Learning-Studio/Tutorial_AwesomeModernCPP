---
title: "构造函数"
description: "掌握默认构造、参数化构造、拷贝构造、初始化列表和委托构造的完整用法"
chapter: 6
order: 2
difficulty: beginner
reading_time_minutes: 15
platform: host
prerequisites:
  - "类的定义"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# 构造函数

上一章我们学了怎么定义一个类——写成员变量、写成员函数、用 `public` 和 `private` 控制访问权限。但有一个问题我们一直绕过去了：对象被创建出来的时候，它的成员变量里装的是什么？答案是——如果你什么都不做，局部对象的成员变量里装的是垃圾值，是上一次那块内存里残留的随机数据。

一个对象一旦被创建出来，它就应该处于一个**合法、可用、可预测**的状态。构造函数（constructor）就是 C++ 的解决方案：它在对象创建时自动执行，负责把成员变量带到正确的初始状态。只要构造函数写得对，"忘记初始化"这种低级错误就不可能发生。

这一章我们会把构造函数的几种形态全部拆开——默认构造、参数化构造、拷贝构造、成员初始化列表，还有 C++11 引入的委托构造。每一种都有它的使用场景和隐藏的坑。

## 默认构造——无参就能建对象

默认构造函数（default constructor）不需要任何参数。当你写 `Point p;` 时，调用的就是它。

```cpp
class Point {
private:
    double x_;
    double y_;

public:
    Point() : x_(0.0), y_(0.0) {}
};
```

`Point()` 后面的 `: x_(0.0), y_(0.0)` 是成员初始化列表，我们先混个眼熟，后面专门讲。关键是默认构造函数的职责：对象一出来就已经是一个合法的原点坐标。

如果你一个构造函数都不写，编译器会帮你生成一个默认构造函数。但它对 `int`、`double` 等基本类型不做任何初始化，值依然是垃圾。所以类里有基本类型成员时，几乎总是需要自己写默认构造函数。

> **踩坑预警**：编译器生成默认构造函数的规则只有一条——只要你手写了**任何一个**构造函数（哪怕是有参数的），编译器就不再帮你生成默认构造函数了。很多朋友写了一个 `Point(double x, double y)` 之后，发现 `Point p;` 编译不过，一头雾水。原因就在这里：你写了有参构造，编译器认为"你既然自己管初始化了，默认构造也得你自己写"。

解决方法很简单——要么自己补一个 `Point() : x_(0.0), y_(0.0) {}`，要么用 C++11 的 `= default` 语法让编译器继续帮你生成：

```cpp
class Point {
private:
    double x_;
    double y_;

public:
    Point() = default;                      // 让编译器生成默认构造
    Point(double x, double y) : x_(x), y_(y) {}
};
```

注意 `= default` 生成的默认构造函数对基本类型仍然不会初始化为零。如果你需要零初始化，还是得自己写 `: x_(0.0), y_(0.0) {}` 或者用类内初始值（下一章会讲到）。

## 参数化构造——把初始化的权力交给调用者

很多时候我们希望对象一创建就带上具体数据，而不是"零值"默认状态。参数化构造函数（parameterized constructor）接受参数来初始化成员变量。

```cpp
class Point {
private:
    double x_;
    double y_;

public:
    Point(double x, double y) : x_(x), y_(y) {}
};

Point origin(0.0, 0.0);
Point target(3.5, -2.1);
```

构造函数支持重载，所以你可以同时提供默认构造和参数化构造，让调用者按需选择。不过接下来我们得聊一个容易忽视的关键字——`explicit`。当一个构造函数只接受一个参数（或其余参数有默认值）时，它充当隐式类型转换函数。看代码：

```cpp
class PWMChannel {
private:
    int channel_;

public:
    PWMChannel(int ch) : channel_(ch) {}
};

void set_active(PWMChannel ch);

set_active(3);  // 编译通过！int 被隐式转换为 PWMChannel(3)
```

`set_active(3)` 这个调用，函数签名要的是 `PWMChannel`，你传了一个 `int`，编译器帮你调了构造函数做隐式转换。在简短示例里看起来没什么，但在大型项目里，这种隐式转换会制造难以定位的 bug——你可能只是参数类型写错了，编译器不但不报错，反而"好心办坏事"。

`explicit` 关键字就是用来禁止这种隐式转换的：

```cpp
class PWMChannel {
private:
    int channel_;

public:
    explicit PWMChannel(int ch) : channel_(ch) {}
};

void set_active(PWMChannel ch);

// set_active(3);               // 编译错误！不能隐式转换
set_active(PWMChannel(3));      // OK，显式构造
```

笔者的建议是：**所有单参数构造函数都应该加 `explicit`**，除非你有非常明确的理由需要隐式转换。这是一个几乎零成本的防御措施。

## 成员初始化列表——初始化的正规战场

我们前面一直在用成员初始化列表（member initializer list），现在来正式拆开讲。

构造函数的初始化列表写在参数列表后面的冒号之后，用逗号分隔，每个成员后面跟一个括号（或花括号）里的初始值：

```cpp
class Sensor {
private:
    int pin_;
    double threshold_;

public:
    Sensor(int pin, double threshold)
        : pin_(pin), threshold_(threshold) {}
};
```

你可能想问：我直接在构造函数体内赋值不就行了吗？为什么要搞一个专门的初始化列表？

```cpp
// 方式一：初始化列表（推荐）
Sensor(int pin, double threshold)
    : pin_(pin), threshold_(threshold) {}

// 方式二：构造函数体内赋值（能编译，但不推荐）
Sensor(int pin, double threshold) {
    pin_ = pin;           // 这不是"初始化"，而是"赋值"
    threshold_ = threshold;
}
```

对 `int` 和 `double` 这样的基本类型，两种方式运行结果完全一样。但问题出在 `const` 成员和引用成员上——这两种东西**只能**被初始化，不能被赋值。等到构造函数体开始执行时，所有成员已经被默认构造完毕了，再去赋值，对 `const` 和引用来说已经晚了——编译器直接报错。

```cpp
class Config {
private:
    const int kMaxRetry;
    int& counter_ref;

public:
    // 唯一合法的方式：初始化列表
    Config(int max, int& ref)
        : kMaxRetry(max), counter_ref(ref) {}

    // 下面这个版本编译直接炸：
    // Config(int max, int& ref) {
    //     kMaxRetry = max;      // 编译错误！const 不能赋值
    //     counter_ref = ref;    // 编译错误！引用必须在初始化时绑定
    // }
};
```

即便没有 `const` 和引用成员，初始化列表也依然更优。对类类型成员（如 `std::string`），在函数体内赋值意味着先默认构造再赋值覆盖——两步操作。而初始化列表直接用目标值构造，一步到位。

> **踩坑预警**：成员的初始化顺序由它们在类定义中的**声明顺序**决定，跟初始化列表里的书写顺序**无关**。这一点非常重要——如果你的初始化列表里写了 `: b(a), a(10)`，而类里 `a` 先声明、`b` 后声明，那实际执行顺序是先初始化 `a` 为 10，再初始化 `b` 为 `a`（此时 `a` 已经是 10），结果正确。但如果声明顺序反过来——`b` 在前、`a` 在后——那 `b(a)` 执行的时候 `a` 还没初始化，读到的就是垃圾值。大多数编译器会在两者顺序不一致时给出警告，但最好还是养成让声明顺序和初始化列表顺序保持一致的习惯，别给自己埋雷。

## 拷贝构造——用已有的对象创建新对象

拷贝构造函数（copy constructor）从一个已存在的同类型对象创建新对象，签名固定为 `ClassName(const ClassName& other)`：

```cpp
class Point {
private:
    double x_;
    double y_;

public:
    Point(double x, double y) : x_(x), y_(y) {}

    // 拷贝构造函数
    Point(const Point& other) : x_(other.x_), y_(other.y_) {}
};

Point a(1.0, 2.0);
Point b = a;   // 调用拷贝构造函数
Point c(a);    // 也是调用拷贝构造函数
```

拷贝构造函数在三种场景下会被调用：拷贝初始化（`Point b = a;`）、函数按值传参（形参通过拷贝构造创建）、函数按值返回（返回值通过拷贝构造复制，不过现代编译器通常用 RVO 省掉这次拷贝）。

如果你不自己写拷贝构造函数，编译器会生成一个默认版本——行为是**逐成员拷贝**（memberwise copy），即对每个成员分别调用其拷贝构造（对基本类型就是直接复制值）。对于 `Point` 这样只含基本类型的类，默认版本完全够用。

> **踩坑预警**：逐成员拷贝对包含**裸指针**的类来说是灾难性的。假设你的类里有一个 `int* data_` 指向动态分配的内存，默认拷贝构造只会复制指针的值（地址），而不是指针指向的内容。结果就是两个对象的 `data_` 指向同一块内存——其中一个析构释放了内存，另一个还在用，变成悬空指针（dangling pointer）。这就是经典的"浅拷贝"问题，我们在后面讲 RAII 和智能指针的时候会深入讨论怎么解决。

```cpp
class Buffer {
private:
    int* data_;
    std::size_t size_;

public:
    Buffer(std::size_t size) : size_(size), data_(new int[size]()) {}

    // 不写拷贝构造函数时，默认版本只复制指针地址
    // 两个对象析构时对同一块内存 delete 两次——boom
};
```

目前只需要记住一点：如果你的类管理了资源（动态内存、文件句柄、网络连接等），就必须自己写拷贝构造函数（或者干脆禁用它，后面会讲怎么禁）。

## 委托构造——让构造函数之间互相帮忙

C++11 引入了委托构造（delegating constructor），允许一个构造函数在初始化列表里调用**同一个类的另一个构造函数**，减少重复代码。

```cpp
class Point {
private:
    double x_;
    double y_;

public:
    // "主"构造函数：干所有的活
    Point(double x, double y) : x_(x), y_(y) {}

    // 默认构造函数：委托给上面的主构造函数
    Point() : Point(0.0, 0.0) {}
};
```

`Point()` 的初始化列表里写的不是成员名，而是 `Point(0.0, 0.0)`——调用另一个构造函数。执行顺序是：先执行目标构造函数的初始化列表和函数体，然后控制权回到委托构造函数的函数体。

这个特性在构造函数比较多、初始化逻辑有重叠时特别有用——把核心逻辑放在一个"主"构造函数里，其他构造函数委托给它就行。

不过委托构造有一条硬规则：**初始化列表里一旦出现了委托，就不能再初始化任何成员**。`Point() : Point(0.0, 0.0), x_(0) {}` 这种写法是非法的——要么全部委托，要么全部自己初始化，不能混着来。

## 实战演练——constructors.cpp

把这一章涉及的所有构造函数类型整合到一个 `Student` 类里，把每一个构造函数的调用都用输出标记出来：

```cpp
// constructors.cpp
// 构造函数综合演练：默认构造、参数化构造、拷贝构造、委托构造

#include <iostream>
#include <string>

class Student {
private:
    std::string name_;
    int age_;
    double score_;

public:
    Student() : name_("Unknown"), age_(0), score_(0.0)
    {
        std::cout << "[默认构造] " << name_ << ", "
                  << age_ << " 岁, " << score_ << " 分" << std::endl;
    }

    Student(const std::string& name, int age, double score)
        : name_(name), age_(age), score_(score)
    {
        std::cout << "[参数化构造] " << name_ << ", "
                  << age_ << " 岁, " << score_ << " 分" << std::endl;
    }

    // 委托构造：只用名字，其余委托给上面的参数化构造
    Student(const std::string& name) : Student(name, 18, 0.0)
    {
        std::cout << "[委托构造] 只指定姓名" << std::endl;
    }

    Student(const Student& other)
        : name_(other.name_), age_(other.age_), score_(other.score_)
    {
        std::cout << "[拷贝构造] 复制: " << name_ << std::endl;
    }

    void print() const
    {
        std::cout << "  " << name_ << ", " << age_
                  << " 岁, " << score_ << " 分" << std::endl;
    }
};

/// @brief 按值传递，触发拷贝构造
void enroll(Student s)
{
    std::cout << "  注册: ";
    s.print();
}

int main()
{
    std::cout << "=== 默认构造 ===" << std::endl;
    Student s1;
    s1.print();

    std::cout << "\n=== 参数化构造 ===" << std::endl;
    Student s2("Alice", 20, 92.5);
    s2.print();

    std::cout << "\n=== 委托构造 ===" << std::endl;
    Student s3("Bob");
    s3.print();

    std::cout << "\n=== 拷贝构造（拷贝初始化）===" << std::endl;
    Student s4 = s2;
    s4.print();

    std::cout << "\n=== 拷贝构造（按值传参）===" << std::endl;
    enroll(s2);

    return 0;
}
```

编译运行：`g++ -std=c++17 -Wall -Wextra -o constructors constructors.cpp && ./constructors`

预期输出：

```text
=== 默认构造 ===
[默认构造] Unknown, 0 岁, 0 分
  Unknown, 0 岁, 0 分

=== 参数化构造 ===
[参数化构造] Alice, 20 岁, 92.5 分
  Alice, 20 岁, 92.5 分

=== 委托构造 ===
[参数化构造] Bob, 18 岁, 0 分
[委托构造] 只指定姓名
  Bob, 18 岁, 0 分

=== 拷贝构造（拷贝初始化）===
[拷贝构造] 复制: Alice
  Alice, 20 岁, 92.5 分

=== 拷贝构造（按值传参）===
[拷贝构造] 复制: Alice
  注册:   Alice, 20 岁, 92.5 分
```

验证一下：委托构造 `Student("Bob")` 先调用 `Student("Bob", 18, 0.0)`（先输出"参数化构造"），再执行自己的函数体（输出"委托构造"）。拷贝构造在两种场景下都被正确触发。

## 动手试试

### 练习一：实现一个 Date 类

写一个 `Date` 类，包含 `year_`、`month_`、`day_` 三个成员。要求提供默认构造函数（初始化为 2000/1/1）、参数化构造函数（接受年月日，做基本合法性检查——月份 1-12、日期 1-31），以及一个 `print()` 方法。验证方法：构造几个日期对象，包含一个不合法的日期（比如月份 13），观察校验逻辑是否生效。

### 练习二：实现一个 Vector3D 类

写一个 `Vector3D` 类，包含 `x_`、`y_`、`z_` 三个 `double` 成员。用委托构造让默认构造函数委托给 `Vector3D(0.0, 0.0, 0.0)`，再实现拷贝构造函数和一个 `length()` 方法返回向量的模。验证方法：创建默认向量、自定义向量、拷贝向量，打印值和模。

## 小结

构造函数是对象生命周期的起点，保证对象一出生就处于合法状态。默认构造函数用于无参创建，但注意——一旦你写了任何构造函数，默认构造就不再自动生成。参数化构造用具体数据初始化对象，`explicit` 防止单参数构造函数的隐式转换。成员初始化列表是初始化的正规途径，对 `const` 和引用成员来说是唯一选择，初始化顺序遵循声明顺序而非书写顺序。拷贝构造用已有对象创建新对象，默认做逐成员拷贝——对含指针的类来说是隐藏的炸弹。C++11 的委托构造让构造函数之间互相复用，减少重复代码。

下一章我们来讲析构函数——构造函数把对象带进来，析构函数负责把对象安全送走。两者合在一起，就是 C++ 资源管理的核心理念 RAII。
