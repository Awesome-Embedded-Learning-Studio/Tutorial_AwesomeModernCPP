---
title: Friend
description: Understand the usage of friend functions and friend classes, and master
  the appropriate use cases and risks of abusing friend.
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
---
# Friends

Hey! My friend! Today we introduce `friend`! Don't get the wrong idea—`friend` is actually a C++ keyword, haha! In previous chapters, we kept emphasizing encapsulation—`private` members are hidden inside the class, and external code can only manipulate objects through `public` interfaces. But occasionally you'll run into a situation where some external function or another class genuinely needs to access private members, and this access is reasonable and unavoidable. C++ provides a dedicated mechanism for this scenario—**`friend`**.

The essence of `friend` is **targeted authorization**: the class author proactively declares, "I trust this function/class, and I allow it to see my private members." It doesn't tear down encapsulation entirely (you could just make everything `public` if you wanted that), but rather opens a small, controlled door. Next, we'll break down the three forms of friends—friend functions, friend classes, and friend member functions—one by one, and finally discuss when to use friends and when not to.

## Friend Functions — Giving an External Function a Pass

The friend function is the most basic form of friendship. We declare it inside the class using the `friend` keyword followed by a regular function declaration:

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

There are a few key points to understand here. First, the `friend` declaration appears inside the class, but the function is **not** a member function of the class—it's a regular global function that simply gains the privilege to access the class's private members. We call it just like a regular function: `reset(a)`, not `a.reset()`.

Second, a `friend` declaration can be placed anywhere inside the class—`public`, `protected`, or `private` regions make no difference; the effect is exactly the same. We typically group them at the beginning or end of the class, separate from member function declarations, so we can see at a glance "which external functions have special permissions."

The most classic use case for friend functions is overloading `operator<<`, allowing custom types to be output directly to a stream. The reason this requires a friend is that the left operand of `operator<<` is `std::ostream&`, not your class itself—so it can't be a member function of your class:

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

We'll dive into the details of `operator<<` overloading in the next chapter. For now, just understand why it must be a friend—the first parameter is `std::ostream&`, not your class, so this function can't be written as a member function of your class.

## Friend Classes — Making an Entire Class a Trusted Entity

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

`friend class Iterator` means that **all** member functions of `Iterator` can access the private members of `Container`. This is a coarse-grained authorization—use it with caution—but there are indeed scenarios where two classes are closely related enough to warrant this level of trust. Typical reasonable scenarios include the "container + iterator" pattern, and the tight collaboration between mathematical types shown above. The common characteristic is: the two classes are **logically a single unit**, but are split into two classes for code organization reasons.

## Friend Member Functions — Precision-Guided Authorization

If you feel that "friend class authorization is too broad," C++ also provides finer-grained control: authorizing only **one specific** member function of another class:

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

Theoretically, this approach is the safest—the principle of least privilege, after all. But in practice, friend member functions have a headache-inducing dependency issue: when declaring the friend, the compiler must have already seen the complete definition of the other class; otherwise, it doesn't know that the function is indeed a member function of that class. This requires us to carefully arrange the order of header file includes, and if we're not careful, we can get stuck in circular dependencies. If three or four member functions need authorization, it's cleaner to just use a friend class.

## When to Use Friends — A Decision Checklist

Friends are easy to abuse, so it's worth seriously discussing the boundaries of use.

**Scenarios where using friends is appropriate.** Operator overloading is the most typical—the `operator<<` we discussed earlier is the best example. Tightly coupled implementation partners are also reasonable, such as `std::vector` and its `std::vector::iterator`, or `Matrix` and `Vector`. In these cases, the two classes share implementation details anyway, and using friends simply makes this fact explicit at the code level.

**Scenarios where you should not use friends.** If you just want to be lazy and avoid designing a proper public interface, casually adding a `friend` to let an external function directly manipulate private data—this kind of friendship is harmful. Most "I need a friend" scenarios can actually be replaced by providing appropriate access interfaces:

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

> ⚠️ **Pitfall warning: Friendship is not inherited and is not transitive**
> Friendship has three key characteristics that are often misunderstood. First, **friendship is not inherited**: if `Func` is a friend of `Base`, `Derived` (which inherits from `Base`) does not automatically become a friend of `Func`. Second, **friendship is not transitive**: if `A` is a friend of `B`, and `B` is a friend of `C`, `A` does not automatically become a friend of `C`. Third, **friendship is unidirectional**: `A` being a friend of `B` means `A` can access `B`'s private members, but `B` cannot access `A`'s private members in return—unless `A` also declares `B` as a friend. These three rules ensure that friend permissions don't spread infinitely like a privilege escalation vulnerability.
>
> ⚠️ **Pitfall warning: A friend declaration is not a forward declaration of a function**
> Writing `friend void func();` inside a class does make `func` a friend of that class, but when you define the friend function outside the class, make sure a regular declaration (not a `friend` declaration) is visible before the call site. Otherwise, on some compilers you may get "undefined function" link errors, especially when the friend function is defined in another `.cpp` file. The safest approach is to add a regular function declaration outside the class as well.

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

In this example, `operator*` directly accesses the `data_` private array. Without a friend, we'd need to provide a `data()` access interface—that's not impossible, but in performance-sensitive scenarios like a math library, one less layer of indirection means tighter loops and more cache-friendly behavior.

## Exercises

**Exercise 1: Implement operator<< with a friend**

Implement a friend function `operator<<` for the `Student` class below, so that `std::cout` can directly output student information.

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

Verification: create a few `Student` objects, output their information using `std::cout`, and confirm the format is correct.

**Exercise 2: Design a Container-Iterator friend pair**

Implement a `Container` container and an `Iterator` iterator. `Container` internally uses a fixed-size `int` array to store data, and `Iterator` accesses that array through friend permissions to perform traversal. External code must not be able to directly access `Container`'s internal array. Hint: `Container` declares `friend class Iterator`, and the iterator holds a pointer to the container.

## Summary

Friends are a carefully designed "escape hatch" in C++'s encapsulation system—granting access permissions to specific external functions or classes without completely abandoning `private` protection. Friend functions are suited for operator overloading (especially `operator<<`), friend classes are suited for tightly coupled implementation partners (containers and iterators, mathematical type collaboration), and friend member functions come into play when minimum-privilege authorization is needed.

But friends are a double-edged sword—every additional friend declaration is another crack in encapsulation. Our advice is: before writing `friend`, ask yourself, "Is there an alternative that doesn't break encapsulation?" If there is, use the alternative; if there isn't, and the scenario genuinely requires direct access to internal data, then use friends with confidence.

In the next chapter, we'll turn our attention to the `this` pointer and cascading calls—gaining a deeper understanding of the role `this` plays in the object model, and how to use it to write more elegant chained interfaces.
