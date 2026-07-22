---
chapter: 12
cpp_standard:
- 11
- 14
- 17
- 20
description: 'The previous piece covered ADL. This one covers its most elegant partner:
  hidden friends and the Barton-Nackman trick. Define operator== as a friend inside
  a class template, and each instantiation gets an operator dedicated to its own type,
  without polluting the global overload pool, discoverable by ADL on exact match.'
difficulty: intermediate
order: 7
platform: host
prerequisites:
- 'Name Lookup and ADL: How Two-Phase Lookup Works'
- 'Class Templates: Members, Dependent Names, and Lazy Instantiation'
reading_time_minutes: 7
related:
- 'Alias Templates and using Declarations: Short Names for Types'
- 'CRTP: Static Polymorphism with the Curiously Recurring Template Pattern'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
title: 'Template Friends and Barton-Nackman: The Hidden Friends Trick'
---
# Template Friends and Barton-Nackman: The Hidden Friends Trick

The previous piece covered ADL. This one covers its most elegant partner: **hidden friends** and the **Barton-Nackman trick**. The core idea in one sentence: define operators (like `operator==`, `operator<<`) as **friends of a class template, with the definition directly inside the class**. Each instantiation then gets a non-template operator function dedicated to that type. It neither pollutes the global overload pool, and it is discoverable by ADL on exact type match. This is the recommended way to give operators to a custom type in modern C++, and the standard library uses it heavily.

## Friends: A Quick Review

A friend is C++'s way of granting an external function or class access to a class's private members. A friend is not a member; it is an external entity that is simply allowed to touch the private parts.

```cpp
class Account {
    int balance_;
    friend void audit(const Account&);   // audit is not a member, but it can read balance_
public:
    explicit Account(int b) : balance_(b) {}
};

void audit(const Account& a) {
    std::cout << "balance = " << a.balance_ << "\n";   // can read private, because it is a friend
}
```

An ordinary friend is "declared in the class, defined outside." Template friends add new tricks on top, which we build up step by step.

## Friend Injection: Defining a Friend Inside a Class Template

Here is the key step. A friend can be more than declared in the class and defined outside. It can be **defined directly inside the class.** When that class is a template, the friend defined inside it is generated, for each concrete type, as a standalone function as the class is instantiated. That function has a special property: **it is not visible at namespace scope, and is only discoverable through ADL.**

```cpp
template <typename T>
class Box {
    T v_;
public:
    constexpr Box(T v) : v_(v) {}

    // in-class friend definition: not a function template, but "a non-template function generated as Box<T> is instantiated"
    friend constexpr bool operator==(const Box& a, const Box& b) {
        return a.v_ == b.v_;
    }
};
```

Note that `operator==` here has no `template <...>` head of its own. It is written inside the `Box` class, and its parameter type uses `const Box&` (inside the class, `Box` is shorthand for `Box<T>`). When the compiler instantiates `Box<int>`, it generates a concrete function `bool operator==(const Box<int>&, const Box<int>&)`. When it instantiates `Box<double>`, it generates a different one, `bool operator==(const Box<double>&, ...)`. The two are distinct, each minding its own type.

This technique is the **Barton-Nackman trick**, named after John Barton and Lee Nackman, who systematized it in their 1994 book "Scientific and Engineering C++". The core problem it solves: give a class template operators automatically, without writing a specialization for every type.

## A Complete Example: == and <<

Here is a runnable complete example, giving `Box<T>` both `==` and `<<`.

```cpp
#include <iostream>

template <typename T>
class Box {
    T v_;
public:
    constexpr Box(T v) : v_(v) {}

    friend constexpr bool operator==(const Box& a, const Box& b) {
        return a.v_ == b.v_;
    }
    friend std::ostream& operator<<(std::ostream& os, const Box& b) {
        return os << "Box{" << b.v_ << "}";
    }
};

int main() {
    Box<int> x{1}, y{1}, z{2};
    Box<double> p{1.5}, q{1.5};
    std::cout << std::boolalpha;
    std::cout << "x == y: " << (x == y) << "\n";   // true
    std::cout << "x == z: " << (x == z) << "\n";   // false
    std::cout << "p == q: " << (p == q) << "\n";   // true (Box<double>'s own operator==)
    std::cout << x << "\n";                        // Box{1}, ADL finds operator<<
    return 0;
}
```

```bash
$ g++ -Wall -Wextra -std=c++20 barton.cpp -o barton && ./barton
x == y: true
x == z: false
p == q: true
Box{1}
```

