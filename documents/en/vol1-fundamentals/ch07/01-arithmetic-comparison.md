---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: Master how to overload arithmetic and comparison operators and build a complete Fraction class.
difficulty: intermediate
order: 1
platform: host
prerequisites:
- The this Pointer and Method Chaining
reading_time_minutes: 18
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Arithmetic and Comparison Operators
translation:
  source: documents/vol1-fundamentals/ch07/01-arithmetic-comparison.md
  source_hash: e1ac6f393921dfcae76ab695f921c65d53d157ea9e711acc08db3817ed87884c
  translated_at: '2026-09-25T11:16:11+00:00'
  engine: anthropic
  token_count: 4400
---
# Arithmetic and Comparison Operator Overloading: Goodbye to a Screen Full of .add() Calls

So far, our custom types could only be manipulated through member functions—to add two objects you had to write `a.add(b)`, and to check equality you had to write `a.equals(b)`. That style is harmless enough in business logic, but the moment you deal with types that naturally carry operator semantics—math values, physical quantities, dates—a screen full of `.add()` and `.compare()` calls becomes genuinely painful. We would much rather have code that reads like the math expression itself: `a + b`, `x == y`, `p1 < p2`.

Operator overloading is exactly the capability C++ gives us here: custom types get to use operators like `+`, `-`, `==`, and `<` directly, so the code reads naturally and feels comfortable to write. This chapter focuses on the arithmetic and comparison operators, and we will walk the whole process through with a complete `Fraction` class.

Operator overloading is great, but never abuse it. An operator is worth overloading only when its meaning is obvious at a glance—`a + b` means addition, `a == b` means equality. If we plan to use `+` to mean "remove an element from the container", we would honestly be better off writing a plain `remove()` function; otherwise whoever inherits our code might just call us at midnight for a friendly chat (confirmed)

## Why Overload Operators

Before we start implementing, let's get the motivation straight. There is really only one core reason: readability. Suppose we have a 2D vector class—putting the two styles side by side makes the difference obvious:

```cpp
// Function-call style
auto v3 = v1.add(v2);
auto v4 = v1.scale(2.0f);

// Operator-overload style
auto v3 = v1 + v2;
auto v4 = v1 * 2.0f;
```

The second style is practically identical to the math formula, so reading it requires no extra mental "translation" step. The gap widens further with complex expressions: `a + b * c - d / e` versus `a.add(b.scale(c)).subtract(d.divide(e))`—the former is clear at a glance, while in the latter you lose your way a few tokens in.

Still, operator overloading is a feature that calls for restraint. I go by a single rule: **overload an operator only when it naturally, obviously should be used that way**. `+` for vector addition is natural; `<` for comparing dates is natural; but overloading `<<` on a logger class to mean "ship logs to a remote server"—at that point the semantics have already jumped the tracks.

## Member or Non-Member—A Choice with Far-Reaching Consequences

We can overload operators in two ways: **member functions** and **non-member functions**. This choice affects the syntax, and it also directly affects how type conversion behaves.

With a member function, the left operand **must** be an object of the current class. If we write `operator+` as a member function, then `Fraction(1, 2) + 3` works (`3` gets implicitly converted to `Fraction` via the constructor), but `3 + Fraction(1, 2)` does not: the compiler will not go hunting for an `operator+` on `int`. Non-member functions have no such restriction—the two operands are symmetric, the compiler attempts implicit conversion on both sides, so both `3 + f` and `f + 3` work fine. Assignment-style operators (`=`, `+=`, `-=`, `[]`, `()`, and friends), on the other hand, must be member functions: the language mandates that some of these operators can only be overloaded as members, and the left side of an assignment is the very object being modified anyway, so a member function is where they sit most naturally semantically.

This yields a widely adopted implementation pattern: implement the compound-assignment operators first (say `+=`) as member functions, then build the binary operators (say `+`) on top of them as non-member functions. The binary operation fully reuses the compound-assignment code—no duplicated addition details—and the non-member placement guarantees left/right operand symmetry. That is exactly the pattern our `Fraction` class will follow to the letter.

## Building Arithmetic from `operator+=`

Enough theory—time to get our hands dirty. The `Fraction` class starts with the compound-assignment operators:

