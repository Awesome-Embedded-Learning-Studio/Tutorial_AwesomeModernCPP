---
chapter: 12
cpp_standard:
- 11
- 14
- 17
- 20
description: 'The previous piece covered ADL; this one covers its most elegant partner:
  hidden friends and the Barton-Nackman trick. Define operator== as a friend inside
  a class template, and every instantiation automatically gains an operator dedicated
  to that exact type — no pollution of the global overload pool, precise discoverability
  through ADL.'
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
translation:
  source: documents/vol4-advanced/vol1-basics-cpp11-14/07-friends-and-barton-nackman.md
  source_hash: 4c6bc69015cd1f732b4d8726851d38d20909c7b78efb157e116111b17bef8b9e
  translated_at: '2026-09-26T04:15:26+00:00'
  engine: anthropic
  token_count: 1600
---
# Template Friends and Barton-Nackman: The Hidden Friends Trick

Last time we covered ADL; this time we meet its most elegant partner: **hidden friends** and the **Barton-Nackman trick**. The core idea in one sentence: define operators (such as `operator==`, `operator<<`) as **friends of a class template, with the definition written directly inside the class**. Each instantiation then automatically produces a non-template operator function dedicated to that exact type: it never pollutes the global overload pool, yet ADL can still find it on a precise type match. This is the recommended way to equip a custom type with operators in modern C++, and the standard library itself leans on it heavily.

## Friends: A Quick Review

A friend is C++'s way of granting some external function or class permission to "access my private members". A friend is not a member of the class; it is an external entity that has simply been licensed to touch the private parts.

```cpp
class Account {
    int balance_;
    friend void audit(const Account&);   // audit is not a member, but it can access balance_
public:
    explicit Account(int b) : balance_(b) {}
};

void audit(const Account& a) {
    std::cout << "balance = " << a.balance_ << "\n";   // private access, because it is a friend
}
```

An ordinary friend is "declared inside the class, defined outside". Template friends take that foundation and run somewhere new with it — let's walk through it step by step.

## Friend Injection: Defining Friends Inside a Class Template

Here comes the crucial step. A friend can not only be declared inside the class and defined outside, it can also be **given its definition directly inside the class body**. When the class is a template, the in-class friend definition is instantiated along with the class, generating a separate function for each concrete type. That function has a special property: **it is not visible at namespace scope, and it can only be found through ADL**.

```cpp
template <typename T>
class Box {
    T v_;
public:
    constexpr Box(T v) : v_(v) {}

    // in-class friend definition: not a function template, but a non-template function generated per Box<T> instantiation
    friend constexpr bool operator==(const Box& a, const Box& b) {
        return a.v_ == b.v_;
    }
};
```

Notice that this `operator==` has no `template <...>` header of its own: it is written inside the `Box` class, and its parameter type is `const Box&` (inside the class, `Box` is shorthand for `Box<T>`). When the compiler instantiates `Box<int>`, it generates a concrete `bool operator==(const Box<int>&, const Box<int>&)`; instantiating `Box<double>` generates another one, `bool operator==(const Box<double>&, ...)`. The two functions are distinct, each taking care of its own type.

This technique is called the **Barton-Nackman trick**, named after John Barton and Lee Nackman, whose 1994 book *Scientific and Engineering C++* was where the two of them first used this style systematically. The core problem it solves: equipping a class template with operators automatically, without writing a specialization for every single type.

## A Complete Example: Equipping == and <<

Let's look at a complete, runnable example that gives `Box<T>` both `==` and `<<`.

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
    std::cout << x << "\n";                        // Box{1}; ADL finds operator<<
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

`Box<int>` and `Box<double>` each have their own `operator==`: comparing `x == y` picks the `Box<int>` version, comparing `p == q` picks the `Box<double>` version. `std::cout << x` works because `operator<<` is a hidden friend of `Box<int>` — ADL discovers it through the type of the argument `x`. You never wrote `std::operator<<`, and you never opened a `using namespace`; ADL did all the work.

