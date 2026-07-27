---
chapter: 13
cpp_standard:
- 20
description: 'The word requires in C++20 is both a clause and an expression, and they are easy to confuse. Taking requires expressions apart. The four kinds of requirements, how to define a concept with one, and the two traps of unevaluated context and hard errors on concrete types.'
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Constraining Templates with Concepts: Subsumption and Overloading'
- 'Concepts: Putting Constraints in the Signature'
reading_time_minutes: 13
related:
- 'Concepts: Putting Constraints in the Signature'
- 'Constraining Templates with Concepts: Subsumption and Overloading'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
- concepts
- 类型安全
title: 'Requires Expressions, In Depth: The Four Kinds'
---
# Requires Expressions, In Depth: The Four Kinds

In the last two pieces, the word `requires` kept showing up, sometimes as a clause, sometimes as an expression. They look the same but do different jobs. This piece takes them fully apart: what a requires expression is, what kinds of requirements it can state, how to use it to describe "what operations a type must provide" on the spot, and two traps that trip people up most. After this piece, every `requires(T t){ ... }` you see will have a clear origin.

## Two kinds of requires: clause and expression

Before going further, put the two same-named things side by side. Everything below builds on this.

| | requires clause (requires-clause) | requires expression (requires-expression) |
|---|---|---|
| **What it is** | a syntactic slot that adds a constraint to a template | an expression that evaluates to bool at compile time |
| **What it looks like** | `requires Numeric<T>` | `requires(T t) { t.size(); }` |
| **Value** | not a value, it's a constraint declaration | a bool (true / false) |
| **Where it appears** | after the template parameter list | almost anywhere a bool is needed: in a clause, in a concept definition, in a `static_assert` |

The star of the last piece, subsumption and overload dispatch, was the **clause**. The star of this piece is the **expression**. They often pair up: a clause that contains an expression is `requires requires(T t){ t+t; }`, the double-`requires` you've seen. The outer one is a clause, the inner one is an expression.

## The four kinds of requirements in a requires expression

Inside the braces of a requires expression `requires(params) { ... }`, you can write four different kinds of "requirements." Let's define a `Container` concept and use all four at once:

```cpp
#include <concepts>
#include <vector>

template <typename T>
concept Container = requires(T t) {
    // 1. simple requirement: the expression must be valid, it just has to compile
    t.begin();
    t.end();

    // 2. type requirement: this nested type must exist
    typename T::value_type;

    // 3. compound requirement: the expression is valid, and the return satisfies a constraint
    { t.size() } -> std::convertible_to<std::size_t>;

    // 4. nested requirement: another compile-time bool judgment inside
    requires std::integral<typename T::value_type>;
};

static_assert(Container<std::vector<int>>);   // vector<int> meets all four
static_assert(!Container<int>);               // int has no begin/end, fails the first one
```

These two `static_assert`s are compile-time assertions. If the code compiles, `vector<int>` satisfies `Container` and `int` doesn't. No need to run anything.

Each kind has its use. The simple requirement is the most common. `t.begin();` just asks "can an object of type `T` call `begin()`, and if it compiles, it passes." The type requirement `typename T::value_type;` checks that a nested type exists. It shows up constantly in trait checks on containers and iterators. The compound requirement `{ t.size() } -> std::convertible_to<std::size_t>;` binds "the expression is valid" and "the return type satisfies the constraint" together. It's tighter than checking the call separately and then querying the return type with `decltype`. A compound requirement can also add `noexcept`: `{ t.size() } noexcept -> std::convertible_to<std::size_t>;`, which requires the call to not throw. The nested requirement `requires std::integral<...>;` lets you tuck another concept judgment inside the expression. It fits "once the main requirements hold, also satisfy this extra one."

A side note that's easy to miss: the fourth requirement on `Container` says `value_type` is an integer type, and `std::integral<char>` is **true** (char is in the integer family, same as `integral<bool>` being true in the last piece). So `Container<std::string>` actually satisfies it, because string's `value_type` is char and passes the fourth requirement. If you only want containers whose `value_type` is exactly `int`, swap the fourth line for `std::same_as<typename T::value_type, int>`.

## A requires expression is a bool: it doesn't have to be a named concept

A requires expression evaluates to a bool, so it doesn't only live inside a concept definition. Anywhere you need a compile-time judgment, you can use it directly.

```cpp
#include <string>

// Drop it straight into a static_assert, no concept needed
static_assert(requires(std::string s) { s.size(); });   // string has size()

// Use it directly in if constexpr for a compile-time branch
template <typename T>
void process(T t) {
    if constexpr (requires(T x) { x.empty(); }) {
        std::cout << "has empty()\n";
    } else {
        std::cout << "no empty()\n";
    }
}
```

<OnlineCompilerDemo allow-run
  title="A requires expression as a bool"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/requires_expression.cpp"
  description="An inline requires expression dropped straight into if constexpr to judge at compile time whether a type has empty(), without defining a concept first."
