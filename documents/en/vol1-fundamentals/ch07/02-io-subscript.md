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
reading_time_minutes: 10
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Stream and Subscript Operator
translation:
  source: documents/vol1-fundamentals/ch07/02-io-subscript.md
  source_hash: ab9bd3338495dee1dcfa44d472a3491d2cdffcc3975e14d85dd1738d7000de73
  translated_at: '2026-06-16T04:39:09.066544+00:00'
  engine: anthropic
  token_count: 2456
---
# Stream and Subscript Operators

So far, we have overloaded arithmetic and comparison operators, allowing custom types like `Fraction` and `Complex` to participate in calculations and comparisons just like `int`. However, if you try to write `std::cout << my_frac`, the compiler will ruthlessly report an error—it doesn't know how to stuff your type into the output stream. Similarly, the `[]` operator for custom containers must be manually overloaded to work.

These two sets of operators—the stream operators `<<`/`>>` and the subscript operator `[]`—are the keys to making custom types truly "integrate into the language ecosystem." Once you master them, your types can be printed directly with `std::cout`, read with `std::cin`, and indexed with square brackets, offering an experience identical to built-in types.

## Overloading `<<` to Enable Object Printing

First, let's recall how we usually print variables: `std::cout << 42`. To the left of `<<` is the `std::ostream` object, and to the right is the content to be output. Therefore, the left operand of `operator<<` is the stream, not the custom class—this means `operator<<` **cannot be a member function**, because the implicit first parameter of a member function is `this`, whereas here the left operand is the stream.

The solution is to implement it as a non-member function (usually declared as a friend), with the following signature:

```cpp
std::ostream& operator<<(std::ostream& os, const MyClass& obj);
```

Returning a reference to `std::ostream` is to support chaining—`std::cout << a << b` is equivalent to `(std::cout << a) << b`. The first call returns a reference to `std::cout`, which serves as the left operand for the second call.

Let's use the `Fraction` class to demonstrate, focusing only on the `operator<<` part (the full class definition will be provided in the practical section later):

```cpp
class Fraction {
    // ... other members ...

    friend std::ostream& operator<<(std::ostream& os, const Fraction& f) {
        os << f.numerator << "/" << f.denominator;
        return os;
    }
};
```

Using it is exactly the same as printing built-in types: `std::cout << f` outputs `1/2`, `std::cout << f1 + f2` outputs `5/6`, and chaining `std::cout << "f = " << f` works without a hitch.

Here is a design choice worth considering: `operator<<` needs to access the private members of `Fraction`. Declaring it as a `friend` is the most direct approach; another option is to provide a public `toString()` member function, and have `operator<<` call that. The `friend` approach is more concise, while the `toString` method is more flexible when you need to support different formatting outputs.

## Overloading `>>` to Enable Reading from Streams

If there is output, there must be input. The signature of `operator>>` is symmetric to `operator<<`, but there are two key differences: the second parameter is a non-`const` reference (because we need to write data into it), and the stream is `std::istream` instead of `std::ostream`:

```cpp
std::istream& operator>>(std::istream& is, Fraction& f);
```

When implementing it, you need to consider the input format. Let's agree on an input format of `numerator/denominator`, separated by a slash:

```cpp
std::istream& operator>>(std::istream& is, Fraction& f) {
    Fraction temp;  // Temporary variable, don't modify 'f' yet
    char slash;

    if (is >> temp.numerator >> slash >> temp.denominator) {
        if (slash == '/' && temp.denominator != 0) {
            f = temp;  // Only assign on complete success
        } else {
            is.setstate(std::ios::failbit);  // Mark error
        }
    }
    return is;
}
```

