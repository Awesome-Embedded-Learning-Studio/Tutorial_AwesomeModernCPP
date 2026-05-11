---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: 让自定义类型像内置类型一样工作——运算符重载的设计哲学、常用运算符的重载方法、成员与非成员重载的选择，以及哪些运算符不该碰
difficulty: beginner
order: 3
platform: host
prerequisites:
- C++98面向对象：类与对象深度剖析
reading_time_minutes: 13
related:
- C++98面向对象：继承与多态
- C++98进阶：类型转换、动态内存与异常处理
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: C++98运算符重载
---
# C++98运算符重载

> 完整的仓库地址在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) 中，您也可以光顾一下，喜欢的话给一个 Star 激励一下作者

运算符重载是 C++ 最具争议但也最有魅力的特性之一。它允许**自定义类型像内置类型一样参与表达式计算**，从而显著提升代码的可读性与表达力。你是喜欢看两个向量塞到一个叫做特别别扭的 `VectorAdd` 方法里（这里内涵下 Java（逃）），还是直接使用 `a + b` 的方式更可读呢？相信各位自有答案。

不过，运算符重载是一个需要克制的特性。笔者就建议一个准则：**当你"自然地"会用某个运算符来读这段代码时，才值得重载它。**比如说自然的处理非内置的向量数学运算、物理量运算、时间日期、容器处理等等。如果你的运算符重载让人看完之后一头雾水——比如用 `+` 来表示"从容器中删除元素"——那不如老老实实写一个名为 `remove` 的函数。

## 1. 算术运算符重载

最经典、也是最合理的运算符重载场景，来自**数学与物理模型**。比如三维向量，本质就是一组数值参与加减乘运算，如果不用运算符重载，代码通常会退化成这样：

```cpp
v3 = v1.add(v2);
v4 = v1.scale(2.0f);
```

而通过运算符重载，我们可以让代码**直接贴近数学表达式本身**：

```cpp
v3 = v1 + v2;
v4 = v1 * 2.0f;
```

我们来看一个完整的 `Vector3D` 实现：

```cpp
class Vector3D {
private:
    int x, y, z;

public:
    Vector3D(int x = 0, int y = 0, int z = 0)
        : x(x), y(y), z(z) {}

    // 二元加法：返回新对象，不修改原对象
    Vector3D operator+(const Vector3D& other) const {
        return Vector3D(x + other.x, y + other.y, z + other.z);
    }

    // 二元减法
    Vector3D operator-(const Vector3D& other) const {
        return Vector3D(x - other.x, y - other.y, z - other.z);
    }

    // 标量乘法（向量 * 标量）
    Vector3D operator*(int scalar) const {
        return Vector3D(x * scalar, y * scalar, z * scalar);
    }

    // 复合赋值：就地修改，避免不必要的临时对象
    Vector3D& operator+=(const Vector3D& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    // 一元负号：向量取反
    Vector3D operator-() const {
        return Vector3D(-x, -y, -z);
    }

    // 相等比较
    bool operator==(const Vector3D& other) const {
        return x == other.x && y == other.y && z == other.z;
    }

    bool operator!=(const Vector3D& other) const {
        return !(*this == other);
    }
};
```

使用效果非常自然：

```cpp
Vector3D v1(1, 2, 3);
Vector3D v2(4, 5, 6);

Vector3D v3 = v1 + v2;   // (5, 7, 9)
Vector3D v4 = v1 * 2;    // (2, 4, 6)

v1 += v2;                // v1 变为 (5, 7, 9)
```

关于二元运算符和复合赋值运算符的关系，有一个很好的实现准则：**先实现复合赋值（`+=`），然后基于它来实现二元运算（`+`）**。这样二元运算就不需要是成员函数了——它可以是一个非成员函数，通过调用 `+=` 来实现。这样做的好处我们稍后在"成员 vs 非成员"那一节再展开。

## 2. 下标运算符 `operator[]`

`operator[]` 是**容器类的"门面接口"**，重载它几乎是自定义容器的标配操作。它的核心价值在于让自定义类型看起来像数组一样可访问：

```cpp
buffer[3] = 0xFF;
auto x = buffer[10];
```

一个关键点是：**必须同时提供 `const` 和非 `const` 两个版本**。非 `const` 版本返回可修改的引用，允许通过下标修改元素；`const` 版本返回只读引用，保证 `const` 对象不会被意外修改。

