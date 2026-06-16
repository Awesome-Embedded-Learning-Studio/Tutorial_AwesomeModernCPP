---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: Make custom types behave like built-in types—the design philosophy of
  operator overloading, how to overload common operators, choosing between member
  and non-member overloads, and which operators to avoid.
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
  source_hash: fe924011f46470a99613f3713fd167203b2acb4c60fa3e55070742b4c26e459a
  translated_at: '2026-06-16T03:31:30.183971+00:00'
  engine: anthropic
  token_count: 1909
---
# C++98 Operator Overloading

> The complete repository is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to visit, and if you like it, give the author a Star to show your support.

Operator overloading is one of C++'s most controversial yet captivating features. It allows **custom types to participate in expression calculations just like built-in types**, thereby significantly enhancing code readability and expressiveness. Would you rather see two vectors stuffed into a method named something awkward like `addVectors()` (a gentle jab at Java here), or would you prefer the readability of `v1 + v2`? I trust you have your own answer.

However, operator overloading is a feature that requires restraint. I suggest a guideline: **Only overload an operator when it is "natural" to read the code using that operator.** This applies naturally to non-built-in vector math, physical quantity calculations, time and date handling, container manipulation, and so on. If your operator overload leaves readers scratching their heads—for example, using `operator-` to mean "delete element from container"—it is better to honestly write a function named `remove()`.

## 1. Arithmetic Operator Overloading

The most classic and reasonable scenario for operator overloading comes from **mathematical and physical models**. Take a 3D vector, for instance; it is essentially a set of values participating in addition, subtraction, and multiplication. Without operator overloading, code usually degenerates into this:

```cpp
// Without operator overloading
Vector3 result = vec1.add(vec2);
Vector3 scaled = vec3.multiply(2.5f);
```

By using operator overloading, we can make the code **directly mirror the mathematical expression itself**:

```cpp
// With operator overloading
Vector3 result = vec1 + vec2;
Vector3 scaled = vec3 * 2.5f;
```

Let's look at a complete `Vector3` implementation:

```cpp
class Vector3 {
public:
    float x, y, z;

    Vector3(float x = 0, float y = 0, float z = 0) : x(x), y(y), z(z) {}

    // Compound assignment (+=)
    Vector3& operator+=(const Vector3& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    // Binary addition (+) implemented as a non-member friend
    friend Vector3 operator+(Vector3 lhs, const Vector3& rhs) {
        lhs += rhs;
        return lhs;
    }
};
```

The usage feels very natural:

```cpp
Vector3 v1(1, 2, 3);
Vector3 v2(4, 5, 6);
Vector3 sum = v1 + v2; // Easy to read
```

Regarding the relationship between binary operators and compound assignment operators, there is a good implementation guideline: **Implement the compound assignment (`+=`) first, then implement the binary operation (`+`) based on it.** This way, the binary operation doesn't need to be a member function—it can be a non-member function implemented by calling `+=`. We will discuss the benefits of this approach in the "Member vs. Non-Member" section later.

## 2. Subscript Operator `[]`

`operator[]` is the **"facade interface" of container classes**. Overloading it is a standard operation for almost any custom container. Its core value lies in making custom types accessible like arrays:

```cpp
MyContainer<int> container;
// ...
int value = container[5]; // Read
container[5] = 10;        // Write
```

A key point is: **You must provide both `const` and non-`const` versions.** The non-`const` version returns a modifiable reference, allowing element modification via the subscript; the `const` version returns a read-only reference, ensuring `const` objects are not accidentally modified.

```cpp
class MyContainer {
    int data[100];
public:
    // Non-const version: allows read/write
    int& operator[](size_t index) {
        return data[index];
    }

    // Const version: allows read-only access
    const int& operator[](size_t index) const {
        return data[index];
    }
};
```

Usage effect:

```cpp
void process(const MyContainer& container) {
    int x = container[10]; // OK: calls const version
    // container[10] = 5; // Error: cannot assign to const reference
}
```

The existence of the `const` version is very important—if there were only the non-`const` version, one could not use `[]` to read data when holding a `const` reference to the object. We mentioned this pitfall in the previous chapter when discussing `const` member functions, and we emphasize it again here: **Providing both `const` and non-`const` versions is standard practice for `operator[]`.**

## 3. Function Call Operator `()`

The function call operator `operator()` allows objects to be invoked like functions. Objects implementing this operator are known as **function objects (functors)**. Compared to ordinary functions, function objects have a unique advantage: **they can carry state**.

```cpp
class Accumulator {
    int sum = 0;
public:
    int operator()(int value) {
        sum += value;
        return sum;
    }
};

Accumulator acc;
int a = acc(10); // Returns 10
int b = acc(20); // Returns 30
```

A typical application of function objects in embedded development is the **callback mechanism**. You can register a function object carrying context information as a callback, rather than being limited to bare function pointers. This became even more convenient with the introduction of lambdas in C++11 (lambdas are function objects under the hood), but even in C++98, hand-writing function objects was a very useful pattern.

## 4. Increment and Decrement Operators `++`/`--`

Increment and decrement operators can be overloaded separately for the prefix version (`++i`) and the postfix version (`i++`). C++ distinguishes between the two by a convention: **the postfix version accepts an extra `int` parameter** (the compiler automatically passes 0), while the prefix version has no extra parameter.

