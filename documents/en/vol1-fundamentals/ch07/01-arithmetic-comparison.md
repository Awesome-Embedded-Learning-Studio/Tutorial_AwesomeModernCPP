---
title: Arithmetic and comparison operators
description: Master the overloading methods for arithmetic and comparison operators
  to implement a complete Fraction class.
chapter: 7
order: 1
difficulty: intermediate
reading_time_minutes: 15
platform: host
prerequisites:
- this 指针与链式调用
tags:
- cpp-modern
- host
- intermediate
- 进阶
cpp_standard:
- 11
- 14
- 17
- 20
---
# Arithmetic and Comparison Operators

So far, we could only manipulate our custom types through member functions — to add two objects, we had to write `a.add(b)`; to check equality, we had to write `a.equals(b)`. Frankly, this style is fine for business logic, but once we deal with types that have "natural arithmetic semantics" like mathematical operations, physical quantities, or dates, a screen full of `.add()` and `.compare()` becomes really painful to read. We want our code to read like the math itself: `a + b`, `x == y`, `p1 < p2`.

Operator overloading is the capability C++ provides to let custom types directly use `+`, `-`, `==`, `<`, making the code natural to read and pleasant to write. In this chapter, we focus on arithmetic and comparison operators, walking through the entire process with a complete `Fraction` (fraction) class.

> **Warning**: Operator overloading is powerful, but never abuse it. Only overload an operator when its meaning is "obvious at a glance" — for example, `a + b` for addition or `a == b` for equality. If you plan to use `+` to mean "delete an element from a container," you're better off writing a `remove()` function. Otherwise, the person maintaining your code might give you a friendly call in the middle of the night (guaranteed).

## Why Overload Operators

Before diving into the implementation, let's clarify our motivation. There is only one core reason — readability. Suppose we have a 2D vector class. Putting the two styles side by side makes the difference obvious:

```cpp
// 函数调用风格
auto v3 = v1.add(v2);
auto v4 = v1.scale(2.0f);

// 运算符重载风格
auto v3 = v1 + v2;
auto v4 = v1 * 2.0f;
```

The second style looks almost identical to a mathematical formula. When reading the code, we don't need to do extra "translation" in our heads. The gap becomes even more obvious with complex expressions — `a + b * c - d / e` versus `a.add(b.scale(c)).subtract(d.divide(e))`. The former is clear at a glance, while the latter is easy to get lost in.

However, operator overloading is a feature that requires restraint. We follow one guideline: **only overload an operator when it feels "natural" for that operation**. Using `+` for vector addition is natural, and using `<` for date comparison is natural. But if you overload `<<` on a logger class to "send logs to a remote server," the semantics have gone off the rails.

## Member or Non-Member — A Far-Reaching Choice

Operators can be overloaded in two ways: **member functions** and **non-member functions**. This choice affects not only syntax but also the behavior of implicit type conversions.

For a member function, the left-hand operand **must** be an object of the current class. If you implement `operator+` as a member function, `Fraction(1, 2) + 3` works (`3` can be implicitly converted to `Fraction` via the constructor), but `3 + Fraction(1, 2)` does not — the compiler won't look for `operator+` on `int`. Non-member functions don't have this limitation; the left and right operands are symmetric, and the compiler attempts implicit conversions on both sides, so both `3 + f` and `f + 3` work correctly. Assignment operators (`=`, `+=`, `-=`, `[]`, `()`, etc.), on the other hand, must be member functions — the language mandates that certain operators can only be overloaded as members, and since the left-hand side of an assignment is the object being modified, placing it in a member function is the most natural semantic choice.

This leads to a widely adopted implementation pattern: first implement compound assignment operators (like `+=`) as member functions, then implement binary operators (like `+`) as non-member functions based on them. The binary operator logic fully reuses the compound assignment code without duplicating the addition details, and the non-member position guarantees symmetry of the left and right operands. We will strictly follow this pattern in our `Fraction` class.

## Building Arithmetic Operations Starting from `operator+=`

Enough theory — let's get to work. We start the `Fraction` class with the compound assignment operators:

```cpp
class Fraction {
private:
    int numerator_;   // 分子
    int denominator_; // 分母

public:
    Fraction(int num = 0, int den = 1)
        : numerator_(num), denominator_(den)
    {
        if (denominator_ == 0) {
            denominator_ = 1;
        }
        normalize();
    }

    // 复合赋值：就地修改，返回 *this 的引用
    Fraction& operator+=(const Fraction& rhs)
    {
        // a/b + c/d = (a*d + c*b) / (b*d)
        numerator_ = numerator_ * rhs.denominator_
                     + rhs.numerator_ * denominator_;
        denominator_ *= rhs.denominator_;
        normalize();
        return *this;
    }

    int num() const { return numerator_; }
    int den() const { return denominator_; }

private:
    void normalize()
    {
        int g = gcd(numerator_, denominator_);
        numerator_ /= g;
        denominator_ /= g;
        if (denominator_ < 0) {
            numerator_ = -numerator_;
            denominator_ = -denominator_;
        }
    }

    static int gcd(int a, int b)
    {
        a = (a < 0) ? -a : a;
        b = (b < 0) ? -b : b;
        while (b != 0) { int t = b; b = a % b; a = t; }
        return (a == 0) ? 1 : a;
    }
};
```

There are two key points here. First, the return type of `operator+=` is `Fraction&`, and it returns a reference to `*this` — this is the foundation for chaining, allowing `a += b += c` to work correctly. Second, we reduce the fraction after every operation (`normalize()`) to ensure it is always in simplest form with a positive denominator. This is an internal invariant of the fraction class; maintaining it properly makes subsequent comparison operations simpler — two reduced fractions are equal if and only if their numerators and denominators are identical, without needing to find a common denominator.

> **Warning**: `operator+=` must return a reference to `*this` (`Fraction&`), not a value. If you write `Fraction operator+=(...)`, even though it compiles, `a += b` returns a temporary object rather than `a` itself, so a chained assignment like `(a += b) = c` won't modify `a` — this is completely inconsistent with the behavior of built-in types. `operator-=`, `operator*=`, and `operator/=` must all follow the same rule.

Once we have `+=`, the implementation of `+` is very concise:

```cpp
// 非成员函数：通过 += 来实现 +
Fraction operator+(Fraction lhs, const Fraction& rhs)
{
    lhs += rhs;  // 复用 operator+=
    return lhs;  // 返回修改后的副本
}
```

Note that `lhs` is **passed by value**. It is already a copy of the caller's argument, so calling `+=` directly on `lhs` modifies this copy rather than the original object. When the function ends, returning this copy is exactly the addition result. This reuses the logic of `+=` while avoiding the creation of extra temporary objects.

> **Warning**: Binary arithmetic operators (`+`, `-`, `*`, `/`) must return a **new object (by value)**, not a reference. The result of `a + b` is a new value that has no relation to `a` or `b` — if you return a reference to a local variable, that's a classic dangling reference, which will likely yield garbage values or crash when used.

The remaining operators follow the exact same pattern. Let's fill in `*=` and `/=`:

```cpp
Fraction& operator*=(const Fraction& rhs)
{
    numerator_ *= rhs.numerator_;
    denominator_ *= rhs.denominator_;
    normalize();
    return *this;
}

Fraction& operator/=(const Fraction& rhs)
{
    // 除以一个分数等于乘以它的倒数
    numerator_ *= rhs.denominator_;
    denominator_ *= rhs.numerator_;
    if (denominator_ == 0) { denominator_ = 1; }
    normalize();
    return *this;
}
```

Then we derive the binary operations: `Fraction operator-(Fraction lhs, const Fraction& rhs)` internally calls `lhs -= rhs; return lhs;`, and multiplication and division follow the same logic, so we won't repeat them here.

## Comparison Operators — From `==` to the Full Set of Six

Because we already ensured in `normalize()` that fractions are always in simplest form, equality comparison is very simple — identical numerators and denominators mean equality:

```cpp
bool operator==(const Fraction& lhs, const Fraction& rhs)
{
    return lhs.num() == rhs.num() && lhs.den() == rhs.den();
}

// 关键：!= 始终基于 == 来实现
bool operator!=(const Fraction& lhs, const Fraction& rhs)
{
    return !(lhs == rhs);
}
```

> **Warning**: `operator!=` **must** be implemented based on `operator==`, written as `!(lhs == rhs)`, rather than writing a separate comparison logic. If you implement `==` and `!=` independently, sooner or later you'll modify one and forget to sync the other, causing `a == b` and `!(a != b)` to give contradictory results. This isn't just a logic bug — it will also break containers and algorithms that rely on comparison operations (like `std::set` and `std::find`).

Relational comparisons follow the same idea. Mathematically, `a/b < c/d` is equivalent to `a*d < c*b` (assuming denominators are positive, which `normalize()` already guarantees). Then `>`, `<=`, and `>=` are all derived based on `<`:

```cpp
bool operator<(const Fraction& lhs, const Fraction& rhs)
{
    return lhs.num() * rhs.den() < rhs.num() * lhs.den();
}
bool operator>(const Fraction& lhs, const Fraction& rhs)  { return rhs < lhs; }
bool operator<=(const Fraction& lhs, const Fraction& rhs) { return !(rhs < lhs); }
bool operator>=(const Fraction& lhs, const Fraction& rhs) { return !(lhs < rhs); }
```

We only actually wrote the logic for `<`; the other three are all implemented based on `<` — this is the same principle as `!=` being based on `==`: a single source of truth, meaning we only need to change one place when modifying.

## Symmetry and Implicit Conversion — Making `3 + f` Work

We've been saying "non-member functions guarantee symmetry," so now let's look at the concrete effect. The constructor of `Fraction` has two `int` parameters, both with default values, so `Fraction f = 3;` creates a `Fraction(3, 1)`. When `operator+` is a non-member function, the compiler, upon encountering `3 + Fraction(1, 2)`, will try to implicitly convert `3` to `Fraction(3, 1)` and then call `operator+` — everything works fine. But if `operator+` is a member function, `3.operator+(Fraction(1,2))` is completely invalid — `int` has no `operator+` that accepts a `Fraction` parameter.

Because we exposed data access through `num()` and `den()`, the non-member functions work without needing `friend`. If your class doesn't conveniently expose getters, you can use a `friend` function to access private members.

> **Warning**: If you decide to add `explicit` to the constructor to prohibit implicit conversions (which is a good practice in itself), `3 + Fraction(1, 2)` will fail to compile. You'll need to provide additional overloads accepting `int`: `Fraction operator+(int lhs, const Fraction& rhs)`. For mathematical types, omitting `explicit` is a common trade-off — sacrificing a bit of safety for more natural expressions.

## In Practice: The Complete fraction.cpp

Now let's assemble all the pieces:

```cpp
// fraction.cpp
#include <iostream>

class Fraction {
private:
    int numerator_;
    int denominator_;

public:
    Fraction(int num = 0, int den = 1)
        : numerator_(num), denominator_(den)
    {
        if (denominator_ == 0) { denominator_ = 1; }
        normalize();
    }

    Fraction& operator+=(const Fraction& rhs)
    {
        numerator_ = numerator_ * rhs.denominator_
                     + rhs.numerator_ * denominator_;
        denominator_ *= rhs.denominator_;
        normalize();
        return *this;
    }

    Fraction& operator-=(const Fraction& rhs)
    {
        numerator_ = numerator_ * rhs.denominator_
                     - rhs.numerator_ * denominator_;
        denominator_ *= rhs.denominator_;
        normalize();
        return *this;
    }

    Fraction& operator*=(const Fraction& rhs)
    {
        numerator_ *= rhs.numerator_;
        denominator_ *= rhs.denominator_;
        normalize();
        return *this;
    }

    Fraction& operator/=(const Fraction& rhs)
    {
        numerator_ *= rhs.denominator_;
        denominator_ *= rhs.numerator_;
        if (denominator_ == 0) { denominator_ = 1; }
        normalize();
        return *this;
    }

    int num() const { return numerator_; }
    int den() const { return denominator_; }

    Fraction operator-() const { return Fraction(-numerator_, denominator_); }

private:
    void normalize()
    {
        int g = gcd(numerator_, denominator_);
        numerator_ /= g;
        denominator_ /= g;
        if (denominator_ < 0) {
            numerator_ = -numerator_;
            denominator_ = -denominator_;
        }
    }

    static int gcd(int a, int b)
    {
        a = (a < 0) ? -a : a;
        b = (b < 0) ? -b : b;
        while (b != 0) { int t = b; b = a % b; a = t; }
        return (a == 0) ? 1 : a;
    }
};

// 二元算术（非成员）
Fraction operator+(Fraction lhs, const Fraction& rhs) { lhs += rhs; return lhs; }
Fraction operator-(Fraction lhs, const Fraction& rhs) { lhs -= rhs; return lhs; }
Fraction operator*(Fraction lhs, const Fraction& rhs) { lhs *= rhs; return lhs; }
Fraction operator/(Fraction lhs, const Fraction& rhs) { lhs /= rhs; return lhs; }

// 比较（非成员）
bool operator==(const Fraction& l, const Fraction& r)
{ return l.num() == r.num() && l.den() == r.den(); }
bool operator!=(const Fraction& l, const Fraction& r) { return !(l == r); }
bool operator<(const Fraction& l, const Fraction& r)
{ return l.num() * r.den() < r.num() * l.den(); }
bool operator>(const Fraction& l, const Fraction& r)  { return r < l; }
bool operator<=(const Fraction& l, const Fraction& r) { return !(r < l); }
bool operator>=(const Fraction& l, const Fraction& r) { return !(l < r); }

std::ostream& operator<<(std::ostream& os, const Fraction& f)
{ os << f.num() << "/" << f.den(); return os; }

int main()
{
    Fraction a(1, 2), b(1, 3);

    std::cout << a << " + " << b << " = " << (a + b) << std::endl;
    std::cout << a << " - " << b << " = " << (a - b) << std::endl;
    std::cout << a << " * " << b << " = " << (a * b) << std::endl;
    std::cout << a << " / " << b << " = " << (a / b) << std::endl;

    // 与整数的混合运算（隐式转换）
    std::cout << a << " + 1 = " << (a + 1) << std::endl;
    std::cout << "2 * " << b << " = " << (2 * b) << std::endl;

    a += b;
    std::cout << "a += b -> a = " << a << std::endl;

    Fraction c(1, 6), d(1, 4);
    std::cout << c << " == " << d << " : " << (c == d) << std::endl;
    std::cout << c << " < " << d << " : " << (c < d) << std::endl;
    std::cout << c << " >= " << d << " : " << (c >= d) << std::endl;

    Fraction e(3, 4);
    std::cout << "-" << e << " = " << (-e) << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -Wall -Wextra -std=c++17 fraction.cpp -o fraction && ./fraction
```