```cpp
class ByteBuffer {
private:
    uint8_t data[256];
    size_t size;

public:
    ByteBuffer() : size(0) {}

    // 非 const 版本：可写
    uint8_t& operator[](size_t index) {
        return data[index];
    }

    // const 版本：只读
    const uint8_t& operator[](size_t index) const {
        return data[index];
    }

    size_t get_size() const { return size; }
};
```

使用效果：

```cpp
ByteBuffer buffer;
buffer[0] = 0xFF;              // 调用非 const 版本
uint8_t value = buffer[0];

const ByteBuffer& const_buffer = buffer;
uint8_t val = const_buffer[0]; // 调用 const 版本
// const_buffer[0] = 0xAA;     // 编译错误！const 版本返回 const 引用
```

`const` 版本的存在非常重要——如果只有非 `const` 版本，那通过 `const` 引用持有 `ByteBuffer` 时就无法使用 `[]` 来读取数据。这个坑我们在上一章讲 `const` 成员函数时已经提到过了，这里再次强调：**提供 `const` 和非 `const` 两个版本是 `operator[]` 的标配做法。**

## 3. 函数调用运算符 `operator()`

函数调用运算符 `operator()` 让对象可以像函数一样被调用。实现了这个运算符的对象被称为**函数对象 (functor)**。函数对象相比普通函数有一个独特的优势：**它可以携带状态**。

```cpp
class Accumulator {
private:
    int sum;

public:
    Accumulator() : sum(0) {}

    void operator()(int value) {
        sum += value;
    }

    int get_sum() const { return sum; }
    void reset() { sum = 0; }
};

// 使用
Accumulator acc;
acc(10);
acc(20);
acc(30);

int total = acc.get_sum();  // 60
```

函数对象在嵌入式开发中的一个典型应用是**回调机制**——你可以把一个携带了上下文信息的函数对象注册为回调，而不是只能用裸函数指针。这在 C++11 引入 lambda 之后变得更加方便（lambda 底层就是函数对象），但即使在 C++98 中，手写函数对象也已经是很有用的模式了。

## 4. 自增与自减运算符 `++`/`--`

自增和自减运算符可以分别重载前缀版本（`++x`）和后缀版本（`x++`）。C++ 通过一个惯例来区分两者：**后缀版本接受一个额外的 `int` 参数**（编译器自动传 0），而前缀版本没有额外参数。

```cpp
class Counter {
private:
    int value;

public:
    Counter(int v = 0) : value(v) {}

    // 前缀 ++：返回修改后的引用
    Counter& operator++() {
        ++value;
        return *this;
    }

    // 后缀 ++：返回修改前的副本
    Counter operator++(int) {
        Counter temp = *this;
        ++value;
        return temp;
    }

    int get() const { return value; }
};

Counter c(5);
Counter c1 = ++c;  // 前缀：c 变为 6，c1 是 6
Counter c2 = c++;  // 后缀：c 变为 7，c2 是 6（修改前的值）
```

注意前缀和后缀返回类型的差异。前缀 `++` 返回引用（因为对象已经被修改了，返回修改后的自身是合理的），而后缀 `++` 返回值（因为需要返回修改前的副本）。这个差异也解释了为什么**前缀 `++` 通常比后缀 `++` 更高效**——后缀版本需要额外构造一个临时对象。对于内置类型这无所谓，但对于复杂的迭代器类型，前缀 `++` 可能省去一次拷贝。

所以，如果你不需要后缀的语义（大多数时候不需要），养成用前缀 `++` 的习惯是个好主意。

## 5. 类型转换运算符

类型转换运算符允许对象被显式或隐式地转换为其他类型，但这是**最容易踩坑的一类重载**。

```cpp
class Temperature {
private:
    float celsius;

public:
    Temperature(float c) : celsius(c) {}

    // 转换为 float：摄氏度
    operator float() const {
        return celsius;
    }

    float to_fahrenheit() const {
        return celsius * 9.0f / 5.0f + 32.0f;
    }
};

Temperature temp(25.5f);
float c = temp;      // 隐式转换：25.5
float f = temp.to_fahrenheit();  // 显式接口：77.9
```

隐式类型转换的问题在于**你无法控制它何时发生**。编译器会在任何它认为"需要"的时候自动调用转换运算符，哪怕你完全没打算让它这么做。如果你的类同时有 `operator float()` 和 `operator int()`，那在重载解析时可能出现令人困惑的歧义——编译器会在两种转换路径之间犹豫不决。

