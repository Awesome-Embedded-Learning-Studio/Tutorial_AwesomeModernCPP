---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: Making custom types behave like built-in types—the design philosophy
  of operator overloading, how to overload common operators, choosing between member
  and non-member overloads, and which operators to leave alone
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
title: C++98 Operator Overloading
translation:
  source: documents/vol1-fundamentals/03E-cpp98-operator-overloading.md
  source_hash: b009bef70dd4643d89549fd9dd33c08a9f01766b3f5f03a22ca1d2f990a8a20d
  translated_at: '2026-04-20T03:06:20.968838+00:00'
  engine: anthropic
  token_count: 1909
---
# C++98 Operator Overloading

> The complete repository is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to check it out, and if you like it, give it a Star to encourage the author.

Operator overloading is one of C++'s most controversial yet fascinating features. It allows **custom types to participate in expression calculations just like built-in types**, significantly improving code readability and expressiveness. Would you rather see two vectors stuffed into an awkwardly named `VectorAdd` method (a subtle jab at Java, by the way), or use the `a + b` approach for better readability? We trust you already know the answer.

However, operator overloading is a feature that requires restraint. We suggest a simple guideline: **only overload an operator if it feels "natural" to read the code that way.** Natural use cases include non-built-in vector math, physical quantity calculations, date and time handling, and container manipulation. If your overloaded operator leaves readers scratching their heads—for example, using `+` to mean "delete an element from a container"—you're better off writing a plain function named `remove`.

## 1. Arithmetic Operator Overloading

The most classic and justifiable scenario for operator overloading comes from **mathematical and physical models**. Take a three-dimensional vector, for instance. At its core, it's just a set of numbers participating in addition, subtraction, and multiplication. Without operator overloading, the code typically devolves into this:

```cpp
v3 = v1.add(v2);
v4 = v1.scale(2.0f);
```

With operator overloading, we can make our code **closely mirror the mathematical expression itself**:

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

The usage feels very natural:

```cpp
Vector3D v1(1, 2, 3);
Vector3D v2(4, 5, 6);

Vector3D v3 = v1 + v2;   // (5, 7, 9)
Vector3D v4 = v1 * 2;    // (2, 4, 6)

v1 += v2;                // v1 变为 (5, 7, 9)
```

Regarding the relationship between binary operators and compound assignment operators, there is an excellent implementation guideline: **implement the compound assignment (`+=`) first, then implement the binary operation (`+`) based on it.** This means the binary operator doesn't need to be a member function—it can be a non-member function implemented by calling `+=`. We'll discuss the benefits of this approach later in the "Member vs. Non-Member" section.

## 2. Subscript Operator `operator[]`

`operator[]` is the **"facade interface" of container classes**, and overloading it is practically standard practice for custom containers. Its core value lies in making custom types feel as accessible as arrays:

```cpp
buffer[3] = 0xFF;
auto x = buffer[10];
```

A key point: **you must provide both a `const` and a non-`const` version.** The non-`const` version returns a modifiable reference, allowing element modification via subscript; the `const` version returns a read-only reference, ensuring that `const` objects are not accidentally modified.

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

The existence of the `const` version is crucial—if you only have the non-`const` version, you won't be able to use `[]` to read data when holding a `ByteBuffer` through a `const` reference. We mentioned this pitfall in the previous chapter when discussing `const` member functions, and we'll emphasize it again here: **providing both `const` and non-`const` versions is standard practice for `operator[]`.**

## 3. Function Call Operator `operator()`

The function call operator `operator()` allows an object to be called like a function. Objects that implement this operator are known as **function objects (functors)**. Compared to regular functions, function objects have a unique advantage: **they can carry state**.

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

A typical application of function objects in embedded development is the **callback mechanism**—you can register a function object carrying context information as a callback, rather than being limited to raw function pointers. This became even more convenient with the introduction of lambdas in C++11 (lambdas are function objects under the hood), but even in C++98, hand-writing function objects was already a very useful pattern.

## 4. Increment and Decrement Operators `++`/`--`

Increment and decrement operators can be overloaded separately for the prefix (`++x`) and postfix (`x++`) versions. C++ distinguishes between the two using a convention: **the postfix version accepts an extra `int` parameter** (the compiler automatically passes 0), while the prefix version takes no extra parameters.

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

