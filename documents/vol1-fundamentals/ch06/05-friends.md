---
title: "友元"
description: "理解 friend 函数和 friend 类的用法，掌握友元的合理使用场景与滥用风险"
chapter: 6
order: 5
difficulty: beginner
reading_time_minutes: 10
platform: host
prerequisites:
  - "static 成员"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# 友元

前面几章我们一直在强调封装——`private` 成员藏在类内部，外部代码只能通过 `public` 接口操作对象。但偶尔你会碰到一种情况：某个外部函数或另一个类确实需要访问私有成员，而且这种访问是合理的、不可避免的。C++ 提供了专门的机制来处理这种场景——**`friend`（友元）**。

友元的本质是**定向授权**：类的作者主动声明"我信任这个函数/类，允许它看到我的私有成员"。它不是把封装彻底拆掉（那直接全写 `public` 就行了），而是开了一扇有控制的小门。接下来我们把友元的三种形态——友元函数、友元类、友元成员函数——逐一拆开来看，最后讨论什么时候该用友元，什么时候不该用。

## 友元函数——给外部函数一张通行证

友元函数是最基本的友元形态。声明方式是在类的内部用 `friend` 关键字加上一个普通函数的声明：

```cpp
class Vector3D {
private:
    float x, y, z;
public:
    Vector3D(float x, float y, float z) : x(x), y(y), z(z) {}
    // 声明 dot_product 为友元函数
    friend float dot_product(const Vector3D& a, const Vector3D& b);
};

// 友元函数定义——不是成员函数，不需要 Vector3D::
float dot_product(const Vector3D& a, const Vector3D& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
```

这里有几个要点需要搞清楚。首先，`friend` 声明出现在类的内部，但 `dot_product` **不是** `Vector3D` 的成员函数——它是一个普通的全局函数，只不过获得了访问 `Vector3D` 私有成员的特权。调用时和普通函数一样：`dot_product(v1, v2)`，而不是 `v1.dot_product(v2)`。

其次，`friend` 声明可以放在类的任何位置——`public`、`private`、`protected` 区域都无所谓，效果完全相同。通常我们把它集中放在类的开头或末尾，和成员函数声明分开，一眼就能看出"哪些外部函数拥有特殊权限"。

友元函数最经典的应用场景是重载 `operator<<`，让自定义类型能直接输出到流。这个场景之所以需要友元，是因为 `operator<<` 的左操作数是 `std::ostream&`，而不是你的类本身——它不可能成为你的成员函数：

```cpp
class Point {
private:
    int x, y;

public:
    Point(int x, int y) : x(x), y(y) {}

    // 友元重载 operator<<
    friend std::ostream& operator<<(std::ostream& os, const Point& p);
};

std::ostream& operator<<(std::ostream& os, const Point& p)
{
    os << "(" << p.x << ", " << p.y << ")";
    return os;
}

// 现在可以这样用了
Point p(3, 4);
std::cout << p << std::endl;  // 输出: (3, 4)
```

`operator<<` 重载的细节我们会在下一章展开，这里只需要理解它为什么必须是友元——第一个参数是 `std::ostream&`，不是 `Point`，所以这个函数没法写成 `Point` 的成员函数。

## 友元类——让整个类都成为信任对象

如果一个类的很多成员函数都需要访问另一个类的私有成员，逐个声明友元函数就太繁琐了。这时可以用 `friend class` 一次性授权给整个类：

```cpp
class Matrix {
private:
    float data[3][3];

public:
    Matrix()  // 初始化为单位矩阵
    {
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                data[i][j] = (i == j) ? 1.0f : 0.0f;
            }
        }
    }

    // Vector 是 Matrix 的友元类
    friend class Vector;
};

class Vector {
private:
    float x, y, z;

public:
    Vector(float x, float y, float z) : x(x), y(y), z(z) {}

    Vector transform(const Matrix& m)
    {
        // Vector 的成员函数可以直接访问 Matrix 的 private 成员
        float nx = m.data[0][0] * x + m.data[0][1] * y + m.data[0][2] * z;
        float ny = m.data[1][0] * x + m.data[1][1] * y + m.data[1][2] * z;
        float nz = m.data[2][0] * x + m.data[2][1] * y + m.data[2][2] * z;
        return Vector(nx, ny, nz);
    }
};
```

