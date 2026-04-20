---
title: "this 指针与链式调用"
description: "理解 this 指针的本质，掌握链式调用模式和 const 成员函数的正确用法"
chapter: 6
order: 6
difficulty: beginner
reading_time_minutes: 10
platform: host
prerequisites:
  - "友元"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# this 指针与链式调用

到目前为止，我们写的类都有一个默契——成员函数"知道"自己操作的是哪个对象。调用 `led.on()`，`on()` 就操作 `led`；调用 `other_led.on()`，`on()` 就操作 `other_led`。同一个函数，不同对象调用，行为各不相同。这件事看起来理所当然，但背后的机制值得深挖：编译器到底怎么让一个函数"知道"调用者是谁的？

答案就是 `this` 指针。每一个非静态成员函数，在底层都有一个隐藏的参数，指向调用该函数的那个对象。这一章我们来彻底搞清楚 `this` 是什么、它怎么工作、以及如何利用它写出优雅的链式调用代码。

## 每个成员函数都有一个隐藏参数

当我们写下这样的代码：

```cpp
class Point {
    int x_;
    int y_;
public:
    void set_x(int x) { x_ = x; }
};

Point p;
p.set_x(42);
```

编译器看到的并不是 `set_x(42)` 这么简单。它实际上把这段调用翻译成了类似这样的形式（伪代码，帮助理解）：

```cpp
// 伪代码：编译器的内部视角
Point::set_x(&p, 42);  // 把 p 的地址作为第一个参数传入
```

在 `set_x` 的函数体内部，这个隐藏的参数就是 `this`——一个指向当前对象的指针。所以 `x_ = x` 实际上等价于 `this->x_ = x`，只不过大多数时候编译器替我们省略了 `this->` 前缀。理解了这一点，很多看起来"神奇"的行为就变得合理了。同一个 `set_x` 函数，被 `p` 调用和被 `q` 调用，本质区别就是传入的 `this` 不同——一个指向 `p`，一个指向 `q`。

## this 的类型和显式使用

`this` 的类型是 `ClassName* const`——一个指向当前对象的常量指针。`const` 修饰的是指针本身而不是指向的对象，意味着你不能改变 `this` 的指向（比如 `this = &other_obj` 是非法的），但可以通过 `this` 修改对象的成员。

大多数情况下我们不需要显式写出 `this`，因为编译器会自动把成员名解析为 `this->成员名`。但在两个场景下，显式使用 `this` 是必要的或者有帮助的。

第一个场景是**参数名和成员变量名冲突**。说实话，这种写法在 C++ 中相当常见——很多工程师喜欢让构造函数的参数和成员变量同名，然后在初始化列表里靠位置区分。但如果是在函数体内赋值，就必须用 `this` 来消除歧义了：

```cpp
class Point {
    int x_;
    int y_;
public:
    // 初始化列表中，括号外的 x_ 是成员，括号内的 x_ 是参数
    Point(int x_, int y_) : x_(x_), y_(y_) {}

    void set_x(int x_) {
        this->x_ = x_;  // this->x_ 是成员，裸 x_ 是参数
    }
};
```

> **踩坑预警**：如果你在成员函数里写 `x_ = x_` 而没有加 `this->`，某些编译器可能不会给出警告——它会认为两个 `x_` 都指向参数本身，赋值变成了"自己赋值给自己"。更安全的做法是给成员变量加统一的后缀或前缀（比如 `x_` 或 `m_x`），从根本上避免命名冲突。

第二个场景是**返回 `*this`**——这正是链式调用的基础，我们接下来重点讲。

## const 成员函数与 this 的关系

在讲链式调用之前，我们必须先把 `const` 成员函数和 `this` 的关系理清楚，因为这是初学者特别容易踩坑的地方。当我们声明一个 `const` 成员函数时，编译器在内部把 `this` 的类型从 `Point* const` 变成了 `const Point* const`——不仅指针本身不可改，指向的对象也不可改。所以你在 `const` 成员函数里修改成员变量，编译器会直接报错。

这带来一个非常重要的后果：**`const` 对象只能调用 `const` 成员函数**。如果你把一个对象通过 `const` 引用传给函数，那你只能调用它标记了 `const` 的方法：

```cpp
void print_point(const Point& p)
{
    std::cout << p.get_x() << std::endl;  // OK，get_x() 是 const 的
    // p.set_x(10);  // 编译错误！set_x() 不是 const 的
}
```

