---
title: Friend
description: Understand the usage of friend functions and friend classes, and master
  the appropriate use cases and risks of misusing friends.
chapter: 6
order: 5
difficulty: beginner
reading_time_minutes: 10
platform: host
prerequisites:
- static 成员
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
cpp_standard:
- 11
- 14
- 17
- 20
translation:
  source: documents/vol1-fundamentals/ch06/05-friends.md
  source_hash: 63e4a99f28c5242b20832ea4c0a370a998b8aec2d329c0c554a3c315cd35fbc7
  translated_at: '2026-05-26T10:51:59.543457+00:00'
  engine: anthropic
  token_count: 1934
---
# Friends

Hey, my friend! Today we are introducing `friend`! Don't get the wrong idea—`friend` is actually a C++ keyword, haha! In the previous chapters, we kept emphasizing encapsulation—`private` members are hidden inside the class, and external code can only manipulate objects through `public` interfaces. But occasionally, we run into a situation where an external function or another class genuinely needs to access private members, and this access is both reasonable and unavoidable. C++ provides a specific mechanism for this scenario—**`friend` (friends)**.

The essence of a friend is **targeted authorization**: the class author proactively declares, "I trust this function/class and allow it to see my private members." It doesn't tear down encapsulation entirely (we could just write everything as `public` if we wanted that), but rather opens a small, controlled door. Next, we will break down the three forms of friends—friend functions, friend classes, and friend member functions—one by one, and finally discuss when we should and shouldn't use friends.

## Friend Functions — Giving an External Function a Pass

The friend function is the most basic form of a friend. We declare it inside the class using the `friend` keyword followed by a regular function declaration:

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

There are a few key points to understand here. First, although the `friend` declaration appears inside the class, `dot_product` is **not** a member function of `Vector3D`—it is a regular global function that simply gains the privilege to access `Vector3D`'s private members. When calling it, we treat it like a normal function: `dot_product(v1, v2)`, rather than `v1.dot_product(v2)`.

Second, the `friend` declaration can be placed anywhere inside the class—whether in the `public`, `private`, or `protected` region makes no difference; the effect is exactly the same. We typically group them at the beginning or end of the class, separate from member function declarations, so we can see at a glance "which external functions have special permissions."

The most classic use case for friend functions is overloading `operator<<` to allow custom types to be output directly to a stream. The reason this requires a friend is that the left operand of `operator<<` is `std::ostream&`, not your class itself—so it cannot be a member function of your class:

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

We will dive into the details of `operator<<` overloading in the next chapter. For now, we just need to understand why it must be a friend—the first parameter is `std::ostream&`, not `Point`, so this function cannot be written as a member function of `Point`.

## Friend Classes — Making an Entire Class a Trusted Object

If many member functions of one class need to access the private members of another class, declaring friend functions one by one becomes too tedious. In this case, we can use `friend class` to authorize an entire class at once:

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

`friend class Vector;` means that **all** member functions of `Vector` can access the private members of `Matrix`. This is a coarse-grained authorization—we should use it cautiously, but there are indeed scenarios where two classes are tightly coupled enough to warrant this level of trust. Typical reasonable scenarios include the "container + iterator" pattern, and the close collaboration between mathematical types shown above. The common characteristic is that the two classes are **logically a single whole**, but are split into two classes for code organization reasons.

## Friend Member Functions — Precision-Guided Authorization

If we feel that "friend class authorization is too broad," C++ also provides finer-grained control: authorizing only **one specific** member function of another class:

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

Theoretically, this approach is the safest—following the principle of least privilege, after all. But in practice, friend member functions have a headache-inducing dependency issue: when declaring `friend Vector Vector::transform(const Matrix&)`, the compiler must have already seen the complete definition of the `Vector` class; otherwise, it doesn't know that `transform` is truly a member function of `Vector`. This requires us to carefully arrange the order of header file includes, and if we aren't careful, we can fall into circular dependencies. If we need to authorize three or four member functions, it's cleaner to just use a friend class.

## When to Use Friends — A Decision Checklist

Friends are easily abused, so it's necessary for us to seriously discuss the boundaries of their use.

