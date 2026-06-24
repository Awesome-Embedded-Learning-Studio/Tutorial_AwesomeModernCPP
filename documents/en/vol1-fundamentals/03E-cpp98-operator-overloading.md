---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: Make custom types behave like built-in ones—the design philosophy of
  operator overloading, how to overload common operators, choosing between member
  and non-member functions, and which operators to avoid.
difficulty: beginner
order: 3
platform: host
prerequisites:
- C++98面向对象：类与对象深度剖析
reading_time_minutes: 10
related:
- C++98面向对象：继承与多态
- C++98进阶：类型转换、动态内存与异常处理
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: C++98 Operator Overloading
translation:
  source: documents/vol1-fundamentals/03E-cpp98-operator-overloading.md
  source_hash: c571c20b7995836c38182d758571d98df22a1522fee518d825e9f90e177ab330
  translated_at: '2026-06-24T00:28:36.561914+00:00'
  engine: anthropic
  token_count: 1909
---
# C++98 Operator Overloading

> The complete repository is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to check it out and give it a Star if you like it to motivate the author.

Operator overloading is one of C++'s most controversial yet captivating features. It allows **custom types to participate in expression calculations just like built-in types**, thereby significantly enhancing code readability and expressiveness. Would you prefer to see two vectors stuffed into an awkwardly named `VectorAdd` method (a subtle dig at Java—just kidding), or is the direct `a + b` approach more readable? I believe you have your own answer.

However, operator overloading is a feature that requires restraint. I suggest the following guideline: **only overload an operator if you would "naturally" use it to read the code**. For example, it is natural to handle non-built-in vector math, physical quantity calculations, time and date, or container manipulation. If your operator overload leaves readers scratching their heads—for instance, using `+` to mean "remove an element from a container"—it is better to stick to writing a function named `remove`.

## 1. Arithmetic Operator Overloading

The most classic and reasonable scenario for operator overloading comes from **mathematical and physical models**. For example, a three-dimensional vector is essentially a set of values involved in addition, subtraction, and multiplication. Without operator overloading, the code typically degenerates into this:

```cpp
v3 = v1.add(v2);
v4 = v1.scale(2.0f);
```

By using operator overloading, we can make the code **closely resemble the mathematical expressions themselves**:

```cpp
v3 = v1 + v2;
v4 = v1 * 2.0f;
```

Let's look at a complete `Vector3D` implementation:

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

The user experience is very natural:

```cpp
Vector3D v1(1, 2, 3);
Vector3D v2(4, 5, 6);

Vector3D v3 = v1 + v2;   // (5, 7, 9)
Vector3D v4 = v1 * 2;    // (2, 4, 6)

v1 += v2;                // v1 变为 (5, 7, 9)
```

Here is a good implementation guideline regarding the relationship between binary operators and compound assignment operators: **implement the compound assignment (`+=`) first, and then implement the binary operation (`+`) based on it.** This way, the binary operator does not need to be a member function—it can be a non-member function implemented by calling `+=`. We will discuss the benefits of this approach later in the "Member vs. Non-Member" section.

## 2. Subscript Operator `operator[]`

The `operator[]` is the **"face" of a container class**, and overloading it is a standard operation for custom containers. Its core value lies in making custom types accessible just like arrays:

```cpp
buffer[3] = 0xFF;
auto x = buffer[10];
```

One key point is that **we must provide both `const` and non-`const` versions**. The non-`const` version returns a modifiable reference, allowing element modification via subscript; the `const` version returns a read-only reference, ensuring that `const` objects are not accidentally modified.

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

Usage:

```cpp
ByteBuffer buffer;
buffer[0] = 0xFF;              // 调用非 const 版本
uint8_t value = buffer[0];

const ByteBuffer& const_buffer = buffer;
uint8_t val = const_buffer[0]; // 调用 const 版本
// const_buffer[0] = 0xAA;     // 编译错误！const 版本返回 const 引用
```

The existence of the `const` version is critical—if only the non-`const` version existed, we would be unable to use `[]` to read data when holding a `ByteBuffer` via a `const` reference. We mentioned this pitfall in the previous chapter when discussing `const` member functions, but it is worth reiterating: **providing both `const` and non-`const` versions is standard practice for `operator[]`.**

## 3. Function Call Operator `operator()`

The function call operator `operator()` allows an object to be invoked like a function. Objects that implement this operator are known as **function objects (functors)**. Compared to ordinary functions, function objects have a unique advantage: **they can maintain state**.

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

A typical application of function objects in embedded development is the **callback mechanism**. You can register a function object carrying context information as a callback, rather than being limited to raw function pointers. This became even more convenient with the introduction of lambdas in C++11 (since lambdas are function objects under the hood), but even in C++98, manually writing function objects was a very useful pattern.

## 4. Increment and Decrement Operators `++`/`--`