`friend class Vector;` 意味着 `Vector` 的**所有**成员函数都可以访问 `Matrix` 的私有成员。这是一种粗粒度的授权——要慎重使用，但确实有一些场景下两个类关系足够紧密，值得这种级别的信任。典型的合理场景包括"容器 + 迭代器"模式、以及上面这种数学类型之间的紧密协作。共同特征是：两个类在**逻辑上是一个整体**，只是出于代码组织的原因被拆成了两个类。

## 友元成员函数——精确制导的授权

如果你觉得"友元类授权太宽泛"，C++ 还提供了更精细的控制：只授权另一个类的**某一个**成员函数：

```cpp
class Vector;  // 前向声明

class Matrix {
private:
    float data[3][3];
public:
    Matrix();
    // 只授权 Vector::transform 这一个成员函数
    friend Vector Vector::transform(const Matrix& m);
};

class Vector {
private:
    float x, y, z;

public:
    Vector(float x, float y, float z) : x(x), y(y), z(z) {}

    Vector transform(const Matrix& m);
};
```

理论上这种方式最安全——最小权限原则嘛。但实际使用中，友元成员函数有一个让人头疼的依赖问题：声明 `friend Vector Vector::transform(const Matrix&)` 的时候，编译器必须已经看到 `Vector` 类的完整定义，否则它不知道 `transform` 确实是 `Vector` 的成员函数。这就要求我们仔细安排头文件包含顺序，弄不好就会陷入循环依赖。如果需要授权的成员函数有三四个之多，不如直接用友元类来得干脆。

## 什么时候该用友元——一张决策清单

友元很容易被滥用，我们有必要认真讨论一下使用边界。

**合理使用友元的场景**。运算符重载是最典型的——前面说的 `operator<<` 就是最好的例子。紧耦合的实现搭档也是合理的，比如 `Container` 和它的 `Iterator`、`Matrix` 和 `Vector`。这些情况下两个类本来就共享实现细节，用友元只是把这个事实在代码层面显式表达出来。

**不应该使用友元的场景**。如果只是想偷懒、不想设计合适的公共接口，随手加个 `friend` 让外部函数直接操作私有数据——这种友元就是有害的。大多数"需要友元"的场景其实可以通过提供恰当的访问接口来替代：

```cpp
// 不推荐：用友元绕过接口设计
class SensorData {
    friend void serialize(const SensorData& data, uint8_t* buffer);
private:
    float values[100];
    int count;
};

// 推荐：提供只读接口，封装完好
class SensorData {
private:
    float values[100];
    int count;
public:
    const float* data() const { return values; }
    int size() const { return count; }
};
```

> ⚠️ **踩坑预警：友元关系不继承、不传递**
> 友元关系有三个关键特性经常被误解。第一，**友元不继承**：如果 `Base` 是 `X` 的友元，`Derived`（继承自 `Base`）并不会自动成为 `X` 的友元。第二，**友元不传递**：如果 `A` 是 `B` 的友元，`B` 是 `C` 的友元，`A` 并不会自动成为 `C` 的友元。第三，**友元是单向的**：`A` 是 `B` 的友元，意味着 `A` 能访问 `B` 的私有成员，但 `B` 不能反过来访问 `A` 的私有成员——除非 `A` 也声明 `B` 为友元。这三条规则确保了友元权限不会像权限提升漏洞一样无限扩散。
>
> ⚠️ **踩坑预警：友元声明不是函数前向声明**
> 在类的内部写 `friend void foo();` 确实会使 `foo` 成为该类的友元，但当你把友元函数定义在类外面的时候，要确保在调用点之前能找到它的普通声明（而非 `friend` 声明）。否则在某些编译器上可能出现"找不到函数定义"的链接错误，尤其是当友元函数定义在另一个 `.cpp` 文件中的时候。最稳妥的做法是在类的外面再加一行普通的函数声明。

## 实战——friend_demo.cpp

现在我们来看一个完整的示例：`Matrix` 和 `Vector` 通过友元关系协作完成矩阵-向量乘法。