笔者的建议是：**优先使用显式成员函数（如 `to_fahrenheit()`）而不是类型转换运算符**，除非语义极其明确。如果一定要用类型转换运算符，C++11 的 `explicit operator T()` 可以限制它只在显式转换时生效，这是更安全的做法。

## 6. 成员 vs 非成员：重载位置的选择指南

运算符可以通过两种方式重载：**成员函数**和**非成员函数**（通常是友元）。选择哪一种，不仅影响语法，还影响类型转换的行为。

**成员函数**的左侧操作数必须是当前类的对象（或者能隐式转换为当前类）。这意味着，如果你把 `operator*` 实现为成员函数，那么 `vec * 2` 可以工作，但 `2 * vec` 就不行——因为 `2` 是 `int`，它不是 `Vector3D` 对象，编译器不会在 `int` 上查找 `operator*`。

**非成员函数**的左右两个操作数是对称的。编译器会尝试对两个操作数都进行隐式转换，所以 `2 * vec` 和 `vec * 2` 都能工作。

一条被广泛接受的经验法则是：

- **对称的二元运算符**（`+`, `-`, `*`, `/`, `==`, `!=` 等）优先实现为**非成员函数**
- **赋值类的运算符**（`=`, `+=`, `-=`, `[]`, `()`, `->` 等）必须实现为**成员函数**（语言规定某些运算符只能是成员函数）
- **一元运算符**（`-`, `!`, `~` 等）通常实现为**成员函数**

对于 `Vector3D` 来说，更好的做法可能是把 `operator+` 和 `operator*` 实现为非成员友元函数：

```cpp
class Vector3D {
    // ... 成员变量和构造函数

    friend Vector3D operator+(const Vector3D& lhs, const Vector3D& rhs) {
        return Vector3D(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z);
    }

    friend Vector3D operator*(const Vector3D& v, int scalar) {
        return Vector3D(v.x * scalar, v.y * scalar, v.z * scalar);
    }

    friend Vector3D operator*(int scalar, const Vector3D& v) {
        return v * scalar;  // 复用上面的版本
    }
};
```

这样一来，`2 * v` 和 `v * 2` 都能正常工作。

## 7. 哪些运算符不应该重载

不是所有运算符都适合重载。有些运算符的重载会带来令人困惑的行为，甚至破坏语言的基本保证。

**逻辑运算符 `&&` 和 `||`** 是最典型的反面教材。在 C++ 中，内置的 `&&` 和 `||` 有一个非常重要的特性——**短路求值 (short-circuit evaluation)**。对于 `a && b`，如果 `a` 为 `false`，`b` 就不会被求值。但一旦你重载了 `operator&&`，它就变成了一个普通的函数调用——**两个参数都会在函数调用之前被求值**，短路求值的特性彻底丢失。这不仅违反了所有 C++ 程序员对 `&&` 和 `||` 的直觉预期，还可能在 `b` 有副作用时产生完全不同的行为。

**逗号运算符 `,`** 也有类似的问题。内置的逗号运算符保证从左到右的求值顺序，但重载版本无法提供这个保证。

**取地址运算符 `&`** 在绝大多数情况下不应该被重载——它返回对象的地址，这是 C++ 的基本操作之一，改变它的语义会让几乎所有代码都无法正常工作。

笔者的建议是：**只重载那些语义自然、不会违反直觉预期的运算符**。具体来说，算术运算符、比较运算符、下标运算符、函数调用运算符、流运算符——这些都可以放心重载。而逻辑运算符、逗号运算符、取地址运算符——离它们远点。

## 小结

运算符重载让自定义类型可以像内置类型一样参与表达式计算，极大地提升了代码的可读性和表达力。我们学习了算术运算符、下标运算符、函数调用运算符、自增自减运算符和类型转换运算符的重载方法，以及成员 vs 非成员重载的选择策略。

运算符重载的核心原则只有一条：**让代码读起来自然**。如果你重载的运算符让读者觉得困惑，那就是一个糟糕的重载。记住这个准则，就能在大多数情况下做出正确的选择。

在下一篇中，我们将学习 C++ 的四种类型转换运算符、动态内存管理机制，以及异常处理——这些是 C++98 中更"进阶"的特性，也是理解现代 C++ 改进方向的基础。