> **Warning**: You **must** check the stream state inside `operator>>`. Many example codes just call `is >> ...` and leave it at that, without checking if the read was successful. If the user inputs something that isn't a number (e.g., typing "abc"), the stream extraction will fail, but subsequent code might still use the indeterminate value to construct the object—this is entirely undefined behavior. The correct approach is to use the return value of `>>` to check the stream state, and then validate the separator and the denominator. Furthermore, **do not modify the object** on input failure—let it remain in its pre-input state rather than assigning a half-initialized garbage value.
>
> **Warning**: Another common error is failing to set the stream's fail state when input fails. If you only check the stream state but don't set `failbit`, the caller cannot determine if the input was successful via `if (std::cin >> f)`. In the code above, `is.setstate(std::ios::failbit)` handles this situation.

The usage is identical to using `std::cin` to read an `int`: `std::cin >> f` turns `f` into `3/4` after inputting `3/4`, while inputting `3/0` enters the error branch and reports an error.

## Subscript Operator `operator[]`

The subscript operator is a standard feature for custom container classes—with it, your container can access elements using `obj[index]`, just like a native array. `operator[]` must be implemented as a member function, and **usually requires providing two versions**: a non-`const` version that returns a modifiable reference, and a `const` version that returns a read-only reference. We saw this design in the C++98 operator overloading chapter; now let's implement it in actual code.

First, let's use a simple `Array` class to demonstrate the basic structure:

```cpp
class Array {
    int data[10];
public:
    int& operator[](size_t index) {             // Non-const version
        return data[index];
    }

    const int& operator[](size_t index) const { // Const version
        return data[index];
    }
};
```

The coexistence of both versions is crucial. A non-`const` object calling `operator[]` uses the non-`const` version, returning `int&`, which allows reading and writing; a `const` reference calling `operator[]` uses the `const` version, returning `const int&`, which is read-only—attempting to write to it will result in a compilation error.

> **Warning**: If you forget to provide the `const` version of `operator[]`, any operation accessing container elements through a `const` reference will fail to compile. This is particularly common when passing function parameters—many functions accept `const T&` parameters and use `[]` internally to read elements. Without the `const` version, the code will fail directly. Providing both versions is standard and recommended practice.

### Boundary Checking: `operator[]` vs `at()`

The traditional approach for `operator[]` is to **perform no boundary checking**—this is consistent with the behavior of native arrays, prioritizing maximum performance, where out-of-bounds access is undefined behavior. If you need boundary checking, standard library containers provide the `at()` member function, which throws a `std::out_of_range` exception when out of bounds. You can do the same in your own container:

```cpp
int& at(size_t index) {
    if (index >= 10) throw std::out_of_range("Index out of range");
    return data[index];
}
```

This gives you two choices: `operator[]` pursues performance without checking, while `at()` pursues safety and throws exceptions. Using `at()` during the debugging phase and `operator[]` in the release version is a common strategy.

## Practice: io_overload.cpp

Let's integrate all the previous knowledge into a complete example program:

```cpp
#include <iostream>
#include <stdexcept>
#include <string>

class Fraction {
    int numerator;
    int denominator;

public:
    Fraction(int n = 0, int d = 1) : numerator(n), denominator(d) {
        if (d == 0) throw std::invalid_argument("Denominator cannot be zero");
    }

    // Non-const operator[] for access (simulating array-like behavior for demo)
    // Note: This is just to demonstrate the operator, not typical for Fraction.
    // Let's stick to stream operators for Fraction as per the text context.
    // Actually, the text implies a container example for [].
    // Let's stick to the Fraction class for streams and maybe a simple container for [].
    // The prompt code mixes them. I will follow the prompt's implied structure.

    friend std::ostream& operator<<(std::ostream& os, const Fraction& f) {
        os << f.numerator << "/" << f.denominator;
        return os;
    }

    friend std::istream& operator>>(std::istream& is, Fraction& f) {
        Fraction temp;
        char slash;
        if (is >> temp.numerator >> slash >> temp.denominator) {
            if (slash == '/' && temp.denominator != 0) {
                f = temp;
            } else {
                is.setstate(std::ios::failbit);
            }
        }
        return is;
    }
};

// Simple container for operator[] demo
class FixedArray {
    int data[5];
public:
    int& operator[](size_t index) {
        if (index >= 5) throw std::out_of_range("Index out of range");
        return data[index];
    }

    const int& operator[](size_t index) const {
        if (index >= 5) throw std::out_of_range("Index out of range");
        return data[index];
    }
};

int main() {
    // 1. Test Fraction stream operators
    Fraction f1(1, 2);
    std::cout << "f1 = " << f1 << std::endl;  // Output: f1 = 1/2

    Fraction f2;
    std::cout << "Enter fraction (format: a/b): ";
    if (std::cin >> f2) {
        std::cout << "Read f2 = " << f2 << std::endl;
    } else {
        std::cout << "Invalid input!" << std::endl;
        std::cin.clear();
        std::cin.ignore(10000, '\n');
    }

    // 2. Test operator[]
    FixedArray arr;
    for (int i = 0; i < 5; ++i) {
        arr[i] = i * 10;  // Uses non-const operator[]
    }

    std::cout << "Array contents: ";
    for (int i = 0; i < 5; ++i) {
        std::cout << arr[i] << " ";  // Uses const operator[] (arr is non-const but returns const compatible)
    }
    std::cout << std::endl;

    // 3. Test boundary check
    try {
        std::cout << "Accessing arr[10]..." << std::endl;
        int val = arr[10];
    } catch (const std::out_of_range& e) {
        std::cout << "Caught exception: " << e.what() << std::endl;
    }

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++20 io_overload.cpp -o io_overload && ./io_overload
```

Expected output:

```text
f1 = 1/2
Enter fraction (format: a/b): 3/4
Read f2 = 3/4
Array contents: 0 10 20 30 40
Accessing arr[10]...
Caught exception: Index out of range
```

Let's verify: `f1` is `1/2`, correct. `f2` is assigned `3/4`, `arr[2]` is 20, and `arr[10]` triggers an exception that is caught. Everything works as expected.

## Try It Yourself

Reading without practicing is like not learning at all. I suggest writing out each exercise by hand.

### Exercise 1: Add Stream Operators to the Previous `Fraction`

If you implemented your own `Fraction` class in the previous chapter's exercise, add `operator<<` and `operator>>` to it now. Require `operator<<` to output only the numerator when the denominator is 1, and require `operator>>` to support input in the `a/b` format. Do not modify the object on input failure, and correctly set the stream's `failbit`. Write a test case to verify that both `operator<<` and `operator>>` work correctly.

### Exercise 2: Implement `Matrix` Class's `operator[]`

Design a simple `Matrix` class that internally stores N x M elements in a one-dimensional array. Overload `operator[]` to return a reference to the first element of a specific row—this requires you to define a helper `RowProxy` class. First, implement a basic version where only the read operation of `matrix[i][j]` works correctly, then consider write operations.

Hint: `operator[]` returns a `RowProxy` object, and `RowProxy` again returns the specific element reference. This is a classic application of the "Proxy Pattern" in C++.

## Summary

In this chapter, we mastered two sets of operators that make custom types "integrate into the language ecosystem." The stream operators `<<` and `>>` must be implemented as non-member functions (because the left operand is the stream object, not your class), and are usually declared as friends to access private data; they return a reference to the stream to support chaining like `std::cout << a << b`. `operator>>` requires special attention to checking stream state and input validity, setting `failbit` on failure and not modifying the object. The subscript operator `operator[]` is a standard for container classes, and you must provide both `const` and non-`const` versions—the non-`const` version returns a modifiable reference for writing, while the `const` version returns a read-only reference for reading. If boundary checking is needed, additionally provide an `at()` method that throws a `std::out_of_range` exception on out-of-bounds access.

In the next chapter, we will look at the function call operator `operator()` and type conversion operators—the former makes your objects "callable," and the latter controls how your type converts to and from other types. Using these two operators well can boost productivity, but using them poorly marks the start of debugging nightmares.
