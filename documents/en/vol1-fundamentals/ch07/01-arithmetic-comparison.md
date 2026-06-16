---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the methods for overloading arithmetic and comparison operators,
  and implement a complete Fraction class.
difficulty: intermediate
order: 1
platform: host
prerequisites:
- this 指针与链式调用
reading_time_minutes: 13
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Arithmetic and Comparison Operators
translation:
  source: documents/vol1-fundamentals/ch07/01-arithmetic-comparison.md
  source_hash: 4bc10e7c2d5bbb988cbc2ab8b73a5ef43946f802a721d7d7dd7a20e6d753fbc4
  translated_at: '2026-06-16T03:45:19.949459+00:00'
  engine: anthropic
  token_count: 2927
---
# Arithmetic and Comparison Operators

So far, our custom types could only be manipulated via member functions—to add two objects, we had to write `a.add(b)`; to check for equality, we had to write `a.equals(b)`. Honestly, this style is passable for general business logic, but once we deal with types that have "natural operational semantics"—like mathematical quantities, physical units, or dates—screens full of `.add()` and `.equals()` become painful. We prefer code that reads like the math expression itself: `a + b`, `a == b`, `a * 2`.

Operator overloading is the capability C++ provides us—allowing custom types to directly use operators like `+`, `==`, `*`, and `<`. This makes code natural to read and comfortable to write. In this chapter, we focus on arithmetic and comparison operators, walking through the entire process using a complete `Fraction` class.

> **Warning**: Operator overloading is powerful, but do not abuse it. Only overload when the meaning is "immediately obvious"—for example, `+` for addition or `==` for equality. If you intend to use `operator-` to "delete an element from a container," you are better off writing a plain `remove()` function. Otherwise, the maintainer of your code might call you in the middle of the night for a "friendly" chat (for sure).

## Why Overload Operators

Before we start implementing, let's clarify our motivation. There is only one core reason—readability. Suppose we have a 2D vector class. Comparing two styles makes this obvious:

```cpp
// Style 1: Member functions
Vec3 result = v1.add(v2).scale(5.0);

// Style 2: Operator overloading
Vec3 result = (v1 + v2) * 5.0;
```

The second style looks almost identical to the mathematical formula. When reading the code, no extra mental "translation" is needed. The difference is even more pronounced with complex expressions—`a + b * c - d` versus `a.add(b.multiply(c)).subtract(d)`. The former is clear at a glance, while the latter is easy to get lost in.

However, operator overloading is a feature that requires restraint. I have one guideline: **Only overload an operator when it feels "natural" for that type.** Using `+` for vector addition is natural; using `<` for date comparison is natural. But if you overload `operator<<` for a logger class to "send logs to a remote server," the semantics have gone astray.

## Member vs. Non-Member—A Choice with Far-Reaching Impact

Operators can be overloaded in two ways: **member functions** and **non-member functions**. This choice affects not only syntax but also type conversion behavior.

For a member function, the left-hand operand **must** be an object of the current class. If you implement `operator+` as a member function, then `fraction + 1` works (because `1` can be implicitly converted to `Fraction` via the constructor), but `1 + fraction` will not work—the compiler won't look for `operator+` in `int`. Non-member functions don't have this limitation; the left and right operands are symmetric, and the compiler attempts implicit conversions on both sides, so both `fraction + 1` and `1 + fraction` work correctly. Assignment-like operators (`=`, `+=`, `-=`, `*=`, `/=`, etc.) must be member functions—the language dictates that some operators can only be overloaded as members, and the left-hand side of an assignment is the object being modified, which fits naturally in a member function.

This leads to a widely adopted implementation pattern: first implement compound assignment operators (like `operator+=`) as member functions, then implement binary operators (like `operator+`) as non-member functions based on them. The logic of the binary operator completely reuses the compound assignment code, avoiding repetition of addition details, and the non-member position ensures symmetry of operands. We will strictly follow this pattern in our `Fraction` class.

## Building Arithmetic Operations Starting with `operator+=

Enough theory; let's get our hands dirty. We'll start the `Fraction` class with the compound assignment operators:

```cpp
class Fraction {
    // ... constructors and private members ...

public:
    // Compound assignment: addition
    Fraction& operator+=(const Fraction& other) {
        numerator = numerator * other.denominator + other.numerator * denominator;
        denominator *= other.denominator;
        normalize(); // Simplify and ensure denominator is positive
        return *this;
    }

    // Compound assignment: multiplication
    Fraction& operator*=(const Fraction& other) {
        numerator *= other.numerator;
        denominator *= other.denominator;
        normalize();
        return *this;
    }

    // ... getters for numerator/denominator ...
};
```

There are two key points here. First, the return type of `operator+=` is `Fraction&`, returning a reference to `*this`—this is the foundation for chaining calls, allowing `a += b += c` to work correctly. Second, we simplify (normalize) after every operation to ensure the fraction is always in simplest form with a positive denominator. This is an internal invariant of the `Fraction` class; maintaining it makes subsequent comparison operations simpler—two normalized fractions are equal if and only if their numerators and denominators are identical, no need for extra common denominator calculation.

> **Warning**: `operator+=` **must** return a reference to `*this` (`Fraction&`), not by value. If you write `Fraction operator+=`, although it compiles, the return value is a temporary object rather than `*this` itself. Chained assignments like `(a += b) = c` won't modify `a`—this is inconsistent with the behavior of built-in types. `-=`, `*=`, and `/=` must follow the same rule.

With `operator+=` in place, implementing `operator+` is very concise:

```cpp
// Binary addition operator (non-member)
Fraction operator+(Fraction lhs, const Fraction& rhs) {
    lhs += rhs; // Reuse the compound assignment logic
    return lhs; // Return the modified copy
}
```

Note that `lhs` is passed **by value**. It is a copy of the caller's argument, so calling `lhs += rhs` modifies this copy rather than the original object. When the function returns this copy, it is exactly the result of the addition. This reuses the logic of `operator+=` and avoids creating extra temporary objects.

> **Warning**: Binary arithmetic operators (`+`, `-`, `*`, `/`) must return a **new object (by value)**, not a reference. The result of `a + b` is a new value; it has no relation to `a` or `b`. If you return a reference to a local variable, you get a dangling reference, which likely leads to garbage values or crashes.

The remaining operators follow the exact same pattern. First, fill in `operator-=` and `operator*=`:

```cpp
Fraction& operator-=(const Fraction& other) {
    numerator = numerator * other.denominator - other.numerator * denominator;
    denominator *= other.denominator;
    normalize();
    return *this;
}

Fraction& operator*=(const Fraction& other) {
    numerator *= other.numerator;
    denominator *= other.denominator;
    normalize();
    return *this;
}
```

Then derive the binary operations from them: `operator-` calls `operator-=` internally, and multiplication/division follow the same logic, so we won't belabor the point.

## Comparison Operators—From `operator==` to the Full Set of Six

Because we ensured in `normalize()` that fractions are always in simplest form, equality comparison is very simple—equal numerators and denominators mean equality:

```cpp
bool operator==(const Fraction& lhs, const Fraction& rhs) {
    return lhs.get_numerator() == rhs.get_numerator() &&
           lhs.get_denominator() == rhs.get_denominator();
}
```

> **Warning**: `operator!=` **must** be implemented based on `operator==`, written as `!(lhs == rhs)`, rather than rewriting comparison logic yourself. If you implement `operator==` and `operator!=` independently, sooner or later you will modify one and forget to sync the other, leading to contradictory results from `==` and `!=`. This is not just a logical bug; it also breaks containers and algorithms that rely on comparisons (like `std::set`, `std::sort`).

Relational comparisons follow the same idea. Mathematically, `a/b < c/d` is equivalent to `a*d < c*b` (assuming denominators are positive, which `normalize()` guarantees). Then `>`, `<=`, `>=` are all derived based on `<`:

```cpp
bool operator<(const Fraction& lhs, const Fraction& rhs) {
    // Compare cross-products to avoid floating point issues
    return lhs.get_numerator() * rhs.get_denominator() <
           rhs.get_numerator() * lhs.get_denominator();
}

bool operator>(const Fraction& lhs, const Fraction& rhs) {
    return rhs < lhs;
}

bool operator<=(const Fraction& lhs, const Fraction& rhs) {
    return !(lhs > rhs);
}

bool operator>=(const Fraction& lhs, const Fraction& rhs) {
    return !(lhs < rhs);
}
```

We only actually wrote the logic for `operator<`; the other three are implemented based on it. This is the same principle as `operator+` based on `operator+=`: a single source of truth, meaning only one place needs modification during changes.

