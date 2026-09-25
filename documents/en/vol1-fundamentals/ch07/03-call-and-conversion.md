---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: Master overloading `operator()` and conversion operators, and learn
  to implement function objects and safe implicit conversions.
difficulty: intermediate
order: 3
platform: host
prerequisites:
- Stream and Subscript Operators
reading_time_minutes: 14
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Function Calls and Type Conversion
translation:
  source: documents/vol1-fundamentals/ch07/03-call-and-conversion.md
  source_hash: 9ceca71e7a5a52ca9c334045ef675be164124b0f8001541dc89b45902b42c971
  translated_at: '2026-09-25T11:13:11+00:00'
  engine: anthropic
  token_count: 7800
---
# Function Calls and Type Conversion: Objects That Can Be Used Like Functions

In the preceding chapters we have given our custom types arithmetic, subscript access, and stream I/O—making objects behave like values, like containers, like printable things. But the power of operator overloading goes far beyond that. In this chapter we take on two particularly interesting scenarios: making an object behave like a function, and letting an object "become" another type, implicitly or explicitly.

Sounds a bit magical? It's really not complicated. An object with an overloaded `operator()` can be "called" just like a function—we call it a **function object** (functor), and it is a core building block of callback mechanisms and generic algorithms in C++. Back in the earliest days of C++, we used this trick everywhere.

Conversion operators, for their part, let an object convert into another type—for example, letting a smart pointer test itself for null naturally inside an `if` statement. Together, these two mechanisms are key tools for building flexible, expressive abstractions.

But the two are also the biggest minefields in overloading. An implicit conversion can sneak in without our noticing, and a function object with badly managed state can make an algorithm's results come out completely wrong. So let's proceed step by step: first we will nail down the mechanics of `operator()`, then dig into conversion operators—including how the `explicit` variants introduced in C++11 help us dodge those age-old traps.

## Making Objects Callable — operator()

The syntax of the function call operator `operator()` is not complicated, but the paradigm shift it brings is profound. Once a class overloads `operator()`, its instances can be used with function-call syntax just like a function: append a pair of parentheses and an argument list after the object.

```cpp
class Multiplier {
private:
    int factor_;

public:
    explicit Multiplier(int factor) : factor_(factor) {}

    int operator()(int x) const { return x * factor_; }
};

Multiplier triple(3);
int result = triple(10);  // 30 — triple acts just like a "multiply by 3" function
```

Here `triple(10)` looks like an ordinary function call, but it is really syntactic sugar for `triple.operator()(10)`. The `Multiplier` instance `triple` is an object, yet it behaves no differently from a function—so we call it a **function object**, or a **functor**.

You might ask: what's the difference from an ordinary function pointer? The difference is enormous. A plain function pointer can only point at a function; it cannot carry any extra state. A function object, by contrast, is a real object: it has member variables, it can save parameters at construction time, and it can lean on that saved state in every subsequent call. The `Multiplier` above is a textbook example: `factor_` is its "state"; different instances can carry different multipliers while their "call interface" stays exactly the same. "Functions with state" like this are tremendously useful in generic programming.

One point about `operator()`'s signature deserves special attention: it can have almost any signature. Parameter types, parameter count, and return type are all up to you; the only restriction is that it must be a member function (the language specifies that `operator()` cannot be overloaded as a non-member). It can come in multiple overloaded versions, it can be a function template, and it can even be variadic. That flexibility lets function objects fit nearly every scenario that calls for a "callable entity".

Also, you'll notice that the `operator()` above is marked `const`. That's a good habit: if calling the function object doesn't modify internal state, mark it `const`, and it will then work correctly in `const` contexts too. Of course, some function objects are designed from the start to modify internal state (a counter, say)—in that case, omitting `const` is the right choice.

## Function Objects in Practice

A lone `Multiplier` may not feel very concrete, so let's look at a more practical example: a custom comparator working with `std::sort`. The standard library's sorting algorithm accepts an optional comparison argument, and we can pass in a function object to define our own ordering rule:

```cpp
#include <algorithm>
#include <vector>

struct DescendingOrder {
    bool operator()(int a, int b) const { return a > b; }
};

int main()
{
    std::vector<int> data = {3, 1, 4, 1, 5, 9, 2, 6};

    // Pass in a function object to sort in descending order
    std::sort(data.begin(), data.end(), DescendingOrder());

    // data is now {9, 6, 5, 4, 3, 2, 1, 1}
    return 0;
}
```

Notice that what we pass to `std::sort` is `DescendingOrder()`—a temporary function object instance. `std::sort` copies that object internally, then invokes its `operator()` each time two elements need to be compared. The pattern is everywhere in the standard library: `std::find_if` takes a predicate function object, `std::transform` takes a transformation function object, `std::accumulate` takes an accumulation function object—they all use `operator()` as the channel for "injecting custom behavior".

Stateful function objects and the copy semantics of algorithms—the trap here is exceptionally well hidden. Standard library algorithms **copy** the function objects we pass in. If we design a stateful function object (a counter tallying comparisons, for example), the copy inside the algorithm and the original object are independent, and none of the algorithm's internal results can be read back from the original object. Take a look:

```cpp
struct CountingComparator {
    int count = 0;
    bool operator()(int a, int b) { ++count; return a < b; }
};

CountingComparator comp;
std::vector<int> v = {5, 2, 8, 1, 9};
std::sort(v.begin(), v.end(), comp);
// comp.count is very likely still 0!
// because sort made its own copy of comp; the comparison count lives in that copy
```

If we truly do need to extract a function object's state from an algorithm, C++11's `std::ref` can help—`std::sort(v.begin(), v.end(), std::ref(comp))` passes in a reference wrapper and dodges the copy. But the better approach is to understand the algorithm's copy semantics and take it into account when designing the function object in the first place.

The power of function objects became far easier to reach once C++11 brought in lambdas—a lambda is, at heart, a function object the compiler generates for you. But before you understand lambdas, writing function objects by hand is the obligatory path to understanding the mechanism. We'll give lambdas a dedicated discussion later; for now, keep the focus on the mechanics of `operator()` itself.

## Conversion Operators: Turning Objects into Another Type

> PS: we do not recommend abusing this feature wholesale—judge whether each conversion feels natural. Take smart pointers: a smart pointer is a kind of pointer, and native pointers can be tested in boolean contexts, so we can add a matching `operator bool()` to keep the call sites reading naturally.

A conversion operator lets an object of a class be converted, implicitly or explicitly, into another type. The syntax is `operator TargetType()`, with no return-type declaration (because the return type is the target type itself):

```cpp
class NullableInt {
private:
    int value_;
    bool has_value_;

public:
    NullableInt(int v) : value_(v), has_value_(true) {}
    NullableInt() : value_(0), has_value_(false) {}

    // Implicit conversion to bool: checks whether it holds a value
    operator bool() const { return has_value_; }

    // Implicit conversion to int: retrieves the value
    operator int() const { return value_; }
};

NullableInt a(42);
NullableInt b;  // empty value

if (a) {
    // a holds a value, so we end up in here
    int x = a;  // implicitly converts to int, x = 42
}
```

Here we use `operator bool()` so a `NullableInt` works directly in an `if` statement, and `operator int()` so it can be assigned to an `int` variable. In some scenarios this is genuinely convenient—a smart pointer overloading `operator bool()` to test for null is a thoroughly classic usage.

But the flip side of convenience is danger. Implicit conversions fire quietly in places where we **never intended them to happen**. Whenever the compiler decides "the types don't match, but a conversion could make them match", it will call the conversion operator automatically. Consider this scenario:

```cpp
NullableInt a(10);
NullableInt b(20);
int result = a + b;
// We might expect a compile error here — NullableInt doesn't overload operator+
// But in reality: a implicitly converts to int(10), b to int(20), result = 30
```

If that is the behavior we wanted, fine. But what if one of our `NullableInt`s holds the empty value? `NullableInt() + NullableInt(5)` yields `0 + 5 = 5`—the empty value silently joins the arithmetic as 0, without a single warning. Worse still, if a class provides both `operator int()` and `operator double()`, overload resolution can turn ambiguous: the compiler wavers between the two conversion paths and then emits a thoroughly baffling error.

