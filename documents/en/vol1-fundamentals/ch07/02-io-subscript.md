---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the implementation of `<<`/`>>` overloading and `operator[]`, giving custom types stream I/O and indexed access.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- Arithmetic and Comparison Operators
reading_time_minutes: 21
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Stream and Subscript Operators
translation:
  source: documents/vol1-fundamentals/ch07/02-io-subscript.md
  source_hash: bf04a66081af6ee6c1f4a07c2396c08d5a42919d0c3d00bd7c6faf967bb76609
  translated_at: '2026-09-25T11:13:16+00:00'
  engine: anthropic
  token_count: 5300
---
# Stream and Subscript Operators: Making cout Recognize Your Types

So far we have overloaded the arithmetic and comparison operators, letting custom types like `Fraction` and `Vector3D` take part in arithmetic and comparisons just like `int`. But try writing `std::cout << fraction;` and the compiler will flatly refuse—it has no idea how to stuff our type into an output stream. Likewise, `container[0]` on a custom container only works once we overload `operator[]` ourselves.

These two families of operators (the stream operators `<<`/`>>` and the subscript operator `[]`) are what let a custom type truly blend into the language ecosystem. Once you have them in place, your types can be printed with `cout`, read with `cin`, and indexed with square brackets—exactly the experience built-in types give you.

## Overloading `<<` So Objects Can Be Printed

Recall how we usually print variables: `std::cout << 42 << " hello";`. The left operand of `<<` is a `std::ostream` object, and the right operand is the content. So in `std::cout << fraction`, the left operand is the stream, not a `Fraction`—which means `operator<<` **cannot be a member function**, because the implicit first parameter of a member function is `this`, and here the left operand is a stream.

Our solution is to implement it as a non-member function (usually declared as a friend), with the signature:

```cpp
friend std::ostream& operator<<(std::ostream& os, const Fraction& f);
```

We return a reference to `os` to support chaining: `cout << a << b` is equivalent to `operator<<(operator<<(cout, a), b)`—the first call returns a reference to `cout`, which then serves as the left operand of the second call.

Let's demonstrate with the `Fraction` class, looking only at the `operator<<` part (the full class definition shows up in the practice section below):

```cpp
friend std::ostream& operator<<(std::ostream& os, const Fraction& f)
{
    if (f.denominator == 1) {
        os << f.numerator;       // Integer form: 5/1 prints as just 5
    }
    else {
        os << f.numerator << "/" << f.denominator;
    }
    return os;
}
```

Usage is identical to printing built-in types: `std::cout << Fraction(3, 4)` prints `3/4`, `std::cout << Fraction(5, 1)` prints `5`, and chaining like `cout << a << " and " << b` works without a hitch.

There is a design choice worth pondering here: `operator<<` needs access to `Fraction`'s private members. Declaring it a `friend` is the most direct route; the alternative is to provide a public `print` member function and have `operator<<` call that. `friend` is more concise, while the `print` method is more flexible when you need to support different output formats.

## Overloading `>>` to Read Objects from a Stream

With output comes input. The signature of `operator>>` mirrors `operator<<`, with two key differences: the second parameter is not a `const` reference (because we are writing data into it), and the stream is a `std::istream` rather than an `ostream`:

```cpp
friend std::istream& operator>>(std::istream& is, Fraction& f);
```

The implementation has to settle on an input format. We agree on `numerator/denominator`, separated by a slash:

```cpp
friend std::istream& operator>>(std::istream& is, Fraction& f)
{
    int num, denom;
    char slash;

    is >> num >> slash >> denom;

    // Check the stream state and denominator validity
    if (is && slash == '/' && denom != 0) {
        f.numerator = num;
        f.denominator = denom;
        f.reduce();
    }
    else {
        // On failed input, put the stream into a failed state
        is.setstate(std::ios::failbit);
    }

    return is;
}
```

Inside `operator>>` we absolutely must check the stream state. Plenty of sample code just does `is >> num >> slash >> denom;` and moves on, never checking whether the reads succeeded. If the user types something non-numeric like `abc`, `is >> num` fails, yet the code that follows still builds the object from indeterminate values—pure undefined behavior. The right approach is to check the stream state with `if (is)`, then validate the separator and the denominator. One more rule: on input failure, **do not modify the object**—leave it in its pre-input state instead of assigning it a half-initialized garbage value.