Increment and decrement operators can be overloaded for both the prefix version (`++x`) and the postfix version (`x++`). C++ distinguishes between the two by a convention: **the postfix version accepts an extra `int` parameter** (which the compiler automatically passes as 0), while the prefix version takes no extra arguments.

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

Note the difference in return types between the prefix and postfix versions. The prefix `++` returns a reference (since the object has already been modified, returning the modified object itself is logical), while the postfix `++` returns a value (because it needs to return a copy of the state before modification). This difference also explains why **prefix `++` is generally more efficient than postfix `++`**—the postfix version requires the construction of a temporary object. This doesn't matter for built-in types, but for complex iterator types, the prefix `++` can save a copy operation.

Therefore, unless you specifically need the postfix semantics (which is rarely the case), it is a good habit to use the prefix `++`.

## 5. Type Conversion Operators

Type conversion operators allow objects to be explicitly or implicitly converted to other types, but this is **the type of overload most prone to pitfalls**.

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

The problem with implicit type conversion is that **you cannot control when it happens**. The compiler will automatically invoke conversion operators whenever it deems it "necessary," even if you had no intention of doing so. If your class defines both `operator float()` and `operator int()`, confusing ambiguities can arise during overload resolution—the compiler will hesitate between the two conversion paths.

Our recommendation is to **prefer explicit member functions (like `to_fahrenheit()`) over type conversion operators**, unless the semantics are crystal clear. If you must use a type conversion operator, C++11's `explicit operator T()` restricts it to explicit conversions only, which is a much safer approach.

## 6. Member vs. Non-member: A Guide to Choosing Overload Location

Operators can be overloaded in two ways: as **member functions** or **non-member functions** (typically friends). The choice affects not only syntax but also type conversion behavior.

For **member functions**, the left-hand operand must be an object of the current class (or implicitly convertible to it). This means that if you implement `operator*` as a member function, `vec * 2` will work, but `2 * vec` will not—because `2` is an `int`, not a `Vector3D` object, and the compiler will not look for `operator*` inside `int`.

For **non-member functions**, the left and right operands are symmetric. The compiler will attempt implicit conversions on both operands, so both `2 * vec` and `vec * 2` will work.

A widely accepted rule of thumb is:

- **Symmetric binary operators** (`+`, `-`, `*`, `/`, `==`, `!=`, etc.) should preferably be implemented as **non-member functions**.
- **Assignment-like operators** (`=`, `+=`, `-=`, `[]`, `()`, `->`, etc.) must be implemented as **member functions** (the language mandates that certain operators can only be members).
- **Unary operators** (`-`, `!`, `~`, etc.) are typically implemented as **member functions**.

For `Vector3D`, a better approach would be to implement `operator+` and `operator*` as non-member friend functions:

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

This way, both `2 * v` and `v * 2` work correctly.

## 7. Which Operators Should Not Be Overloaded

Not all operators are suitable for overloading. Overloading some operators can lead to confusing behavior or even break fundamental guarantees of the language.

The **logical operators `&&` and `||`** are the most typical counter-examples. In C++, the built-in `&&` and `||` have a very important feature—**short-circuit evaluation**. For `a && b`, if `a` is `false`, `b` will not be evaluated. However, once you overload `operator&&`, it becomes a normal function call—**both parameters are evaluated before the function is called**, and the feature of short-circuit evaluation is completely lost. This not only violates the intuitive expectations of all C++ programmers regarding `&&` and `||`, but can also produce completely different behavior if `b` has side effects.

The **comma operator `,`** has a similar problem. The built-in comma operator guarantees a left-to-right evaluation order, but the overloaded version cannot provide this guarantee.

The **address-of operator `&`** should, in the vast majority of cases, not be overloaded—it returns the address of an object, which is one of the fundamental operations of C++. Changing its semantics will cause almost all code to fail to work correctly.

My advice is: **only overload operators that have natural semantics and do not violate intuitive expectations**. Specifically, arithmetic operators, comparison operators, the subscript operator, the function call operator, and stream operators—these can all be safely overloaded. As for logical operators, the comma operator, and the address-of operator—stay away from them.

## Summary

Operator overloading allows custom types to participate in expression calculations just like built-in types, greatly enhancing code readability and expressiveness. We learned how to overload arithmetic operators, the subscript operator, the function call operator, increment and decrement operators, and type conversion operators, as well as the selection strategy between member and non-member overloading.

There is only one core principle of operator overloading: **make the code read naturally**. If the operator you overload confuses the reader, it is a bad overload. Keeping this guideline in mind will help us make the right choices in most situations.

In the next article, we will learn about C++'s four type conversion operators, dynamic memory management mechanisms, and exception handling—these are more "advanced" features in C++98 and are also the foundation for understanding the direction of modern C++ improvements.