> **踩坑预警**：忘记给 getter 加 `const` 是 C++ 新手最高频的错误之一。你写了一个 `int get_x() { return x_; }`，它"看起来只是读取数据"，但没有 `const` 修饰就意味着编译器认为它可能修改对象。结果是，任何通过 `const` 引用持有对象的人都没法调用这个 getter，报错信息通常是 "discards qualifiers" 之类的鬼话，新手看到完全摸不着头脑。笔者的建议是：写完每个成员函数后问自己一句"它需要修改对象吗？"如果答案是不需要，立刻加上 `const`。

## 链式调用——让接口流起来

链式调用（method chaining）的核心思路很简单：成员函数返回 `*this` 的引用，这样调用者可以在一条语句里连续调用多个方法。

我们先看一个不使用链式调用的 `Point` 类，感受一下痛点：

```cpp
class Point {
    int x_;
    int y_;
public:
    Point() : x_(0), y_(0) {}

    void set_x(int x) { x_ = x; }
    void set_y(int y) { y_ = y; }
};

// 每个 setter 都是独立的语句
Point p;
p.set_x(3);
p.set_y(4);
```

四行代码做了四件事，看起来也还行。但如果 setter 数量变多——比如一个 `Config` 类有十几个配置项——重复写对象名就成了纯粹的体力活。改成链式调用只需要一个改动：把返回类型从 `void` 改成 `ClassName&`，在函数末尾 `return *this;`：

```cpp
class Point {
    int x_;
    int y_;
public:
    Point() : x_(0), y_(0) {}

    Point& set_x(int x)
    {
        x_ = x;
        return *this;
    }

    Point& set_y(int y)
    {
        y_ = y;
        return *this;
    }

    Point& print()
    {
        std::cout << "(" << x_ << ", " << y_ << ")" << std::endl;
        return *this;
    }
};

// 现在一行搞定
Point p;
p.set_x(3).set_y(4).print();
```

原理我们拆开来看：`p.set_x(3)` 返回的是 `p` 的引用，所以紧接着的 `.set_y(4)` 等价于在 `p` 上调用 `set_y`；`set_y` 又返回 `p` 的引用，所以 `.print()` 还是在 `p` 上调用。整条链串在一起，每一步都操作同一个对象。

实际上，这种模式在实际工程里用得非常广泛。C++ 标准库中的 `std::cout` 就是最经典的例子——`operator<<` 返回 `std::ostream&`，所以我们可以写 `std::cout << "a" << "b" << "c";`。嵌入式开发中的硬件配置接口、日志系统也经常用链式调用来让代码更紧凑。

> **踩坑预警**：链式调用中，如果某个方法返回的是值而不是引用（比如不小心写了 `StringBuilder append(...)` 而不是 `StringBuilder& append(...)`），链式调用仍然能编译通过——但每一次链式调用操作的都会是一个新的副本，而不是原始对象。结果就是前面的调用全部白费，只有最后一个方法的结果被保留。这种 bug 非常隐蔽，因为代码"看起来"是对的，编译器也不报错，但运行结果就是不对。记住：链式调用必须返回**引用**。

## 动手实战：StringBuilder 和 Config Builder

现在我们把前面讲的东西综合起来，写一个完整的可编译文件。里面包含两个类——一个通过链式调用拼接字符串的 `StringBuilder`，一个用 Builder 模式构造配置的 `Config`。

```cpp
#include <cstdio>
#include <cstring>

class StringBuilder {
    char buffer_[256];
    std::size_t length_;

public:
    StringBuilder() : length_(0) { buffer_[0] = '\0'; }

    StringBuilder& append(const char* str)
    {
        while (*str && length_ < 255) {
            buffer_[length_++] = *str++;
        }
        buffer_[length_] = '\0';
        return *this;
    }

    StringBuilder& append_char(char c)
    {
        if (length_ < 255) {
            buffer_[length_++] = c;
            buffer_[length_] = '\0';
        }
        return *this;
    }

    // const 成员函数：只读取，不修改
    const char* c_str() const { return buffer_; }
    std::size_t length() const { return length_; }
};
```

`append` 和 `append_char` 都返回 `StringBuilder&`，所以可以链式调用。而 `c_str()` 和 `length()` 是只读操作，加上了 `const`，通过 `const` 引用也能调用。接下来是 `Config` 和它的 Builder——Builder 模式是链式调用最经典的应用之一，当我们需要构造一个配置对象、而配置项又很多的时候，它可以让代码既清晰又紧凑：