```cpp
// friend_demo.cpp
#include <array>
#include <cstdio>

class Vector;

class Matrix {
private:
    std::array<std::array<float, 3>, 3> data;
public:
    Matrix() : data{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}} {}
    void set(int row, int col, float value) { data[row][col] = value; }
    void print() const
    {
        for (int i = 0; i < 3; ++i)
            std::printf("| %.2f %.2f %.2f |\n",
                        data[i][0], data[i][1], data[i][2]);
    }
    // 授权 Vector 访问私有成员
    friend class Vector;
};

class Vector {
private:
    std::array<float, 3> v;
public:
    Vector(float x, float y, float z) : v{x, y, z} {}
    // 友元权限：直接访问 Matrix 内部数组
    Vector transform(const Matrix& m) const
    {
        float nx = m.data[0][0] * v[0] + m.data[0][1] * v[1] + m.data[0][2] * v[2];
        float ny = m.data[1][0] * v[0] + m.data[1][1] * v[1] + m.data[1][2] * v[2];
        float nz = m.data[2][0] * v[0] + m.data[2][1] * v[1] + m.data[2][2] * v[2];
        return Vector(nx, ny, nz);
    }
    void print() const
    { std::printf("(%.2f, %.2f, %.2f)\n", v[0], v[1], v[2]); }
};

int main()
{
    Matrix m;
    m.set(0, 0, 2.0f);
    m.set(1, 1, 3.0f);
    m.set(2, 2, 0.5f);
    Vector v(1.0f, 2.0f, 4.0f);
    Vector result = v.transform(m);
    std::printf("Matrix:\n");
    m.print();
    std::printf("Vector:  ");
    v.print();
    std::printf("Result:  ");
    result.print();
    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra -o friend_demo friend_demo.cpp
./friend_demo
```

预期输出：

```text
Matrix:
| 2.00 0.00 0.00 |
| 0.00 3.00 0.00 |
| 0.00 0.00 0.50 |
Vector:  (1.00, 2.00, 4.00)
Result:  (2.00, 6.00, 2.00)
```

这个例子里 `Vector::transform` 直接访问了 `Matrix::data` 这个私有数组。如果不用友元，就得提供一个 `float get(int, int) const` 的访问接口——不是不行，但在数学库这种对性能敏感的场景下，少一层间接访问就意味着更紧凑的循环和更友好的缓存行为。

## 练习

**练习 1：用友元实现 operator<<**

为下面的 `Student` 类实现一个友元函数 `operator<<`，使得 `std::cout << student;` 能够直接输出学生的信息。

```cpp
class Student {
private:
    int id;
    float score;

public:
    Student(int id, float score) : id(id), score(score) {}

    // 在这里添加友元声明
};

// 在这里实现 operator<<
```

验证方式：创建几个 `Student` 对象，用 `std::cout` 输出它们的信息，确认格式正确。

**练习 2：设计 Container-Iterator 友元对**

实现一个 `IntBuffer` 容器和一个 `IntBufferIterator` 迭代器。`IntBuffer` 内部用固定大小的 `int` 数组存储数据，`IntBufferIterator` 通过友元权限访问该数组完成遍历。要求外部代码无法直接访问 `IntBuffer` 的内部数组。提示：`IntBuffer` 声明 `friend class IntBufferIterator;`，迭代器持有指向容器的指针。

## 小结

友元是 C++ 封装体系中一个经过审慎设计的"逃生舱"——在不完全放弃 `private` 保护的前提下，为特定的外部函数或类授予访问权限。友元函数适合运算符重载（尤其是 `operator<<`），友元类适合紧耦合的实现搭档（容器与迭代器、数学类型协作），友元成员函数则在需要最小权限授权时发挥作用。

但友元也是一把双刃剑——每多一个友元声明，封装就多一道裂缝。笔者的建议是：在写下 `friend` 之前，先问自己"有没有不破坏封装的替代方案？"如果有，用替代方案；如果没有，而且场景确实需要直接访问内部数据，再放心地用友元。

下一章我们会把目光转向 `this` 指针和级联调用——深入理解 `this` 在对象模型中扮演的角色，以及如何利用它写出更优雅的链式接口。
