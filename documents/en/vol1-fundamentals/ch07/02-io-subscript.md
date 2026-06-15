---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: Master `<<` and `>>` overloading and `operator[]` implementation to enable
  stream I/O and indexed access for custom types.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 算术与比较运算符
reading_time_minutes: 11
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Streams and Subscript Operators
translation:
  engine: anthropic
  source: documents/vol1-fundamentals/ch07/02-io-subscript.md
  source_hash: c7b632b75413f8f7dee3290b8ab2aa83cc955b0d120187dee1e1f058d5f06154
  token_count: 2459
  translated_at: '2026-05-26T10:53:03.243681+00:00'
---
# Stream and Subscript Operators

So far, we have overloaded arithmetic and comparison operators, allowing custom types like `Fraction` and `Vector3D` to participate in calculations and comparisons just like `int`. But if we try to write `std::cout << fraction;`, the compiler will bluntly throw an error—it does not know how to shove our type into an output stream. Similarly, the `container[0]` of a custom container requires us to manually overload `operator[]` for it to work.

These two groups of operators—the stream operators `<<`/`>>` and the subscript operator `[]`—are key to truly integrating custom types into the language ecosystem. Once we get them right, our types can be printed directly with `cout`, read with `cin`, and indexed with square brackets, providing an experience completely consistent with built-in types.

## Overloading << to Make Objects Printable

First, let us recall how we usually print variables: `std::cout << 42 << " hello";`. The left side of `<<` is an `std::ostream` object, and the right side is the content to output. Therefore, the left operand of `std::cout << fraction` is `ostream`, not `Fraction`—this means `operator<<` **cannot be a member function**, because the implicit first parameter of a member function is `this`, whereas the left operand here is the stream.

The solution is to implement it as a non-member function (usually declared as a friend), with the following signature:

```cpp
friend std::ostream& operator<<(std::ostream& os, const Fraction& f);
```

Returning a reference to `os` supports chained calls—`cout << a << b` is equivalent to `operator<<(operator<<(cout, a), b)`, where the first call returns a reference to `cout`, serving as the left operand for the second call.

Let us use the `Fraction` class for demonstration, focusing only on the `operator<<` part (we will provide the full class definition in the hands-on section later):

```cpp
friend std::ostream& operator<<(std::ostream& os, const Fraction& f)
{
    if (f.denominator == 1) {
        os << f.numerator;       // 整数形式：5/1 只输出 5
    }
    else {
        os << f.numerator << "/" << f.denominator;
    }
    return os;
}
```

Using it is exactly the same as printing built-in types: `std::cout << Fraction(3, 4)` outputs `3/4`, `std::cout << Fraction(5, 1)` outputs `5`, and chained calls like `cout << a << " and " << b` work without a hitch.

Here is a design choice worth considering: `operator<<` needs to access the private members of `Fraction`. Declaring it as a `friend` is the most straightforward approach; another option is to provide a public `print` member function, and then have `operator<<` call it. `friend` is more concise, while the `print` approach is more flexible when we need to support different formatted outputs.

## Overloading >> to Read Objects from a Stream

Where there is output, there must be input. The signature of `operator>>` is symmetrical to `operator<<`, but there are two key differences: the second parameter is not a `const` reference (because we need to write data into it), and the stream is `std::istream` rather than `ostream`:

```cpp
friend std::istream& operator>>(std::istream& is, Fraction& f);
```

When implementing this, we need to consider the input format. We agree on an input format of `numerator/denominator`, separated by slashes:

```cpp
friend std::istream& operator>>(std::istream& is, Fraction& f)
{
    int num, denom;
    char slash;

    is >> num >> slash >> denom;

    // 检查流状态和分母合法性
    if (is && slash == '/' && denom != 0) {
        f.numerator = num;
        f.denominator = denom;
        f.reduce();
    }
    else {
        // 输入失败时设置流为失败状态
        is.setstate(std::ios::failbit);
    }

    return is;
}
```

> **Pitfall Warning**: We must check the stream state inside `operator>>`. Many example codes simply call `is >> num >> slash >> denom;` and leave it at that, without even checking if the read was successful. If the user inputs something that is not a number (such as typing a `abc`), `is >> num` will fail, but the subsequent code will still use an indeterminate value to construct the object—this is entirely undefined behavior (UB). The correct approach is to use `if (is)` to check the stream state, and then verify the separator and denominator validity. Additionally, **do not modify the object** on input failure—let it remain in its pre-input state, rather than assigning a half-initialized garbage value.
>
> **Pitfall Warning**: Another common mistake is not setting `failbit` when input fails. If we only check the stream state but do not set `failbit`, the caller cannot determine whether the input was successful via `if (cin >> fraction)`. The `is.setstate(std::ios::failbit)` in the code above handles this exact situation.