Another common mistake is not setting `failbit` when input fails. If we only check the stream state but never set `failbit`, the caller has no way to tell whether input succeeded via `if (cin >> fraction)`. That is exactly what `is.setstate(std::ios::failbit)` in the code above handles.

Usage works exactly like `cin >>` for an `int`: after typing `3/4`, `if (std::cin >> f)` leaves `f` as `Fraction(3, 4)`; typing `abc` takes the failure branch and reports the error.

## The Subscript Operator `operator[]`

The subscript operator is the standard fixture of a custom container class: with it, our container supports `obj[i]` element access, matching the native array experience exactly. `operator[]` must be implemented as a member function, and **usually comes in two versions**: a non-`const` version returning a modifiable reference, and a `const` version returning a read-only reference. We saw this design in the operator overloading chapter; here we put it into actual code.

Let's demonstrate the basic structure with a compact `IntArray`:

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

    // Copying disabled (simplified example; move semantics comes in a later chapter)
    IntArray(const IntArray&) = delete;
    IntArray& operator=(const IntArray&) = delete;

    // Non-const version: read-write
    int& operator[](std::size_t index)
    {
        return data[index];
    }

    // Const version: read-only
    const int& operator[](std::size_t index) const
    {
        return data[index];
    }

    std::size_t size() const { return count; }
};
```

The coexistence of both versions is the crux of this design. Calling `arr[0] = 42` on a non-`const` object goes through the non-`const` version and returns `int&`, which reads and writes; accessing `ref[0]` through a `const` reference goes through the `const` version and returns `const int&`, read-only—attempting `ref[0] = 100` fails to compile on the spot.

If we forget to provide the `const` version of `operator[]`, any access to container elements through a `const` reference stops compiling. This bites most often at function boundaries—plenty of functions take a `const IntArray&` parameter and read elements with `arr[i]` inside; without the `const` version that is an immediate error. Providing both versions is the standard, recommended practice.

### Bounds Checking: `operator[]` vs `at()`

The traditional approach for `operator[]` is to **perform no bounds checking**—consistent with native arrays, chasing maximum performance, with out-of-bounds access being undefined behavior. So what do you do when you *do* want checking? The standard library's convention is to additionally provide an `at()` member function: it checks the index first and throws a `std::out_of_range` exception when the index is out of bounds, reporting the error loudly instead of letting the program wander into undefined behavior.

We won't formally cover exceptions until the exception-handling chapter, so for now just record the conclusion: `[]` is fast but unchecked; `at()` adds one check and fails immediately on an out-of-bounds index—we will meet it again when we get to the standard library containers. Follow the same convention for your own containers: `operator[]` generally does not check, and if you want a safe variant, add an `at()`. Leaning on `at()` during debugging and switching to `[]` in release builds is a common strategy.

## Practice: io_overload.cpp

Let's pull everything above into one complete example program:

```cpp
// io_overload.cpp
// Stream and subscript operators: a combined walkthrough

#include <iostream>
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
            denominator = 1;   // Same simplification as the previous article
        }
        reduce();
    }

    double to_double() const
    {
        return static_cast<double>(numerator) / denominator;
    }

    // Addition
    Fraction operator+(const Fraction& other) const
    {
        return Fraction(
            numerator * other.denominator + other.numerator * denominator,
            denominator * other.denominator
        );
    }

    // Output stream
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

    // Input stream
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

    std::size_t size() const { return count; }

    /// @brief Print all elements
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
    // --- Fraction output demo ---
    Fraction a(3, 4);
    Fraction b(2, 6);   // Automatically reduced to 1/3
    Fraction c(6, 1);   // Integer form

    std::cout << "a = " << a << std::endl;    // 3/4
    std::cout << "b = " << b << std::endl;    // 1/3
    std::cout << "c = " << c << std::endl;    // 6
    std::cout << "a + b = " << (a + b) << std::endl;  // 13/12
    std::cout << "a (double) = " << a.to_double() << std::endl;  // 0.75
    std::cout << std::endl;

    // --- IntArray subscript demo ---
    IntArray arr(5);
    for (std::size_t i = 0; i < arr.size(); ++i) {
        arr[i] = static_cast<int>(i * 10);  // Write via []
    }

    std::cout << "arr = ";
    arr.print();
    std::cout << std::endl;

    const IntArray& const_arr = arr;
    std::cout << "const_arr[2] = " << const_arr[2] << std::endl;  // 20

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
```

Let's double-check: `3/4 + 1/3 = 9/12 + 4/12 = 13/12`, correct. `arr` ends up as `{0, 10, 20, 30, 40}` and `const_arr[2]` is 20—all good.

## Try It Yourself

Reading without practicing amounts to not learning. Write every exercise out yourself.

### Exercise 1: Add Stream Operators to the Previous `Fraction`

If you implemented your own `Fraction` class in the previous chapter's exercise, add `operator<<` and `operator>>` to it now. `operator<<` must print only the numerator when the denominator is 1, and `operator>>` must accept input in the `numerator/denominator` format. On input failure, leave the object unmodified and set the stream's `failbit` correctly. Then write a test that verifies both `cin >> fraction` and `cout << fraction` work.

::: details Reference Solution

```cpp
#include <iostream>
#include <istream>
#include <ostream>