```cpp
class Fraction {
private:
    int numerator_;   // Numerator
    int denominator_; // Denominator

public:
    Fraction(int num = 0, int den = 1)
        : numerator_(num), denominator_(den)
    {
        if (denominator_ == 0) {
            denominator_ = 1;
        }
        normalize();
    }

    // Compound assignment: modifies in place, returns a reference to *this
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

Let's pull out the key points from this code. First: the return type of `operator+=` is `Fraction&`, returning a reference to `*this`—that is the foundation of chaining, and it is what makes `a += b += c` work correctly. Second: after every operation we reduce the fraction (`normalize()`), guaranteeing it always stays in lowest terms with a positive denominator. That is the class's internal invariant; keeping it maintained makes the later comparison operators much simpler: two reduced fractions are equal if and only if their numerators and denominators are exactly the same, with no extra common-denominator juggling needed.

`operator+=` must return a reference to `*this` (`Fraction&`), not return by value. If we wrote `Fraction operator+=(...)`, it would compile, but `a += b` would return a temporary object rather than `a` itself; the chained assignment `(a += b) = c` would then fail to modify `a`—completely inconsistent with how built-in types behave. `operator-=`, `operator*=`, and `operator/=` all follow the same rule.

With `+=` in hand, implementing `+` becomes trivial:

```cpp
// Non-member function: implement + in terms of +=
Fraction operator+(Fraction lhs, const Fraction& rhs)
{
    lhs += rhs;  // Reuse operator+=
    return lhs;  // Return the modified copy
}
```

Note that `lhs` is **passed by value**—it is already a copy of the caller's argument—so calling `+=` directly on `lhs` modifies that copy, not the original object. Returning this copy at the end hands back exactly the result of the addition, reusing the `+=` logic while avoiding any extra temporary objects.

Binary arithmetic operators (`+`, `-`, `*`, `/`) must return a **new object (by value)**, not a reference. The result of `a + b` is a brand-new value that has no connection to either `a` or `b`; returning a reference to a local variable would be a textbook dangling reference—use it and you will most likely read garbage or crash outright.

The remaining operators follow exactly the same pattern. Let's fill in `*=` and `/=` first:

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
    // Dividing by a fraction equals multiplying by its reciprocal
    numerator_ *= rhs.denominator_;
    denominator_ *= rhs.numerator_;
    if (denominator_ == 0) { denominator_ = 1; }
    normalize();
    return *this;
}
```

Then we derive the binary operations from them: `Fraction operator-(Fraction lhs, const Fraction& rhs)` internally does `lhs -= rhs; return lhs;`—multiplication and division are analogous, so we won't belabor them.

## Comparison Operators—From `==` to the Full Set of Six

Since `normalize()` already guarantees the fraction is always in lowest terms, equality comparison is dead simple: same numerator and same denominator means equal.

```cpp
bool operator==(const Fraction& lhs, const Fraction& rhs)
{
    return lhs.num() == rhs.num() && lhs.den() == rhs.den();
}

// Key point: != is always implemented in terms of ==
bool operator!=(const Fraction& lhs, const Fraction& rhs)
{
    return !(lhs == rhs);
}
```

`operator!=` **must** be implemented in terms of `operator==`, written as `!(lhs == rhs)`, not as its own freshly written comparison logic. If we implement `==` and `!=` independently, sooner or later someone will change one and forget to sync the other, and `a == b` and `!(a != b)` will start contradicting each other. Beyond being a logic bug, it sends every container and algorithm that relies on comparisons (think `std::set`, `std::find`) into total disarray.

Relational comparisons follow the same idea. Mathematically, `a/b < c/d` is equivalent to `a*d < c*b` (assuming positive denominators—which `normalize()` already guarantees), and then `>`, `<=`, and `>=` are all derived from `<`:

```cpp
bool operator<(const Fraction& lhs, const Fraction& rhs)
{
    return lhs.num() * rhs.den() < rhs.num() * lhs.den();
}
bool operator>(const Fraction& lhs, const Fraction& rhs)  { return rhs < lhs; }
bool operator<=(const Fraction& lhs, const Fraction& rhs) { return !(rhs < lhs); }
bool operator>=(const Fraction& lhs, const Fraction& rhs) { return !(lhs < rhs); }
```

We only wrote the actual logic for `<`; the other three are all implemented in terms of `<`—same reasoning as `!=` building on `==`: a single source of truth, so a change only ever needs to happen in one place.

## Symmetry and Implicit Conversion—Making `3 + f` Work Too

We kept saying "non-member functions guarantee symmetry"—now let's see the concrete effect. `Fraction`'s constructor takes two `int` parameters and both have defaults, so `Fraction f = 3;` creates `Fraction(3, 1)`. When `operator+` is a non-member function and the compiler meets `3 + Fraction(1, 2)`, it tries to implicitly convert `3` to `Fraction(3, 1)` and then call `operator+`—everything works. But if `operator+` is a member function, `3.operator+(Fraction(1,2))` is flat-out illegal: `int` has no `operator+` that takes a `Fraction` parameter.

Because we exposed data access through `num()` and `den()`, the non-member functions work without needing `friend`. If your class has no convenient getters to expose, use `friend` functions to access the private members instead.

If we decided to mark the constructor `explicit` to forbid implicit conversion (a good habit in its own right), `3 + Fraction(1, 2)` would stop compiling. We would then need to provide extra overloads taking `int`: `Fraction operator+(int lhs, const Fraction& rhs)`. For math-flavored classes, leaving the constructor non-`explicit` is a common trade-off—giving up a little safety in exchange for more natural expressions.

## Practice: A Complete fraction.cpp

Now let's assemble all the parts:

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