## Symmetry and Implicit Conversion—Making `1 + fraction` Work

We've been talking about "non-member functions ensuring symmetry." Now let's look at the concrete effect. The `Fraction` constructor has two `int` parameters with default values, so `Fraction(1)` creates `1/1`. When `operator+` is a non-member function, the compiler attempts to implicitly convert `1` to `Fraction` when it sees `1 + fraction`, then calls `operator+`. Everything works. However, if `operator+` is a member function, `1 + fraction` is completely illegal—`int` certainly doesn't have an `operator+` that accepts a `Fraction` parameter.

Because we exposed data access via getters, non-member functions work without needing `friend`. If your class doesn't want to expose getters, use `friend` functions to access private members.

> **Warning**: If you decide to add `explicit` to the constructor to prohibit implicit conversion (which is generally a good habit), `1 + fraction` will fail to compile. You need to provide an overload accepting `int`: `Fraction operator+(Fraction, int);`. For mathematical types, omitting `explicit` is a common trade-off—sacrificing a little safety for more natural expressions.

## In Practice: Complete fraction.cpp

Now let's assemble all the parts:

```cpp
#include <iostream>
#include <numeric> // for std::gcd

class Fraction {
    int numerator;
    int denominator;

    // Ensure denominator > 0 and fraction is reduced
    void normalize() {
        if (denominator < 0) {
            numerator = -numerator;
            denominator = -denominator;
        }
        int common = std::gcd(std::abs(numerator), denominator);
        if (common > 0) {
            numerator /= common;
            denominator /= common;
        }
    }

public:
    Fraction(int n = 0, int d = 1) : numerator(n), denominator(d) {
        if (d == 0) throw std::invalid_argument("Denominator cannot be zero");
        normalize();
    }

    // Getters
    int get_numerator() const { return numerator; }
    int get_denominator() const { return denominator; }

    // Compound assignment operators
    Fraction& operator+=(const Fraction& other) {
        numerator = numerator * other.denominator + other.numerator * denominator;
        denominator *= other.denominator;
        normalize();
        return *this;
    }

    Fraction& operator-=(const Fraction& other) {
        numerator = numerator * other.denominator - other.numerator * denominator;
        denominator *= other.denominator;
        normalize();
        return *this;
    }

    Fraction& operator*=(const Fraction& other) {
        numerator *= other.numerator;
        denominator *= other.denominator;
        normalize();
        return *this;
    }

    Fraction& operator/=(const Fraction& other) {
        if (other.numerator == 0) throw std::runtime_error("Division by zero");
        numerator *= other.denominator;
        denominator *= other.numerator;
        normalize();
        return *this;
    }

    // Binary arithmetic operators (non-members)
    friend Fraction operator+(Fraction lhs, const Fraction& rhs) {
        lhs += rhs;
        return lhs;
    }

    friend Fraction operator-(Fraction lhs, const Fraction& rhs) {
        lhs -= rhs;
        return lhs;
    }

    friend Fraction operator*(Fraction lhs, const Fraction& rhs) {
        lhs *= rhs;
        return lhs;
    }

    friend Fraction operator/(Fraction lhs, const Fraction& rhs) {
        lhs /= rhs;
        return lhs;
    }

    // Comparison operators (non-members)
    friend bool operator==(const Fraction& lhs, const Fraction& rhs) {
        return lhs.numerator == rhs.numerator && lhs.denominator == rhs.denominator;
    }

    friend bool operator!=(const Fraction& lhs, const Fraction& rhs) {
        return !(lhs == rhs);
    }

    friend bool operator<(const Fraction& lhs, const Fraction& rhs) {
        return lhs.numerator * rhs.denominator < rhs.numerator * lhs.denominator;
    }

    friend bool operator>(const Fraction& lhs, const Fraction& rhs) {
        return rhs < lhs;
    }

    friend bool operator<=(const Fraction& lhs, const Fraction& rhs) {
        return !(lhs > rhs);
    }

    friend bool operator>=(const Fraction& lhs, const Fraction& rhs) {
        return !(lhs < rhs);
    }

    friend std::ostream& operator<<(std::ostream& os, const Fraction& f) {
        os << f.numerator << "/" << f.denominator;
        return os;
    }
};

int main() {
    Fraction f1(1, 2);
    Fraction f2(1, 3);

    std::cout << "f1 = " << f1 << ", f2 = " << f2 << "\n";

    std::cout << "f1 + f2 = " << (f1 + f2) << "\n"; // 5/6
    std::cout << "f1 - f2 = " << (f1 - f2) << "\n"; // 1/6
    std::cout << "f1 * f2 = " << (f1 * f2) << "\n"; // 1/6
    std::cout << "f1 / f2 = " << (f1 / f2) << "\n"; // 3/2

    std::cout << "f1 + 1 = " << (f1 + 1) << "\n";   // 3/2
    std::cout << "1 + f1 = " << (1 + f1) << "\n";   // 3/2

    std::cout << "f1 > f2 ? " << (f1 > f2) << "\n";  // true (1)
    std::cout << "f1 == f2 ? " << (f1 == f2) << "\n"; // false (0)

    // Chaining
    Fraction f3 = f1 + f2 + Fraction(1, 6);
    std::cout << "f1 + f2 + 1/6 = " << f3 << "\n"; // 1/1

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 fraction.cpp -o fraction && ./fraction
```