Note the difference in return types between prefix and postfix. The prefix `++` returns a reference (since the object has already been modified, returning the modified self makes sense), whereas the postfix `++` returns by value (since it needs to return a copy of the pre-modification state). This difference also explains why **prefix `++` is generally more efficient than postfix `++`**—the postfix version needs to construct an additional temporary object. For built-in types, this doesn't matter, but for complex iterator types, prefix `++` can save a copy.

Therefore, if you don't need the postfix semantics (which is true most of the time), building the habit of using prefix `++` is a good idea.

## 5. Type Conversion Operators

Type conversion operators allow an object to be explicitly or implicitly converted to another type, but this is **the most error-prone category of overloading**.

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

The problem with implicit type conversion is that **you cannot control when it happens**. The compiler will automatically invoke the conversion operator whenever it deems it "necessary," even if you had absolutely no intention of letting it do so. If your class has both a `operator float()` and a `operator int()`, confusing ambiguities can arise during overload resolution—the compiler will hesitate between two conversion paths.

Our advice is: **prefer explicit member functions (like `to_fahrenheit()`) over type conversion operators**, unless the semantics are extremely clear. If you must use a type conversion operator, C++11's `explicit operator T()` can restrict it to explicit conversions only, which is a much safer approach.

## 6. Member vs. Non-Member: A Guide to Choosing Overload Location

Operators can be overloaded in two ways: as **member functions** or **non-member functions** (typically friends). The choice affects not only syntax but also the behavior of type conversions.

For a **member function**, the left-hand operand must be an object of the current class (or something implicitly convertible to it). This means that if you implement `operator*` as a member function, `vec * 2` will work, but `2 * vec` will not—because `2` is a `int`, not a `Vector3D` object, and the compiler won't look for `operator*` on `int`.

For a **non-member function**, the left and right operands are symmetric. The compiler will attempt implicit conversions on both operands, so both `2 * vec` and `vec * 2` will work.

A widely accepted rule of thumb is:

- **Symmetric binary operators** (`+`, `-`, `*`, `/`, `==`, `!=`, etc.) should preferably be implemented as **non-member functions**
- **Assignment-like operators** (`=`, `+=`, `-=`, `[]`, `()`, `->`, etc.) must be implemented as **member functions** (the language mandates that certain operators can only be members)
- **Unary operators** (`-`, `!`, `~`, etc.) are typically implemented as **member functions**

For `Vector3D`, a better approach might be to implement `operator+` and `operator*` as non-member friend functions:

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

This way, both `2 * v` and `v * 2` will work correctly.

## 7. Which Operators Should Not Be Overloaded

Not all operators are suitable for overloading. Overloading some operators leads to confusing behavior and can even break fundamental language guarantees.

**Logical operators `&&` and `||`** are the quintessential anti-patterns. In C++, the built-in `&&` and `||` have a very important characteristic—**short-circuit evaluation**. For `a && b`, if `a` is `false`, `b` will not be evaluated. But once you overload `operator&&`, it becomes a regular function call—**both arguments are evaluated before the function is called**, and short-circuit evaluation is completely lost. This not only violates every C++ programmer's intuitive expectations of `&&` and `||`, but it can also produce completely different behavior if `b` has side effects.

The **comma operator `,`** has a similar issue. The built-in comma operator guarantees left-to-right evaluation order, but the overloaded version cannot provide this guarantee.

The **address-of operator `&`** should not be overloaded in the vast majority of cases—it returns the address of an object, which is one of the most fundamental operations in C++. Changing its semantics will break almost all code.

Our advice is: **only overload operators whose semantics are natural and don't violate intuitive expectations**. Specifically, arithmetic operators, comparison operators, the subscript operator, the function call operator, and stream operators—these are all safe to overload. As for logical operators, the comma operator, and the address-of operator—stay far away from them.

## Summary

Operator overloading allows custom types to participate in expression calculations just like built-in types, greatly enhancing code readability and expressiveness. We learned how to overload arithmetic operators, the subscript operator, the function call operator, increment and decrement operators, and type conversion operators, as well as the strategy for choosing between member and non-member overloads.

There is only one core principle of operator overloading: **make the code read naturally**. If your overloaded operator confuses the reader, it's a bad overload. Keep this guideline in mind, and you will make the right choice in most situations.

In the next article, we will learn about C++'s four type conversion operators, dynamic memory management mechanisms, and exception handling—these are more "advanced" features in C++98, and they form the foundation for understanding the direction of modern C++ improvements.