class Fraction {
 private:
  int numerator_;    // Numerator
  int denominator_;  // Denominator
  void reduce() {
    int a = std::abs(numerator_);
    int b = std::abs(denominator_);

    // Euclidean algorithm for the greatest common divisor
    while (b != 0) {
      int temp = b;
      b = a % b;
      a = temp;
    }

    int gcd = (a != 0) ? a : 1;

    // Reduce the fraction
    numerator_ /= gcd;
    denominator_ /= gcd;

    if (denominator_ < 0) {
      numerator_ = -numerator_;
      denominator_ = -denominator_;
    }
  }

 public:
  Fraction(int num = 0, int den = 1) : numerator_(num), denominator_(den) {
    if (denominator_ == 0) {
      denominator_ = 1;
    }
    reduce();
  }
  // Unary operator
  Fraction operator-() const { return Fraction(-numerator_, denominator_); }
  // Compound assignment operators
  Fraction& operator+=(const Fraction& rhs) {
    this->numerator_ = this->numerator_ * rhs.denominator_ +
                       this->denominator_ * rhs.numerator_;
    this->denominator_ = this->denominator_ * rhs.denominator_;
    reduce();
    return *this;
  }
  Fraction& operator-=(const Fraction& rhs) {
    this->numerator_ = this->numerator_ * rhs.denominator_ -
                       this->denominator_ * rhs.numerator_;
    this->denominator_ = this->denominator_ * rhs.denominator_;
    reduce();
    return *this;
  }
  Fraction& operator*=(const Fraction& rhs) {
    this->numerator_ = this->numerator_ * rhs.numerator_;
    this->denominator_ = this->denominator_ * rhs.denominator_;
    reduce();
    return *this;
  }
  Fraction& operator/=(const Fraction& rhs) {
    if (rhs.numerator_ == 0) {
      return *this;
    }
    this->numerator_ = this->numerator_ * rhs.denominator_;
    this->denominator_ = this->denominator_ * rhs.numerator_;
    reduce();
    return *this;
  }
  friend bool operator<(const Fraction& lhs, const Fraction& rhs) {
    return (lhs.numerator_ * rhs.denominator_) <
           (rhs.numerator_ * lhs.denominator_);
  }
  friend std::ostream& operator<<(std::ostream& os, const Fraction& rhs) {
    if (rhs.denominator_ == 1) {
      os << rhs.numerator_;
    } else {
      os << rhs.numerator_ << "/" << rhs.denominator_;
    }
    return os;
  }
  friend std::istream& operator>>(std::istream& is, Fraction& rhs) {
    int num = 0;
    int denom = 1;
    char slash = '\0';
    is >> num >> slash >> denom;
    if (is && (slash == '/') && (denom != 0)) {
      rhs.numerator_ = num;
      rhs.denominator_ = denom;
      rhs.reduce();
    } else {
      is.setstate(std::ios::failbit);
    }
    return is;
  }
};
// Comparison operators
bool operator>(const Fraction& lhs, const Fraction& rhs) { return rhs < lhs; }
bool operator<=(const Fraction& lhs, const Fraction& rhs) {
  return !(lhs > rhs);
}
bool operator>=(const Fraction& lhs, const Fraction& rhs) {
  return !(lhs < rhs);
}
bool operator==(const Fraction& lhs, const Fraction& rhs) {
  return !(lhs < rhs) && !(rhs < lhs);
}
bool operator!=(const Fraction& lhs, const Fraction& rhs) {
  return !(lhs == rhs);
}
// Binary operators
Fraction operator+(Fraction lhs, const Fraction& rhs) { return lhs += rhs; }
Fraction operator-(Fraction lhs, const Fraction& rhs) { return lhs -= rhs; }
Fraction operator*(Fraction lhs, const Fraction& rhs) { return lhs *= rhs; }
Fraction operator/(Fraction lhs, const Fraction& rhs) { return lhs /= rhs; }