```cpp
class Counter {
    int value = 0;
public:
    // Prefix ++ (++i): returns the modified value
    Counter& operator++() {
        ++value;
        return *this;
    }

    // Postfix ++ (i++): returns the value before modification
    Counter operator++(int) {
        Counter temp = *this;
        ++value;
        return temp;
    }
};
```

Note the difference in return types between prefix and postfix. Prefix `++` returns a reference (because the object has been modified, returning the modified self is logical), while postfix `++` returns a value (because it needs to return a copy of the pre-modified state). This difference also explains why **prefix `++` is generally more efficient than postfix `++`**—the postfix version requires constructing an extra temporary object. For built-in types, this doesn't matter much, but for complex iterator types, prefix `++` can save a copy operation.

Therefore, if you don't need the postfix semantics (which is most of the time), it is a good idea to cultivate the habit of using prefix `++`.

## 5. Type Conversion Operators

Type conversion operators allow objects to be explicitly or implicitly converted to other types, but this is **the type of overload most prone to pitfalls**.

```cpp
class MyString {
public:
    // Implicit conversion to const char*
    operator const char*() const { return data_; }
    // ...
};

void log(const char* str);

MyString str;
log(str); // Implicit conversion happens here
```

The problem with implicit type conversion is that **you cannot control when it happens**. The compiler will automatically invoke the conversion operator whenever it deems it "necessary," even if you had no intention of doing so. If your class has both a conversion operator to Type A and an overloaded constructor taking Type A, confusing ambiguities can arise during overload resolution—the compiler will hesitate between two conversion paths.

My advice is: **Prefer explicit member functions (like `c_str()`, `toInt()`) over type conversion operators**, unless the semantics are extremely clear. If you must use a type conversion operator, C++11's `explicit` keyword can restrict it to take effect only during explicit casting, which is a safer approach.

## 6. Member vs. Non-Member: A Guide to Choosing Overload Location

Operators can be overloaded in two ways: **member functions** and **non-member functions** (usually friends). The choice affects not only syntax but also type conversion behavior.

For **member functions**, the left-hand operand must be an object of the current class (or implicitly convertible to it). This means that if you implement `operator+` as a member function, `obj + scalar` will work, but `scalar + obj` will not—because `scalar` is a `float`, it is not a `Vector3` object, and the compiler will not look for `operator+` in `float`.

For **non-member functions**, the left and right operands are symmetric. The compiler will attempt implicit conversions on both operands, so both `obj + scalar` and `scalar + obj` will work.

A widely accepted rule of thumb is:

- **Symmetric binary operators** (`+`, `-`, `*`, `/`, `==`, `!=`, etc.) should preferably be implemented as **non-member functions**.
- **Assignment-like operators** (`=`, `+=`, `-=`, `*=`, `/=`, `%=`, etc.) must be implemented as **member functions** (the language dictates that certain operators can only be members).
- **Unary operators** (`-`, `!`, `~`, etc.) are usually implemented as **member functions**.

For `Vector3`, a better approach might be to implement `operator+` and `operator*` as non-member friend functions:

```cpp
class Vector3 {
    // ...
    friend Vector3 operator+(Vector3 lhs, const Vector3& rhs);
    friend Vector3 operator*(Vector3 v, float scalar);
};

Vector3 operator+(Vector3 lhs, const Vector3& rhs) {
    lhs += rhs;
    return lhs;
}

Vector3 operator*(Vector3 v, float scalar) {
    v.x *= scalar;
    v.y *= scalar;
    v.z *= scalar;
    return v;
}
```

This way, both `vec + vec` and `scalar * vec` (if you overload for that order too) can work correctly.

## 7. Which Operators Should Not Be Overloaded

Not all operators are suitable for overloading. Overloading some operators can lead to confusing behavior or even break fundamental guarantees of the language.

**Logical operators `&&` and `||`** are the most typical counter-examples. In C++, the built-in `&&` and `||` have a very important characteristic—**short-circuit evaluation**. For `a && b`, if `a` is `false`, `b` is not evaluated. But once you overload `&&`, it becomes a normal function call—**both parameters are evaluated before the function is called**, and the short-circuit evaluation characteristic is completely lost. This not only violates the intuitive expectations of all C++ programmers regarding `&&` and `||`, but can also produce completely different behavior if `b` has side effects.

**The comma operator `,`** has a similar problem. The built-in comma operator guarantees a left-to-right evaluation order, but the overloaded version cannot provide this guarantee.

**The address-of operator `&`** should almost never be overloaded—it returns the address of the object, which is one of the fundamental operations of C++. Changing its semantics will cause almost all code to fail.

My advice is: **Only overload operators with natural semantics that do not violate intuitive expectations.** Specifically, arithmetic operators, comparison operators, subscript operators, function call operators, and stream operators—these can be overloaded safely. As for logical operators, the comma operator, and the address-of operator—stay away from them.

## Summary

Operator overloading allows custom types to participate in expression calculations like built-in types, greatly enhancing code readability and expressiveness. We learned how to overload arithmetic operators, subscript operators, function call operators, increment/decrement operators, and type conversion operators, as well as strategies for choosing between member and non-member overloads.

There is only one core principle of operator overloading: **Make the code read naturally.** If your overloaded operator confuses the reader, it is a bad overload. Keeping this guideline in mind will help you make the right choice in most situations.

In the next article, we will learn about C++'s four type conversion operators, dynamic memory management mechanisms, and exception handling—these are more "advanced" features in C++98 and are also the foundation for understanding the direction of modern C++ improvements.