The usage is exactly the same as using `cin >>` to read a `int`: `if (std::cin >> f)` will make `f` become `Fraction(3, 4)` after inputting `3/4`, while inputting `abc` will enter the `else` branch and report an error.

## Subscript Operator operator[]

The subscript operator is standard equipment for custom container classes—with it, our containers can access elements using `obj[i]`, providing an experience completely consistent with native arrays. `operator[]` must be implemented as a member function, and **usually requires two versions**: a non-`const` version that returns a modifiable reference, and a `const` version that returns a read-only reference. We saw this design back in the C++98 operator overloading chapter, and now we are putting it into actual code.

First, let us use a concise `IntArray` to demonstrate the basic structure:

```cpp
class IntArray {
private:
    int* data;
    std::size_t count;

public:
    explicit IntArray(std::size_t n)
        : data(new int[n]()), count(n)
    {
    }

    ~IntArray() { delete[] data; }

    // 禁止拷贝（简化示例，后面章节会讲移动语义）
    IntArray(const IntArray&) = delete;
    IntArray& operator=(const IntArray&) = delete;

    // 非 const 版本：允许读写
    int& operator[](std::size_t index)
    {
        return data[index];
    }

    // const 版本：只读
    const int& operator[](std::size_t index) const
    {
        return data[index];
    }

    std::size_t size() const { return count; }
};
```

The coexistence of both versions is crucial. A non-`const` object calling `arr[0] = 42` goes through the non-`const` version, returning a `int&` that allows reading and writing; a `const` reference calling `ref[0]` goes through the `const` version, returning a `const int&` that is read-only—attempting `ref[0] = 100` will result in a direct compilation error.

> **Pitfall Warning**: If we forget to provide the `const` version of `operator[]`, any operation that accesses container elements through a `const` reference will fail to compile. This is particularly common when passing function parameters—many functions accept `const IntArray&` parameters and use `arr[i]` internally to read elements; without the `const` version, it will directly error out. Providing two versions is the standard and recommended practice.

### Boundary Checking: operator[] vs at()

The traditional approach for `operator[]` is to **not perform boundary checking**—this is consistent with native array behavior, pursuing maximum performance, where out-of-bounds access is undefined behavior (UB). If we need boundary checking, standard library containers provide the `at()` member function, which throws an `std::out_of_range` exception when out of bounds. We can follow the same pattern in our own containers:

```cpp
int& at(std::size_t index)
{
    if (index >= count) {
        throw std::out_of_range("IntArray::at: index out of range");
    }
    return data[index];
}

const int& at(std::size_t index) const
{
    if (index >= count) {
        throw std::out_of_range("IntArray::at: index out of range");
    }
    return data[index];
}
```

This gives us two choices: `[]` pursues performance without checking, while `at()` pursues safety by throwing exceptions. Using `at()` during the debugging phase and `[]` in release builds is a common strategy.

## Hands-on: io_overload.cpp

Let us integrate all the previous knowledge into a complete example program:

```cpp
// io_overload.cpp
// 流运算符和下标运算符综合演练

#include <iostream>
#include <stdexcept>
#include <cmath>

class Fraction {
private:
    int numerator;
    int denominator;

    void reduce()
    {
        int a = std::abs(numerator);
        int b = std::abs(denominator);
        while (b != 0) {
            int temp = b;
            b = a % b;
            a = temp;
        }
        int gcd = (a != 0) ? a : 1;
        numerator /= gcd;
        denominator /= gcd;
        if (denominator < 0) {
            numerator = -numerator;
            denominator = -denominator;
        }
    }

public:
    Fraction(int num = 0, int denom = 1)
        : numerator(num), denominator(denom)
    {
        if (denominator == 0) {
            throw std::invalid_argument("分母不能为零");
        }
        reduce();
    }

    double to_double() const
    {
        return static_cast<double>(numerator) / denominator;
    }

    // 加法
    Fraction operator+(const Fraction& other) const
    {
        return Fraction(
            numerator * other.denominator + other.numerator * denominator,
            denominator * other.denominator
        );
    }

    // 输出流
    friend std::ostream& operator<<(std::ostream& os, const Fraction& f)
    {
        if (f.denominator == 1) {
            os << f.numerator;
        }
        else {
            os << f.numerator << "/" << f.denominator;
        }
        return os;
    }

    // 输入流
    friend std::istream& operator>>(std::istream& is, Fraction& f)
    {
        int num = 0;
        int denom = 1;
        char slash = '\0';

        is >> num >> slash >> denom;

        if (is && slash == '/' && denom != 0) {
            f.numerator = num;
            f.denominator = denom;
            f.reduce();
        }
        else {
            is.setstate(std::ios::failbit);
        }

        return is;
    }
};

class IntArray {
private:
    int* data;
    std::size_t count;

public:
    explicit IntArray(std::size_t n)
        : data(new int[n]()), count(n)
    {
    }

    ~IntArray() { delete[] data; }

    IntArray(const IntArray&) = delete;
    IntArray& operator=(const IntArray&) = delete;

    int& operator[](std::size_t index)
    {
        return data[index];
    }

    const int& operator[](std::size_t index) const
    {
        return data[index];
    }

    const int& at(std::size_t index) const
    {
        if (index >= count) {
            throw std::out_of_range("IntArray::at: index out of range");
        }
        return data[index];
    }

    std::size_t size() const { return count; }

    /// @brief 打印所有元素
    void print(std::ostream& os = std::cout) const
    {
        os << "[";
        for (std::size_t i = 0; i < count; ++i) {
            os << data[i];
            if (i + 1 < count) {
                os << ", ";
            }
        }
        os << "]";
    }
};

int main()
{
    // --- Fraction 输出演示 ---
    Fraction a(3, 4);
    Fraction b(2, 6);   // 自动约分为 1/3
    Fraction c(6, 1);   // 整数形式

    std::cout << "a = " << a << std::endl;    // 3/4
    std::cout << "b = " << b << std::endl;    // 1/3
    std::cout << "c = " << c << std::endl;    // 6
    std::cout << "a + b = " << (a + b) << std::endl;  // 13/12
    std::cout << "a (double) = " << a.to_double() << std::endl;  // 0.75
    std::cout << std::endl;

    // --- IntArray 下标访问演示 ---
    IntArray arr(5);
    for (std::size_t i = 0; i < arr.size(); ++i) {
        arr[i] = static_cast<int>(i * 10);  // 通过 [] 写入
    }

    std::cout << "arr = ";
    arr.print();
    std::cout << std::endl;

    const IntArray& const_arr = arr;
    std::cout << "const_arr[2] = " << const_arr[2] << std::endl;  // 20

    // 边界检查
    try {
        std::cout << "arr.at(10) = " << arr.at(10) << std::endl;
    }
    catch (const std::out_of_range& e) {
        std::cout << "捕获异常: " << e.what() << std::endl;
    }

    return 0;
}
```

Compile and run: `g++ -std=c++17 -Wall -Wextra -o io_overload io_overload.cpp && ./io_overload`

Expected output:

```text
a = 3/4
b = 1/3
c = 6
a + b = 13/12
a (double) = 0.75

arr = [0, 10, 20, 30, 40]
const_arr[2] = 20
捕获异常: IntArray::at: index out of range
```

Let us verify: `3/4 + 1/3 = 9/12 + 4/12 = 13/12`, correct. `arr` is assigned to `{0, 10, 20, 30, 40}`, `const_arr[2]` is 20, and the `at(10)` out-of-bounds access is caught by the exception—everything works perfectly.

## Try It Yourself

Reading without practicing is useless; we recommend writing out each exercise by hand.

### Exercise 1: Add Stream Operators to the Previous Fraction

If we implemented our own `Fraction` class following the previous chapter's exercises, now let us add `operator<<` and `operator>>` to it. Require `operator<<` to output only the numerator when the denominator is 1, and `operator>>` to support input in the `分子/分母` format. Do not modify the object on input failure, and correctly set the stream's `failbit`. Write a test snippet to verify that both `cin >> fraction` and `cout << fraction` work correctly.

### Exercise 2: Implement operator[] for a Matrix Class

Design a simple `Matrix` class that stores N x M elements internally using a one-dimensional array. Overload `operator[]` so that it returns a reference to the first element of a given row—this requires us to define a helper `Row` proxy class. First, implement a basic version where only read operations via `matrix[i][j]` work correctly, then consider write operations.

Hint: `matrix[i]` returns a `Row` object, and `Row::operator[]` then returns a reference to the specific element. This is a classic use of the "proxy pattern" in C++.

## Summary

In this chapter, we mastered two groups of operators that integrate custom types into the language ecosystem. The stream operators `<<` and `>>` must be implemented as non-member functions (because the left operand is a stream object, not our class), and are usually declared as friends to access private data; returning a reference to the stream supports chained calls like `cout << a << b << c`. We must pay special attention to checking the stream state and input validity in `operator>>`, setting `failbit` on failure without modifying the object. The subscript operator `operator[]` is standard for container classes, and we must provide both `const` and non-`const` versions—the non-`const` version returns a modifiable reference for writing, while the `const` version returns a read-only reference for reading. If boundary checking is needed, additionally provide an `at()` method that throws an `std::out_of_range` exception when out of bounds.

In the next chapter, we will look at the function call operator `operator()` and type conversion operators—the former makes our objects "callable," while the latter controls how our type converts to and from other types. Using these two operators well boosts productivity, but using them poorly is the starting point of debugging nightmares.