`Box<int>` and `Box<double>` each have their own `operator==`. Comparing `x == y` goes through the `Box<int>` version, comparing `p == q` through the `Box<double>` version. `std::cout << x` works because `operator<<` is a hidden friend of `Box<int>`, and ADL discovers it through the type of `x`. You wrote neither `std::operator<<` nor a `using namespace`; it is all ADL.

## The Power of Hidden Friends: No Global Overload Pollution

The real value of hidden friends shows when you compare with the older "function-template operator== at namespace scope." The old style looks like this.

```cpp
// old style: define an operator== template at namespace scope
// (this assumes Box exposes a public value(); the Box in this piece keeps v_
//  private, so the old style would need either a public accessor on Box or
//  this operator== also declared as a friend)
template <typename T>
bool operator==(const Box<T>& a, const Box<T>& b) {
    return a.value() == b.value();
}
```

This `operator==` is a **function template** living at namespace scope, participating as a candidate for every `Box<T>` comparison. The problem is that it participates in overload resolution for **every** `==` expression where the argument types are even tangentially related. This causes two troubles. First, slow compiles, since for every `==` the compiler has to consider whether this template applies. Second, ambiguity risk, if another namespace also has an `operator==` template, both might match and produce ambiguity.

Hidden friends fix both. It is not a function template, but a concrete function generated on instantiation. It is not visible at namespace scope. Only when the argument types of `==` exactly match `Box<T>` does ADL pull it into the candidate set. Put differently, it appears "only when it should," and is otherwise completely transparent. This makes overload resolution faster and avoids accidental matches between unrelated types.

::: warning Hidden friends not crossing types is a feature, not a bug

A hidden friend is discovered only when argument types match exactly. `Box<int>`'s `operator==` takes only `Box<int>`, never `Box<double>`. So the following line fails to compile.

```cpp
Box<int> x{1};
Box<double> p{1.5};
bool same = (x == p);   // error: Box<int> and Box<double> have no common operator==
```

```text
barton_bad.cpp:12:20: error: no match for 'operator=='
      (operand types are 'Box<int>' and 'Box<double>')
```

This is the safety of hidden friends. If you genuinely want `Box<int>` to compare to `Box<double>`, you write a cross-type operator explicitly, rather than hoping it "just happens." Hidden friends make type relationships explicit, which is exactly why they are recommended.

:::

## Why This Is Tightly Coupled with ADL

Hidden friends are unusable without ADL. As noted earlier, an in-class friend definition is not visible at namespace scope, so ordinary lookup cannot find it. Only ADL can: when calling `x == y`, the compiler uses the argument `x` (type `Box<int>`) to pull `Box<int>`'s hidden friend `operator==` into the candidate set.

So hidden friends are ADL's best partner. ADL makes "operators defined inside a class" findable, and hidden friends make "calls that should not be found" unfoundable. Together, the scope of an operator is controlled precisely to "only effective for this type." This is also why the previous piece spent so much effort on ADL; it is the prerequisite for understanding this one.

## A Practical Pattern: Giving a Type Its Comparison Operators

In practice, the recommended template for giving a type its operators looks like this.

```cpp
class Temperature {
    double kelvin_;
public:
    constexpr explicit Temperature(double k) : kelvin_(k) {}
    constexpr double k() const { return kelvin_; }

    // hidden friends: all operators written as in-class friends
    friend constexpr bool operator==(Temperature a, Temperature b) {
        return a.kelvin_ == b.kelvin_;
    }
    friend constexpr bool operator!=(Temperature a, Temperature b) {
        return !(a == b);   // reuse ==
    }
    friend constexpr bool operator<(Temperature a, Temperature b) {
        return a.kelvin_ < b.kelvin_;
    }
    // ... other comparison operators
};
```

After C++20 there is an even more economical option: define an `operator<=>` (the three-way comparison operator, covered in another piece in this volume), and the compiler auto-generates `==`, `!=`, `<`, `<=`, `>`, `>=` for you. But if you do not use spaceship, or you want custom semantics for some operator, hidden friends remain the preferred style. Operators in the standard library's iterators and in `std::chrono` duration types are mostly hidden friends.

Next we move into alias templates and using declarations. How `template <typename T> using vec = std::vector<T>;` gives types short names, why alias templates cannot be specialized, and how `using` introduces base-class names in template inheritance, all get covered.