Non-explicit conversion operators are the most dangerous kind of implicit contract. A classic cautionary tale comes from the "safe bool idiom" of the C++98 era. Back then, smart pointers that wanted to support the `if (ptr)` syntax would typically overload `operator bool()` or some member-pointer type. But `operator bool()` takes part in arithmetic—`ptr + 1` would actually compile, because `ptr` gets implicitly converted to `bool` first (0 or 1), and then `1 + 1 = 2`. Implicit conversions like these are brutally hard to hunt down in a large codebase. C++11 handed us a clean solution: `explicit operator bool`, which we'll get to right away.

## explicit Conversion Operators (C++11): The Safe Default

C++11 introduced the `explicit` specifier for conversion operators, and its effect mirrors an `explicit` constructor: **implicit conversion is forbidden; only explicit use is allowed**. But there is one exquisitely crafted exception—in boolean contexts (the condition parts of `if`, `while`, and `for`, plus the operands of `!`, `&&`, and `||`), an `explicit operator bool` can still be triggered implicitly. That exception was designed precisely for types like smart pointers that need boolean testing:

```cpp
class SafeBool {
private:
    bool value_;

public:
    explicit SafeBool(bool v) : value_(v) {}

    explicit operator bool() const { return value_; }
};

SafeBool sb(true);

// Boolean context: implicit use is fine
if (sb) {
    // we enter the branch as usual
}

// Non-boolean context: explicit conversion required
bool b = static_cast<bool>(sb);  // OK
// int n = sb;  // Compile error! No implicit conversion allowed
// int x = sb + 1;  // Compile error! Won't take part in arithmetic
```

Look closely at the last two commented-out lines: had `operator bool()` not been `explicit`, they would compile (even though the semantics would be utterly wrong); with `explicit`, the compiler refuses the dangerous implicit conversion outright. Meanwhile, in a boolean context like `if (sb)`, the `explicit` restriction is automatically relaxed—precisely the behavior we want: test the boolean safely, but refuse any accidental participation in arithmetic.

This leaves us with a crisp design guideline: **conversion operators should be `explicit` by default**. The only cases that can go without `explicit` are conversions whose semantics are so unambiguous that misreading is nearly impossible—`operator std::string_view() const` on a string wrapper class, say—but even then, think twice before you commit.

## Practice: callable.cpp

Now let's put `operator()` and conversion operators together and write one complete example. The program has three parts: a threshold-based checker function object, a safe bool wrapper, and a string-number class supporting explicit conversions.

```cpp
// callable.cpp
#include <cstdio>
#include <cstring>
#include <string>

/// @brief A threshold-based range-checking function object
class ThresholdChecker {
private:
    int min_;
    int max_;
    int rejected_count_;

public:
    ThresholdChecker(int min_val, int max_val)
        : min_(min_val), max_(max_val), rejected_count_(0)
    {
    }

    /// @brief Checks whether a value is in range; increments the rejection count when it is not
    bool operator()(int value)
    {
        if (value < min_ || value > max_) {
            ++rejected_count_;
            return false;
        }
        return true;
    }

    int rejected_count() const { return rejected_count_; }

    void reset() { rejected_count_ = 0; }
};

/// @brief A safe bool wrapper using explicit operator bool
class SafeBool {
private:
    bool value_;

public:
    explicit SafeBool(bool v) : value_(v) {}

    explicit operator bool() const { return value_; }
};

/// @brief A number stored as a string, supporting explicit conversion to int and const char*
class StringNumber {
private:
    char buffer_[32];

public:
    explicit StringNumber(const char* str)
    {
        std::strncpy(buffer_, str, sizeof(buffer_) - 1);
        buffer_[sizeof(buffer_) - 1] = '\0';
    }

    explicit operator int() const { return std::atoi(buffer_); }

    explicit operator const char*() const { return buffer_; }
};

int main()
{
    // --- ThresholdChecker: function object ---
    ThresholdChecker checker(0, 100);

    int test_values[] = {50, -1, 75, 200, 30, -5, 88};
    const char* labels[] = {"50", "-1", "75", "200", "30", "-5", "88"};

    std::printf("=== ThresholdChecker (0..100) ===\n");
    for (int i = 0; i < 7; ++i) {
        bool ok = checker(test_values[i]);
        std::printf("  %s -> %s\n", labels[i], ok ? "PASS" : "REJECT");
    }
    std::printf("  Rejected: %d\n", checker.rejected_count());

    // --- SafeBool: explicit operator bool ---
    std::printf("\n=== SafeBool ===\n");
    SafeBool flag_true(true);
    SafeBool flag_false(false);

    if (flag_true) {
        std::printf("  flag_true is truthy\n");
    }
    if (!flag_false) {
        std::printf("  flag_false is falsy\n");
    }

    // --- StringNumber: explicit conversion ---
    std::printf("\n=== StringNumber ===\n");
    StringNumber sn("42");
    StringNumber sn2("100");

    int val = static_cast<int>(sn);
    int val2 = static_cast<int>(sn2);
    const char* str = static_cast<const char*>(sn);

    std::printf("  StringNumber(\"42\") as int: %d\n", val);
    std::printf("  StringNumber(\"100\") as int: %d\n", val2);
    std::printf("  StringNumber(\"42\") as string: %s\n", str);
    std::printf("  Sum: %d\n", val + val2);

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o callable callable.cpp && ./callable
```

