---
chapter: 13
cpp_standard:
- 20
description: 'In C++20 the word requires is both a clause and an expression, and the pair is easy to confuse. This piece takes requires expressions apart: the four kinds of requirements (simple, type, compound, nested), how to define a concept with one, and the two traps of unevaluated context and hard errors on concrete types.'
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
title: 'Requires Expressions, In Depth: The Four Kinds of Requirements'
translation:
  source: documents/vol4-advanced/vol3-metaprogramming-cpp20-23/03-requires-expressions.md
  source_hash: 148e4f3fb9ed20c9cff89c289d697a4385c37c6d023dce4e7974478b2fcb5b91
  translated_at: '2026-09-26T04:31:57+00:00'
  engine: anthropic
  token_count: 2000
---
# Requires Expressions, In Depth: The Four Kinds of Requirements

In the last two pieces the word `requires` kept showing up — sometimes as a clause, sometimes as an expression. The two look identical but do different jobs. This piece takes them fully apart: what a requires expression is, what kinds of requirements it can contain, how to use one to describe on the spot "what operations a type must provide," and the two traps that confuse people most. Once you finish it, all those `requires(T t){ ... }` snippets from earlier will make sense — you will know exactly where each form comes from.

## Two kinds of `requires`: clause and expression

First, put the two same-named things side by side in one table. Everything below builds on it.

| | requires clause (requires-clause) | requires expression (requires-expression) |
|---|---|---|
| **What it is** | a syntactic slot that attaches a constraint to a template | an expression that evaluates to a bool at compile time |
| **What it looks like** | `requires Numeric<T>` | `requires(T t) { t.size(); }` |
| **Value** | not a value; it is a constraint declaration | a bool (true / false) |
| **Where it appears** | after the template parameter list | almost anywhere a bool is needed: inside a clause, inside a concept definition, inside a `static_assert` |

The star of the last piece — subsumption, overload dispatch — was the **clause**. The star of this piece is the **expression**. The two often work as a pair: put an expression inside a clause and you get the `requires requires(T t){ t+t; }` look, `requires` written twice in a row — the outer one is the clause, the inner one is the expression.

## The four kinds of requirements in a `requires` expression

Inside the braces of a requires expression `requires(params) { ... }` you can write four different kinds of "requirements." Let's define a `Container` concept that uses all four at once:

```cpp
#include <concepts>
#include <vector>

template <typename T>
concept Container = requires(T t) {
    // ① simple requirement: the expression must be valid, it just has to compile
    t.begin();
    t.end();

    // ② type requirement: this nested type must exist
    typename T::value_type;

    // ③ compound requirement: the expression is valid, and the return value satisfies a constraint
    { t.size() } -> std::convertible_to<std::size_t>;

    // ④ nested requirement: one more compile-time bool check nested inside
    requires std::integral<typename T::value_type>;
};

static_assert(Container<std::vector<int>>);   // vector<int> meets all four
static_assert(!Container<int>);               // int has no begin/end, fails the first one
```

These two `static_assert`s are compile-time assertions: if the code compiles, it shows `vector<int>` satisfies `Container` and `int` does not. Nothing needs to run.

Each of the four has its use. The simple requirement is the most common: `t.begin();` merely asks "can an object of type `T` call `begin()`?" — if it compiles, it passes. The type requirement `typename T::value_type;` checks whether a nested type exists, and it shows up constantly in trait checks on containers and iterators. The compound requirement `{ t.size() } -> std::convertible_to<std::size_t>;` binds "the expression is valid" and "the return type satisfies a constraint" into a single step — tighter than first checking whether the call compiles and then querying the return type with `decltype` in two separate steps. A compound requirement can also carry `noexcept`: `{ t.size() } noexcept -> std::convertible_to<std::size_t>;`, which additionally requires that the call not throw. The nested requirement `requires std::integral<...>;` lets you tuck one more concept judgment inside the expression — a good fit for "once the main requirements hold, this extra one must hold too."

One detail that is easy to miss: the fourth requirement of `Container` says `value_type` must be an integer type, and `std::integral<char>` is **true** (char belongs to the integer family — the same reason `integral<bool>` was true in the last piece). So `Container<std::string>` actually satisfies the concept: string's `value_type` is char, which clears the fourth requirement. If you only want containers whose `value_type` is exactly `int`, swap the fourth requirement for `std::same_as<typename T::value_type, int>`.

## A `requires` expression is a bool: no concept name needed

A requires expression evaluates to a bool, so it does not have to live inside a concept definition — anywhere you need a compile-time decision, you can use one directly.