int main() {
    // Create two fraction objects
    const Fraction a(1, 2);
    const Fraction b(1, 3);

    // Test fraction addition
    std::cout << "========== 分数运算测试 ==========" << std::endl;
    std::cout << "分数 a = " << a << std::endl;
    std::cout << "分数 b = " << b << std::endl;
    std::cout << "加法运算：" << a << " + " << b
              << " = " << (a + b) << std::endl;

    // Test the default constructor
    std::cout << "\n========== 默认构造测试 ==========" << std::endl;
    Fraction c{};
    std::cout << "分数 c 的初始值为：" << c << std::endl;

    // Test the input operator
    std::cout << "\n========== 分数输入测试 ==========" << std::endl;
    std::cout << "请输入一个分数（格式：分子/分母）：";

    if (std::cin >> c) {
        std::cout << "输入成功！" << std::endl;
        std::cout << "化简后的分数 c = " << c << std::endl;
    } else {
        std::cout << "输入失败！请输入正确的分数格式，且分母不能为 0。"
                  << std::endl;
    }

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

Output:

```text
========== 分数运算测试 ==========
分数 a = 1/2
分数 b = 1/3
加法运算：1/2 + 1/3 = 5/6

========== 默认构造测试 ==========
分数 c 的初始值为：0

========== 分数输入测试 ==========
请输入一个分数（格式：分子/分母）：2/4
输入成功！
化简后的分数 c = 1/2
```

> When the content consumed by `std::cin >> c` does not match the required format, `is.setstate(ios::failbit);` sets the stream's internal `failbit`—the "input/output operation failed (formatting or extraction error)" state bit—to 1, marking this formatted input or data extraction as failed. Since `operator>>` returns the stream object itself, and the stream object records these state bits and can convert itself to `bool` based on its current state, evaluating `std::cin >> c` in a condition yields `false`

:::

### Exercise 2: Implement `operator[]` for a `Matrix` Class

Design a simple `Matrix` class that stores its N x M elements in a one-dimensional array internally. Overload `operator[]` so that it returns a reference to the first element of a row—which means you need to define a helper `Row` proxy class. Build the basic version first, requiring only that reads through `matrix[i][j]` work correctly, then think about writes.

Hint: `matrix[i]` returns a `Row` object, and `Row::operator[]` in turn returns the reference to the actual element. This classic "proxy pattern" technique shows up again and again in C++.

::: details Reference Solution

```cpp
#include <iostream>
class Matrix {
 private:
  int rows_{};
  int cols_{};
  int* data_;

 public:
  class Row {
   private:
    int* data_;

   public:
    Row(int* data) : data_(data) {}
    int& operator[](int j) { return data_[j]; }
    const int& operator[](int j) const { return data_[j]; }
  };

  Matrix(int rows, int cols)
      : rows_(rows), cols_(cols), data_(new int[rows * cols]{}) {}
  ~Matrix() { delete[] data_; }

  Matrix(const Matrix& matrix) = delete;
  Matrix& operator=(const Matrix& matrix) = delete;

  Row operator[](int i) { return Row(data_ + i * cols_); }
  const Row operator[](int i) const { return Row(data_ + i * cols_); }
};
int main() {
  // Create a 2-row, 3-column matrix
  Matrix matrix(2, 3);

  // Assign values to the matrix
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 3; j++) {
      matrix[i][j] = i * 3 + j;
    }
  }

  // Print the matrix
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 3; j++) {
      std::cout << matrix[i][j] << " ";
    }
    std::cout << std::endl;
  }

  return 0;
}
```

Compile and run:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

Output:

```text
0 1 2 
3 4 5
```

:::