// Binary arithmetic (non-member)
Fraction operator+(Fraction lhs, const Fraction& rhs) { lhs += rhs; return lhs; }
Fraction operator-(Fraction lhs, const Fraction& rhs) { lhs -= rhs; return lhs; }
Fraction operator*(Fraction lhs, const Fraction& rhs) { lhs *= rhs; return lhs; }
Fraction operator/(Fraction lhs, const Fraction& rhs) { lhs /= rhs; return lhs; }

// Comparison (non-member)
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

    // Mixed arithmetic with integers (implicit conversion)
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

Let's double-check: every result is correct. `a + b` gives `5/6` (after common denominators, `3/6 + 2/6`), the division `1/2 / 1/3` gives `3/2`, and the mixed expression `2 * 1/3` works too—`2` gets implicitly converted to `Fraction(2, 1)` and then takes part in the multiplication. Reduction happens automatically at every step of every operation; that is `normalize()` doing its job.

## A Look Ahead at C++20: The Three-Way Comparison Operator `<=>`

Before we close, we have to mention the three-way comparison operator (the spaceship operator) `<=>` introduced in C++20. If your compiler supports C++20, implementing a single `operator<=>` lets the compiler generate all six comparison operators automatically:

```cpp
// C++20: one line handles all comparisons
auto operator<=>(const Fraction&, const Fraction&) = default;
```

If the class's member variables themselves support three-way comparison (`int` certainly does), `= default` is all it takes. That saves the work of handwriting six comparison functions and completely eliminates the "changed `<` but forgot `<=`" class of bugs. Our tutorial keeps C++17 as its baseline, though, so handwriting the comparison operators remains a fundamental skill we must master.

## Run Online

You can also run the Fraction class online and watch operator overloading in action:

<OnlineCompilerDemo
  title="Operator Overloading: The Fraction Class"
  source-path="code/examples/vol1/13_fraction_operators.cpp"
  description="Run online and observe how the arithmetic and comparison operators behave when overloaded. Try changing the fraction values."
  allow-run
/>

## Exercises

### Exercise 1: Complete Fraction's Subtraction and Division

The complete code above already provides implementations of `operator-=` and `operator/=`. But if you have been following the tutorial step by step, try writing these two operators on your own without looking at the answer, then check them against the code for consistency. Pay special attention to how division handles a zero denominator.

::: details Reference Solution

```cpp
  Fraction& operator*=(const Fraction& rhs) {
    this->numerator_ = this->numerator_ * rhs.numerator_;
    this->denominator_ = this->denominator_ * rhs.denominator_;
    normalize();
    return *this;
  }
  Fraction& operator/=(const Fraction& rhs) {
    if (rhs.denominator_ == 0) {
      return *this;
    }
    this->numerator_ = this->numerator_ * rhs.denominator_;
    this->denominator_ = this->denominator_ * rhs.numerator_;
    normalize();
    return *this;
  }
```

:::

### Exercise 2: Implement Comparison Operators for a Date Class

Create a `Date` class with three fields—`year`, `month`, and `day`—and implement all six comparison operators. Hint: implement `operator<` first (compare year, then month, then day, in that order), then derive the other five from it. Think about it: if two `Date` objects differ in year but share the same month, how should the comparison logic be written?

::: details Reference Solution

```cpp
class Date {
 private:
  int year_{};
  int month_{};
  int day_{};

 public:
  Date() = default;
  Date(int year, int month, int day) : year_(year), month_(month), day_(day) {}
  friend bool operator<(const Date& Date1, const Date& Date2);
};
bool operator<(const Date& Date1, const Date& Date2) {
  if (Date1.year_ != Date2.year_) {
    return Date1.year_ < Date2.year_;
  }
  if (Date1.month_ != Date2.month_) {
    return Date1.month_ < Date2.month_;
  }
  return Date1.day_ < Date2.day_;
}
bool operator>(const Date& Date1, const Date& Date2) { return Date2 < Date1; }
bool operator<=(const Date& Date1, const Date& Date2) {
  return !(Date2 < Date1);
}
bool operator>=(const Date& Date1, const Date& Date2) {
  return !(Date1 < Date2);
}
bool operator==(const Date& Date1, const Date& Date2) {
  return (!(Date1 < Date2)) && (!(Date2 < Date1));
}
bool operator!=(const Date& Date1, const Date& Date2) {
  return !(Date1 == Date2);
}
```

:::

## Summary

This chapter walked the full path from theory to implementation around the core practices of operator overloading. The compound-assignment operators (`+=`, `-=`, `*=`, `/=`) are implemented as member functions that modify the object in place and return a reference to `*this`; the binary arithmetic operators (`+`, `-`, `*`, `/`) are implemented as non-member functions that take the left operand by value, reuse the compound assignments, and return a new object by value; among the comparison operators, `!=` builds on `==`, and `>`, `<=`, `>=` build on `<`, guaranteeing a single source of truth. Non-member functions keep the two operands symmetric, so both `3 + f` and `f + 3` work correctly.

The next chapter continues our operator-overloading journey with the stream operators (`<<`, `>>`) and the subscript operator (`[]`): the former lets custom types talk to `std::cout`, and the latter is the signature interface of any custom container.