Expected output:

```text
=== ThresholdChecker (0..100) ===
  50 -> PASS
  -1 -> REJECT
  75 -> PASS
  200 -> REJECT
  30 -> PASS
  -5 -> REJECT
  88 -> PASS
  Rejected: 3

=== SafeBool ===
  flag_true is truthy
  flag_false is falsy

=== StringNumber ===
  StringNumber("42") as int: 42
  StringNumber("100") as int: 100
  StringNumber("42") as string: 42
  Sum: 142
```

Let's take it apart piece by piece. `ThresholdChecker` is a typical stateful function object: every call to `operator()` checks whether the value falls inside the given range while tallying how many were rejected. Note that `operator()` here is not marked `const`, because it modifies `rejected_count_`. Of the 7 test values, 3 are rejected, and `rejected_count()` records that number faithfully—handed to some algorithm via `std::ref`, it could tell us "how many comparisons were made" or "how many were rejected" once the algorithm finishes.

`SafeBool` demonstrates the correct use of `explicit operator bool`. It works naturally in an `if` condition, but the moment you try assigning it to an `int` or letting it join an arithmetic expression, the compiler barks. Exactly what we want—boolean semantics stay clean, with no overflow risk.

`StringNumber` shows several explicit conversion operators coexisting. It supports conversion to both `int` and `const char*`, but since both are marked `explicit`, you must request the conversion explicitly with a `static_cast`—there is no room for the compiler to pick a conversion path "on our behalf".

## Try It Yourself

### Exercise 1: Implement a Generic Comparator Function Object

Write a template class `GenericComparator` whose constructor accepts a sort strategy (ascending or descending) and which performs its comparisons through `operator()`. It must support any comparable type (implement it with templates) and offer a member function that returns the total comparison count.

Hint: we can represent the sort strategy with the enum `enum class Order { kAscending, kDescending };`, and inside `operator()` return `a < b` or `a > b` depending on which strategy is active.

To verify: use your `GenericComparator` together with `std::sort` to sort a `std::vector<double>` in ascending and then descending order, printing the results before and after each sort.

### Exercise 2: Implement explicit operator bool for a Result Class

Implement a `Result<T>` class template that either holds a valid value or holds an error-message string. Requirements: overload `explicit operator bool()` to tell whether a valid value is held; provide a `value()` member function to fetch the valid value (print the error message and terminate when there is none); and provide an `error()` member function to fetch the error message.

Hint: we can store the data using `std::optional<T>`, or with a `bool` flag plus a `union`.

To verify: create one `Result<int>` holding a value and one holding an error, test the boolean conversion of each with `if (result)`, and confirm the logic is correct.