```cpp
#include <string>

// Drop it straight into a static_assert, no concept defined first
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
  title="Using a requires expression as a bool"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/requires_expression.cpp"
  description="An inline requires expression dropped straight into if constexpr, deciding at compile time whether a type has empty(), with no concept defined first."
/>

Output:

```text
has empty()
no empty()
```

For a one-off check of "does this type have that operation," an inline requires expression is the cheapest tool. But note the tradeoff: an inline expression has no name, so it does not form a reusable atomic constraint. When we covered subsumption in the last piece, we said overload dispatch relies on named concepts to build entailment relations. If you want two overloads to dispatch by constraints, you need named concepts (`concept C = requires(...){...}`); inline expressions cannot subsume. So a check you use exactly once, in exactly one place, fits an inline expression; a requirement that must take part in overloading or be reused again and again should be lifted into a concept.

## Trap one: a `requires` expression is never evaluated

This is the most counterintuitive trap of all. The calls written inside a requires expression only **check whether they compile** — they are never actually executed. Let's run one and watch the evidence.

```cpp
#include <iostream>

int counter = 0;
int increment() {
    ++counter;
    std::cout << "[副作用] increment 被调用了\n";
    return 1;
}

template <typename T>
concept MentionsIncrement = requires(T t) {
    increment();   // only checks "is this call legal"; not evaluated, not executed
};

int main() {
    static_assert(MentionsIncrement<int>);   // satisfied: the increment() call is legal
    std::cout << "concept 求值完毕,counter = " << counter << "\n";
    increment();                              // this is the real call
    std::cout << "真正调用后,counter = " << counter << "\n";
}
```

<OnlineCompilerDemo allow-run
  title="A requires expression is not evaluated: the counter proof"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/unevaluated.cpp"
  description="The increment() call inside requires only checks legality without executing; after the concept evaluates, counter is still 0, until the real call in main."
/>

Output:

```text
concept 求值完毕,counter = 0
[副作用] increment 被调用了
真正调用后,counter = 1
```

Look closely at the `counter = 0` line. When `MentionsIncrement<int>` was evaluated, the `increment()` call inside the requires expression **never executed at all** — counter stays 0, and the side-effect line never printed. Only when `main` contains a real `increment();` statement does counter become 1.

A requires expression lives in an **unevaluated context**, same as `decltype` and `sizeof`. The compiler only cares whether the expressions inside are "type-legal": it generates no call code, let alone triggers any side effect. Beginners trip over this constantly — they write something inside a requires expression that "looks like it initializes" or "looks like it computes," assume it ran, when in fact nothing happened. Use requires expressions to probe a type's capabilities; to actually make code run, you still have to write it in an ordinary function body.

## Trap two: writing a concrete type directly gives a hard error

The second trap is sneakier, and it grows out of the first. Suppose we want to test "string does not have some method." The intuitive move is to make string the parameter of the requires expression:

```cpp
// The intuitive way: test the negative case with the concrete type string
static_assert(!requires(std::string s) { s.nope(); });   // string has no nope
```

```text
four_requirements2.cpp:17:44: error: 'std::string' has no member named 'nope'
```

What comes out is a hard error, not a graceful false. Why? Because for a **concrete type**, a requires expression is "evaluated immediately" — the compiler sees the concrete type `std::string s`, goes straight into string to look for `nope`, and errors out hard when it isn't found, never reaching the SFINAE machinery of "substitution failure yields false." To make it sting even more, `requires(int x) { x.foo(); }` reports `request for member 'foo' in 'x', which is of non-class type 'int'`, because a fundamental type like `int` cannot carry `.foo()` syntax at all — it fails at the parsing stage.

The fix is to keep the requires expression in a **template context**; the most common move is to wrap it in a concept:

```cpp
template <typename T> concept HasSize = requires(T t) { t.size(); };
template <typename T> concept HasNope = requires(T t) { t.nope(); };

static_assert(HasSize<std::string>);    // string has size -> true
static_assert(!HasNope<std::string>);   // string has no nope -> false, graceful this time
static_assert(!HasSize<int>);           // int has no size -> false
```

```bash
$ g++ -std=c++20 -Wall -Wextra neg_via_concept.cpp -o nvc && echo "全部断言通过"
全部断言通过
```

Once wrapped in a concept, `T` is a template parameter, and evaluating the requires expression takes the SFINAE-friendly path: a missing member simply comes out as false, not a hard error. So when you write test assertions, wrap negative cases in a named concept as a rule, and don't shove concrete types straight into a requires expression.

::: warning The two traps are two sides of one coin
"Not evaluated" and "hard error on concrete types" both trace back to when a requires expression gets evaluated. For a template parameter it is "deferred and SFINAE-friendly," so it executes nothing (it is not evaluated) and returns false on failure. For a concrete type it is "immediate," so it likewise executes nothing, but a failure turns into a hard error on the spot. Remember one rule: a requires expression only checks "would this compile," never executes; to make it return false gracefully, keep it in a template context (usually, wrapped as a concept).
:::

Put the three things together — the four kinds of requirements, the unevaluated context, the template context — and the most confusable word in C++20 is fully unpacked. The next piece looks at the compile-time power of templates from another direction: before concepts existed, how template metaprogramming (TMP) did compile-time computation and type deduction with specialization and SFINAE, and how to migrate those old techniques onto concepts today.