Verify output:

```text
f1 = 1/2, f2 = 1/3
f1 + f2 = 5/6
f1 - f2 = 1/6
f1 * f2 = 1/6
f1 / f2 = 3/2
f1 + 1 = 3/2
1 + f1 = 3/2
f1 > f2 ? 1
f1 == f2 ? 0
f1 + f2 + 1/6 = 1/1
```

All operation results are correct. `1/2 + 1/3` yields `5/6` (common denominator `6/6`), division `1/2 / 1/3` yields `3/2`, and mixed operations like `1 + f1` work normally—`1` is implicitly converted to `Fraction` and participates in multiplication. Simplification happens automatically at every step, thanks to `normalize()`.

## The Dawn of C++20—The Three-Way Comparison Operator `operator<=>

Before finishing, we must mention the three-way comparison operator (spaceship operator) `operator<=>` introduced in C++20. If the compiler supports C++20, you only need to implement one `operator<=>`, and the compiler can automatically generate all six comparison operators:

```cpp
// C++20 auto operator<=>(const Fraction&) const = default;
```

If the class's member variables themselves support three-way comparison (which `int` does), simply using `= default` does the job. This saves the effort of writing six comparison functions by hand and completely eliminates bugs like "modified `==` but forgot to update `<`". However, since our tutorial uses C++17 as the baseline, hand-writing comparison operators is still an essential skill to master.

## Run Online

Run the Fraction class online to observe the effects of operator overloading:

<OnlineCompilerDemo
  title="Operator Overloading: Fraction Class"
  source-path="code/examples/vol1/13_fraction_operators.cpp"
  description="Run online and observe the overloading behavior of arithmetic and comparison operators. Try modifying the fraction values."
  allow-run
/>

## Exercises

**Exercise 1: Complete Subtraction and Division for Fraction**

The full code above provides implementations for `operator-=` and `operator/=`, but if you followed the tutorial step-by-step, try to complete these two operators independently without looking at the answer, then check your code against the solution. Pay attention to handling division by zero.

**Exercise 2: Implement Comparison Operators for a Date Class**

Create a `Date` class containing `year`, `month`, and `day` fields, and implement all six comparison operators. Hint: You can implement `operator<` first (compare year, then month, then day), then derive the other five based on it. Think about this: If two `Date` objects have different years but the same month, how should the comparison logic be written?

## Summary

In this chapter, we focused on the core practices of operator overloading, covering the complete path from theory to implementation. Compound assignment operators (`+=`, `-=`, `*=`, `/=`) are implemented as member functions, modifying the object in place and returning a reference to `*this`. Binary arithmetic operators (`+`, `-`, `*`, `/`) are implemented as non-member functions, passing the left operand by value, reusing compound assignment logic, and returning the new object by value. For comparison operators, `operator!=` is based on `operator==`, and `>`, `<=`, `>=` are based on `operator<`, ensuring a single source of truth. Non-member functions ensure symmetry of operands, allowing both `fraction + 1` and `1 + fraction` to work correctly.

In the next chapter, we continue our journey into operator overloading by looking at stream operators (`<<`, `>>`) and the subscript operator (`[]`)—the former allows custom types to work with `iostream`, and the latter is a standard interface for custom containers.