**Scenarios where using friends is reasonable.** Operator overloading is the most typical example—the `operator<<` we discussed earlier is the best case. Tightly coupled implementation partners are also reasonable, such as `Container` and its `Iterator`, or `Matrix` and `Vector`. In these cases, the two classes share implementation details anyway, and using friends simply makes this fact explicit at the code level.

**Scenarios where friends should not be used.** If we just want to be lazy and avoid designing a proper public interface, casually adding a `friend` to let an external function directly manipulate private data—this kind of friend is harmful. Most "need a friend" scenarios can actually be replaced by providing appropriate access interfaces:

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

> ⚠️ **Pitfall Warning: Friend relationships are not inherited, not transitive**
> There are three key characteristics of friend relationships that are often misunderstood. First, **friends are not inherited**: if `Base` is a friend of `X`, `Derived` (which inherits from `Base`) does not automatically become a friend of `X`. Second, **friends are not transitive**: if `A` is a friend of `B`, and `B` is a friend of `C`, `A` does not automatically become a friend of `C`. Third, **friendship is unidirectional**: `A` being a friend of `B` means `A` can access `B`'s private members, but `B` cannot access `A`'s private members in return—unless `A` also declares `B` as a friend. These three rules ensure that friend permissions don't spread infinitely like a privilege escalation vulnerability.
>
> ⚠️ **Pitfall Warning: A friend declaration is not a forward declaration of a function**
> Writing `friend void foo();` inside a class does indeed make `foo` a friend of that class, but when we define the friend function outside the class, we must ensure that a regular declaration (not a `friend` declaration) can be found before the call site. Otherwise, we might encounter "undefined function" linker errors on certain compilers, especially when the friend function is defined in another `.cpp` file. The safest approach is to add a regular function declaration outside the class as well.

## Hands-On — friend_demo.cpp

Now let's look at a complete example: `Matrix` and `Vector` collaborate through a friend relationship to perform matrix-vector multiplication.

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

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o friend_demo friend_demo.cpp
./friend_demo
```

Expected output:

```text
Matrix:
| 2.00 0.00 0.00 |
| 0.00 3.00 0.00 |
| 0.00 0.00 0.50 |
Vector:  (1.00, 2.00, 4.00)
Result:  (2.00, 6.00, 2.00)
```

In this example, `Vector::transform` directly accesses the private array `Matrix::data`. If we didn't use a friend, we would have to provide a `float get(int, int) const` access interface—it's not impossible, but in performance-sensitive scenarios like a math library, one less layer of indirection means tighter loops and more cache-friendly behavior.

## Exercises

**Exercise 1: Implement operator<< with a friend**

Implement a friend function `operator<<` for the `Student` class below, so that `std::cout << student;` can directly output student information.

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

Verification: Create a few `Student` objects, use `std::cout` to output their information, and confirm the format is correct.

**Exercise 2: Design a Container-Iterator friend pair**

Implement a `IntBuffer` container and an `IntBufferIterator` iterator. `IntBuffer` internally uses a fixed-size `int` array to store data, and `IntBufferIterator` accesses this array through friend permissions to perform traversal. External code must not be able to directly access the internal array of `IntBuffer`. Hint: `IntBuffer` declares `friend class IntBufferIterator;`, and the iterator holds a pointer to the container.

## Summary

Friends are a carefully designed "escape hatch" in C++'s encapsulation system—granting access permissions to specific external functions or classes without completely abandoning `private` protection. Friend functions are suited for operator overloading (especially `operator<<`), friend classes are suited for tightly coupled implementation partners (containers and iterators, mathematical type collaborations), and friend member functions come into play when minimum-privilege authorization is needed.

But friends are also a double-edged sword—every additional friend declaration adds another crack in encapsulation. Our advice is: before writing `friend`, ask yourself, "Is there an alternative that doesn't break encapsulation?" If there is, use the alternative; if there isn't, and the scenario genuinely requires direct access to internal data, then go ahead and use a friend with confidence.

In the next chapter, we will turn our attention to `this` pointers and cascading calls—gaining a deeper understanding of the role `this` plays in the object model, and how to leverage it to write more elegant chained interfaces.