/>

Run it:

```text
has empty()
no empty()
```

For a quick check of "does this type have this operation," an inline requires expression is the lightest tool. But note the tradeoff: an inline expression has no name, so it doesn't form a reusable atomic constraint. In the last piece we said overload dispatch relies on named concepts to build entailment. If you want two overloads to dispatch by constraint, you need named concepts (`concept C = requires(...){...}`). Inline expressions can't subsume. So for a check you only use once, an inline expression is fine. For a requirement that has to dispatch or be reused, lift it into a concept.

## Trap one: a requires expression is not evaluated

This is the most counterintuitive trap. The calls written inside a requires expression only **check whether they compile**. They never actually run. Let's run one for proof.

```cpp
#include <iostream>

int counter = 0;
int increment() {
    ++counter;
    std::cout << "[side effect] increment called\n";
    return 1;
}

template <typename T>
concept MentionsIncrement = requires(T t) {
    increment();   // only checks "is this call legal", does not evaluate, does not run
};

int main() {
    static_assert(MentionsIncrement<int>);   // satisfied: the increment() call is legal
    std::cout << "concept evaluated, counter = " << counter << "\n";
    increment();                              // this is the real call
    std::cout << "after the real call, counter = " << counter << "\n";
}
```

<OnlineCompilerDemo allow-run
  title="A requires expression is unevaluated, counter proof"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/unevaluated.cpp"
  description="The increment() call inside requires only checks legality, it never runs. After the concept evaluates, counter is still 0, until the real call in main."
/>

Run it:

```text
concept evaluated, counter = 0
[side effect] increment called
after the real call, counter = 1
```

Look at the `counter = 0` line. When `MentionsIncrement<int>` gets evaluated, the `increment()` call inside the requires expression **never ran**. Counter is still 0, and the side-effect line didn't print. Only when `main` actually writes `increment()` does counter become 1.

A requires expression belongs to an **unevaluated context**, like `decltype` and `sizeof`. The compiler only cares whether the expressions inside are "type-legal." It generates no call, and it triggers no side effects. Beginners trip on this a lot: they write something inside a requires expression that "looks like it initializes" or "looks like it computes," assume it ran, and nothing happened. Use requires expressions to judge type capability. To actually make code run, you still have to write it in an ordinary function body.

## Trap two: writing a concrete type directly gives a hard error

The second trap is sneakier, and it's the flip side of the first. Suppose we want to test "string doesn't have some method." The intuitive way is to put string into the requires expression as the parameter:

```cpp
// Intuitive: test the negative case with a concrete string type
static_assert(!requires(std::string s) { s.nope(); });   // string has no nope
```

```text
four_requirements2.cpp:17:44: error: 'std::string' has no member named 'nope'
```

That's a hard error, not an elegant false. Why? Because a requires expression is "immediately evaluated" for a **concrete type**. The compiler sees the concrete type `std::string s`, goes straight into string to look for `nope`, doesn't find it, and errors hard. It doesn't go through the SFINAE path of "substitution failure returns false." To make it sting more, `requires(int x) { x.foo(); }` reports `request for member 'foo' in 'x', which is of non-class type 'int'`, because a basic type like `int` can't take `.foo()` syntax at all, and parsing fails on the spot.

The fix is to keep the requires expression in a **template context**. The usual move is to wrap it in a concept:

```cpp
template <typename T> concept HasSize = requires(T t) { t.size(); };
template <typename T> concept HasNope = requires(T t) { t.nope(); };

static_assert(HasSize<std::string>);    // string has size -> true
static_assert(!HasNope<std::string>);   // string has no nope -> false, elegant this time
static_assert(!HasSize<int>);           // int has no size -> false
```

```bash
$ g++ -std=c++20 -Wall -Wextra neg_via_concept.cpp -o nvc && echo "all assertions passed"
all assertions passed
```

Once it's wrapped in a concept, `T` is a template parameter. Evaluating the requires expression takes the SFINAE-friendly path: a missing member comes out as false, not a hard error. So for negative test cases, wrap them in a named concept. Don't shove concrete types straight into a requires expression.

::: warning The two traps are two sides of one coin
"Unevaluated" and "hard error on concrete type" both root in when a requires expression gets evaluated. A requires expression is "deferred and SFINAE-friendly" for a template parameter, so it doesn't execute (not evaluated) and returns false on failure. It's "immediate" for a concrete type, so it also doesn't execute, but a failure errors hard. Remember one line: a requires expression only checks "can this compile," it never runs. To make it return false gracefully, keep it in a template context (usually, wrap it as a concept).
:::

Three things together, the four kinds of requirements, unevaluated, and template context, and you've taken apart the most easily confused word in C++20. In the next piece we look at the template's compile-time power from another direction: before concepts existed, how did template metaprogramming (TMP) do compile-time computation and type deduction with specialization and SFINAE, and how do we migrate those old techniques onto concepts now.