Verify the output:

```text
1/2 + 1/3 = 5/6
1/2 - 1/3 = 1/6
1/2 * 1/3 = 1/6
1/2 / 1/3 = 3/2
1/2 + 1 = 3/2
2 * 1/3 = 2/3
a += b -> a = 5/6
1/6 == 1/4 : 0
1/6 < 1/4 : 1
1/6 >= 1/4 : 0
-3/4 = -3/4
```

All operation results are correct. `a + b` yields `5/6` (after finding a common denominator, `3/6 + 2/6`), division `1/2 / 1/3` yields `3/2`, and mixed operations like `2 * 1/3` also work normally — `2` is implicitly converted to `Fraction(2, 1)` and participates in the multiplication. Reduction is automatically performed after every arithmetic step, thanks to `normalize()`.

## The Dawn of C++20 — The Three-Way Comparison Operator `<=>`

Before wrapping up, we have to mention the three-way comparison operator (spaceship operator) `<=>` introduced in C++20. If the compiler supports C++20, we only need to implement one `operator<=>`, and the compiler can automatically generate all six comparison operators:

```cpp
// C++20：一行搞定所有比较
auto operator<=>(const Fraction&, const Fraction&) = default;
```

If the class's member variables themselves support three-way comparison (`int` certainly does), simply writing `= default` does the job. This saves the effort of hand-writing six comparison functions and completely eliminates bugs like "modifying `<` but forgetting to update `<=`." However, since our tutorial currently uses C++17 as the baseline, hand-writing comparison operators remains an essential skill to master.

## Exercises

**Exercise 1: Complete Subtraction and Division for Fraction**

The complete code above already provides the implementations for `operator-=` and `operator/=`, but if you've been following along step by step, try to implement these two operators independently without looking at the answer, then check your code against the solution. Pay special attention to handling zero denominators in division.

**Exercise 2: Implement Comparison Operators for a Date Class**

Create a `Date` class with three fields: `year`, `month`, and `day`. Implement all six comparison operators. Hint: you can first implement `operator<` (comparing year, month, and day in order), then derive the other five from it. Think about this: if two `Date` objects have different years but the same month, how should the comparison logic be written?

## Summary

In this chapter, we walked the complete path from theory to implementation, focusing on the core practices of operator overloading. Compound assignment operators (`+=`, `-=`, `*=`, `/=`) are implemented as member functions, modifying the object in place and returning a reference to `*this`. Binary arithmetic operators (`+`, `-`, `*`, `/`) are implemented as non-member functions, passing the left-hand operand by value, reusing the compound assignment implementation, and returning a new object by value. For comparison operators, `!=` is implemented based on `==`, and `>`, `<=`, and `>=` are derived based on `<`, ensuring a single source of truth. Non-member functions guarantee symmetry of the left and right operands, allowing both `3 + f` and `f + 3` to work correctly.

In the next chapter, we continue our operator overloading journey, looking at how to overload stream operators (`<<`, `>>`) and the subscript operator (`[]`) — the former lets custom types interact with `std::cout`, and the latter is the standard interface for custom containers.