```cpp
class Config {
    char name_[64];
    int baudrate_;
    bool use_parity_;
    int timeout_ms_;

    // 私有构造，强制通过 Builder 创建
    Config(const char* name, int baud, bool parity, int timeout)
        : baudrate_(baud), use_parity_(parity), timeout_ms_(timeout)
    {
        std::strncpy(name_, name, 63);
        name_[63] = '\0';
    }

public:
    class Builder {
        char name_[64];
        int baudrate_;
        bool use_parity_;
        int timeout_ms_;

    public:
        Builder() : baudrate_(9600), use_parity_(false), timeout_ms_(1000)
        {
            name_[0] = '\0';
        }

        Builder& set_name(const char* name)
        {
            std::strncpy(name_, name, 63);
            name_[63] = '\0';
            return *this;
        }

        Builder& set_baudrate(int baud)
        {
            baudrate_ = baud;
            return *this;
        }

        Builder& set_parity(bool parity)
        {
            use_parity_ = parity;
            return *this;
        }

        Builder& set_timeout(int ms)
        {
            timeout_ms_ = ms;
            return *this;
        }

        Config build() const
        {
            return Config(name_, baudrate_, use_parity_, timeout_ms_);
        }
    };

    void print() const
    {
        std::printf("Config: name=%s, baud=%d, parity=%s, timeout=%dms\n",
                    name_, baudrate_,
                    use_parity_ ? "yes" : "no",
                    timeout_ms_);
    }
};
```

注意 `Config` 的构造函数是 `private` 的——外部代码不能直接创建 `Config` 对象，必须通过 `Config::Builder()` 一步步构建。每个 setter 都返回 `Builder&`，最后调用 `build()` 产出一个完整的 `Config`。我们来跑一下：

```cpp
int main()
{
    // StringBuilder 链式调用
    StringBuilder sb;
    sb.append("Hello")
          .append(", ")
          .append("this ")
          .append("is ")
          .append("a ")
          .append("chain!")
          .append_char('\n');

    std::printf("--- StringBuilder ---\n");
    std::printf("%s", sb.c_str());
    std::printf("Total length: %zu\n\n", sb.length());

    // Config Builder 链式调用
    Config cfg = Config::Builder()
                     .set_name("UART1")
                     .set_baudrate(115200)
                     .set_parity(false)
                     .set_timeout(500)
                     .build();

    std::printf("--- Config Builder ---\n");
    cfg.print();

    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra -o this_demo this_demo.cpp && ./this_demo
```

预期输出：

```text
--- StringBuilder ---
Hello, this is a chain!
Total length: 24

--- Config Builder ---
Config: name=UART1, baud=115200, parity=no, timeout=500ms
```

你可以自己动手编译跑一下，确认链式调用的每一环都确实在操作同一个对象。如果想进一步验证，可以在每个方法里加一行 `std::printf("this = %p\n", (void*)this);`，你会发现整条链中打印出来的地址完全一致——它们操作的就是同一个对象。

## `*this` 和 `this` 的区别

最后我们来澄清一个初学者经常混淆的问题。`this` 是指针，`*this` 是对当前对象的引用。如果你想让一个函数返回当前对象本身，写法是：

```cpp
// 返回对当前对象的引用
Point& set_x(int x)
{
    x_ = x;
    return *this;  // 解引用 this 指针，得到对象的引用
}
```

如果写成 `return this;`，返回类型就得是 `Point*`——调用者拿到的是指针，后续调用得用 `->` 而不是 `.`，链式调用的流畅感就全没了。虽然 `p.set_x(3)->set_y(4)->print()` 也能工作，但风格不统一，而且和标准库的惯例（`std::cout` 用 `.` 不用 `->`）对不上。所以标准的链式调用模式永远是 `return *this;` 配合返回类型 `ClassName&`。

## 练习

1. **实现一个链式 setter 的 `Rectangle` 类**。要求：提供 `set_width(int)` 和 `set_height(int)` 两个链式方法，再提供一个 `area() const` 返回面积。写一段测试代码验证 `rect.set_width(3).set_height(4).area()` 的结果是否为 12。

2. **实现一个简易 `QueryBuilder`**。要求：通过链式调用构建一个 SQL 查询字符串——`select("id, name").from("users").where("age > 18").build()` 应该返回 `"SELECT id, name FROM users WHERE age > 18"`。提示：内部用 `StringBuilder` 的思路维护一个字符缓冲区，每个链式方法往里面追加对应的 SQL 片段。

## 小结

这一章我们拆解了 `this` 指针的底层机制——每个非静态成员函数都有一个隐藏的 `this` 参数，指向调用该函数的对象。`const` 成员函数把 `this` 变成了指向常量的指针，从而在编译期禁止修改对象。链式调用的模式通过返回 `*this` 的引用让多个方法调用串联在一起，这种模式在 Builder 模式和运算符重载中都会大量使用。到这里，我们已经把 OOP 的基础知识全部过完了。下一章我们进入运算符重载——看看怎么让自定义类型像内置类型一样支持 `+`、`==`、`<<` 等运算。