## The Benefit of Hidden Friends: No Pollution of the Global Overload Pool

The real value of hidden friends only becomes clear in contrast with "a namespace-scope function-template `operator==`". The old-school spelling looks like this:

```cpp
// Old style: define an operator== template at namespace scope
// (this assumes Box exposes a public value(); this piece's Box keeps v_ private
//  and friend-only, so the old style needs a public accessor or a friend declaration too)
template <typename T>
bool operator==(const Box<T>& a, const Box<T>& b) {
    return a.value() == b.value();
}
```

This `operator==` is a **function template**. It lives at namespace scope and joins the candidate set for every comparison of `Box<T>` values. The problem: it participates in overload resolution for **every** `==` expression (whenever the argument types are even remotely related), which causes two headaches. First, slower builds: for every `==`, the compiler has to consider whether this template fits. Second, ambiguity risk: if another namespace also has an `operator==` template, both templates can match at once and you get an ambiguity.

Hidden friends solve both headaches at once. A hidden friend is not a function template but a concrete function generated at instantiation; it is not visible at namespace scope, and only when the operands of `==` match `Box<T>` exactly does ADL pull it into the candidate set. In other words, it shows up exactly when it is supposed to, and is otherwise completely invisible. That not only makes overload resolution faster, it also rules out accidental matches between unrelated types.

::: warning Hidden friends refusing to cross type boundaries is a feature, not a bug

Hidden friends are found only when the argument types match exactly. `Box<int>`'s `operator==` accepts only `Box<int>`, never `Box<double>`. So the following line fails to compile:

```cpp
Box<int> x{1};
Box<double> p{1.5};
bool same = (x == p);   // error: Box<int> and Box<double> share no common operator==
```

```text
barton_bad.cpp:12:20: error: no match for 'operator=='
      (operand types are 'Box<int>' and 'Box<double>')
```

This is exactly the safety of hidden friends. If you genuinely want `Box<int>` and `Box<double>` to be comparable, you have to write a cross-type operator explicitly rather than hoping it happens "automatically". Hidden friends make type relationships explicit, and that is precisely why they come so highly recommended.

:::

## Why This Is So Tightly Coupled with ADL

Hidden friends are unusable without ADL. As mentioned earlier, a friend defined inside a class is not visible at namespace scope, so ordinary lookup cannot find it. Only ADL can: when `x == y` is called, the compiler uses the scope associated with the argument `x` (of type `Box<int>`) to pull `Box<int>`'s hidden friend `operator==` into the candidate set.

So hidden friends are ADL's best partner. ADL makes "operators defined inside the class" findable, while hidden friends keep "calls that should not match" from matching. Working together, an operator's scope is pinned down precisely to "takes effect for this type only". This is also why the previous piece spent so much ink on ADL — it is the prerequisite for understanding this one.

## A Practical Recipe: Equipping a Type with the Full Set of Comparison Operators

When you are writing a real library, the recommended pattern for equipping a type with operators looks like this:

```cpp
class Temperature {
    double kelvin_;
public:
    constexpr explicit Temperature(double k) : kelvin_(k) {}
    constexpr double k() const { return kelvin_; }

    // hidden friends: every operator is written as an in-class friend
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

Since C++20 there is an even easier route: define `operator<=>` (the three-way comparison operator, covered in its own piece later in this volume), and the compiler generates `==`, `!=`, `<`, `<=`, `>`, and `>=` for you automatically. But if you don't use the spaceship, or you want custom semantics for some of the operators, hidden friends remain the go-to style. In the standard library, iterators and the `std::chrono` duration types write virtually all of their operators as hidden friends.

Next up: alias templates and using declarations. We will cover how a spelling like `template <typename T> using vec = std::vector<T>;` gives types short names, why alias templates cannot be specialized, and the role they play in introducing base-class names during template inheritance.
