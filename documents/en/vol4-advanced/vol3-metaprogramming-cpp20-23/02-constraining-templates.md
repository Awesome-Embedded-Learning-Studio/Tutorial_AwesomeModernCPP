---
chapter: 13
cpp_standard:
- 20
description: 'Putting a concept into the signature is only the first step; what really changes how generic code is written is that constraints now take part in overload resolution. How subsumption (constraint entailment) picks the best fit among several constrained overloads, plus the trap hidden in atomic constraints.'
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Concepts: Putting Constraints in the Signature'
reading_time_minutes: 14
related:
- 'Concepts: Putting Constraints in the Signature'
- 'Requires Expressions, In Depth: The Four Kinds of Requirements'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
- concepts
- 类型安全
title: 'Constraining Templates with Concepts: Subsumption and Overloading'
translation:
  source: documents/vol4-advanced/vol3-metaprogramming-cpp20-23/02-constraining-templates.md
  source_hash: aa0af0f3ea208a8c2be498ee400e396f796a575d78fcf83e0465f0fca9a52fc1
  translated_at: '2026-09-26T04:30:28+00:00'
  engine: anthropic
  token_count: 4600
---
# Constraining Templates with Concepts: Subsumption and Overloading

In the last piece we put concepts into signatures and watched error messages turn from an `enable_if` internal expansion into a single "constraint not satisfied". But the thing that truly changes how generic code is written is something else: concepts make template **overloading** practical. In C++17 and earlier, two same-named function templates would easily collide unless their parameter counts or types clearly differed, and conditional overloading via `enable_if` was painfully awkward to write. With concepts, you can write a family of same-named overloads with different constraints and let the compiler pick based on which constraint the argument satisfies. The picking rule is called **subsumption** (constraint entailment), and it is the protagonist of this piece.

## First, Tell the Two Same-Named Things Apart: the requires Clause and the requires Expression

Before going further, we have to separate the two uses of the word `requires`, or what follows will get blurrier and blurrier.

The **requires clause** (requires-clause) appears after the template parameter list; its job is "attach a constraint to the template". We saw it as form ② in the last piece:

```cpp
template <typename T>
    requires Numeric<T>      // this whole line is the requires clause
T add(T a, T b) { return a + b; }
```

A **requires expression** (requires-expression) is an expression that can be evaluated to a `bool` at compile time, describing "which operations a type must provide" right on the spot. The next piece takes it apart in full — here, just a glance:

```cpp
requires(T t) { t + t; t.size(); }   // this is a requires expression; its value is a bool
```

The difference between the two: the clause is the syntactic position where you "lay down the rules" for the template, while the expression is the formula that describes what the rules say and produces a truth value. The clause often stuffs an expression inside itself, as in `requires requires(T t){ t+t; }` (outer clause, inner expression) — that is where the two consecutive `requires` of form ④ in the last piece came from. This piece concentrates on how the clause is used and how constraints take part in overload resolution; the expression is left to the next piece.

## Where Constraints Can Go

Constraints from concepts are not limited to free function templates. Function templates, class templates, member functions, even abbreviated `auto` parameters — all of them can be constrained. One all-in-one example:

```cpp
#include <concepts>

template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

// ① function template
template <Numeric T>
T square(T x) { return x * x; }

// ② class template: instantiated only for numeric types
template <Numeric T>
struct SafeNumber {
    T value;
    SafeNumber(T v) : value(v) {}
    // ③ member functions can add constraints of their own
    SafeNumber& operator+=(Numeric auto other) {
        value += other;
        return *this;
    }
};

// ④ abbreviated syntax: the constraint goes directly in front of auto
Numeric auto half(Numeric auto x) { return x / 2; }
```

<OnlineCompilerDemo allow-run
  title="Where Constraints Can Go: function / class / member / auto"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/constraints_everywhere.cpp"
  description="The Numeric constraint applied to a function template, a class template, a member function, and abbreviated auto — all four positions compile."
/>

Output:

```text
square(4) = 16
square(2.5) = 6.25
SafeNumber(3) + 4 = 7
half(10) = 5
```

All four positions compile. The class template deserves a special word of caution: once you constrain a class template, an instantiation like `SafeNumber<std::string>` that does not satisfy `Numeric` fails the constraint right at the declaration, instead of waiting until some member is used to blow up. The constraint pulls "this class is for numeric types only" forward to the very moment of instantiation.

## Subsumption: The Compiler Picks Overloads by Constraint Entailment

Now for the main event. We write two same-named overloads, one with a looser requirement and one with a tighter one, and watch which one the compiler picks.

```cpp
#include <concepts>
#include <iostream>

template <typename T>
concept Animal = requires(T t) { t.eat(); };

template <typename T>
concept Dog = Animal<T> && requires(T t) { t.bark(); };   // Dog demands one more thing than Animal: bark()

void describe(Animal auto) { std::cout << "an animal\n"; }   // the wide overload
void describe(Dog auto)    { std::cout << "a dog\n"; }       // the narrow overload

struct Cat { void eat() {} };
struct Pup { void eat() {} void bark() {} };

int main() {
    describe(Cat{});   // Cat satisfies Animal only
    describe(Pup{});   // Pup satisfies both Animal and Dog
}
```

What is pasted here is the core part. The complete file (including the `Both/C` case the conjunction section below will cover) is at [subsumption_overloads.cpp](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/examples/vol4/vol3-metaprogramming-cpp20-23/subsumption_overloads.cpp).

Output (the animal/dog part):

```text
an animal
a dog
```

`Cat` satisfies only `Animal`; with no second overload to choose, it goes to the wide one. `Pup` satisfies both `Animal` and `Dog`, yet it is routed to the **narrow overload** `Dog`. This is subsumption at work: what `Dog` requires is `Animal<T> && bark requirement`, which takes everything `Animal` requires entirely inside it — we say **`Dog` subsumes `Animal`**. When both overloads match, the compiler picks the one with the tighter, more specific constraint.

What would this have looked like in the SFINAE era? `enable_if` paired with layer upon layer of "does this member function exist" probing, or tag dispatch brought in on top — several times the code, and still not necessarily readable. Concepts hand the question of "who is more specific" to the compiler, computed from constraint relationships. That is the fundamental way they change generic code.

## Two Constraints That Do Not Subsume Each Other: Ambiguity

Subsumption disambiguates for you only when one constraint strictly entails the other. If two constraints are independent, neither containing the other, and some type satisfies both at once, the compiler cannot pick one.

```cpp
template <typename T> concept Swimmable = requires(T t) { t.swim(); };
template <typename T> concept Flyable   = requires(T t) { t.fly(); };

void act(Swimmable auto) { /* ... */ }
void act(Flyable auto)   { /* ... */ }

struct Duck { void swim() {} void fly() {} };   // satisfies both

int main() { act(Duck{}); }   // neither subsumes the other
```

```text
ambiguity.cpp:12:8: error: call of overloaded 'act(Duck)' is ambiguous
```

`Swimmable` and `Flyable` do not entail each other, `Duck` satisfies both, and the compiler, having no reason to prefer either, reports an ambiguity outright. The ways out: add one more narrow overload with `Duck = Swimmable<T> && Flyable<T>` (it subsumes both at once and gets picked), or explicitly write `act<SomeConcreteType>` at the call site. The key is to understand: **subsumption only resolves ambiguity when a containment relation exists; it cannot resolve a contest between two peers**.

## Atomic Constraints: The Real Unit That Decides Subsumption

How was "`Dog` entails `Animal`" computed above? For that we need **atomic constraints**. The compiler does not look at "which of the two names `Dog` and `Animal` contains the other"; it decomposes the constraints into a pile of minimal, indivisible atomic constraints and then examines set containment.

After normalization, the atomic constraint set of `concept Dog = Animal<T> && bark requirement` is `{ Animal<T>, bark requirement }`. The atomic constraint set of `Animal` is `{ Animal<T> }`. The former is a proper superset of the latter, so `Dog` subsumes `Animal`.

Here is a pit that is absurdly easy to fall into; let's run one directly and look. Intuitively, a spelling like `C2 = C1<T>` — assigning one concept to another verbatim — feels like `C2` should be more specific than `C1`, right? Run it:

```cpp
template <typename T> concept C1 = std::is_integral_v<T>;
template <typename T> concept C2 = C1<T>;          // only the name changed

void g(C1 auto) { /* ... */ }
void g(C2 auto) { /* ... */ }

int main() { g(42); }   // int satisfies both C1 and C2
```

```text
atomic.cpp:14:15: error: call of overloaded 'g(int)' is ambiguous
```

Ambiguous. Why? Because after normalization, the atomic constraint of `C2 = C1<T>` is `C1<T>` itself — **exactly identical** to `C1`'s atomic constraint. The two overloads' constraint sets are equal; neither properly contains the other, neither subsumes the other, and we fall back to an ordinary ambiguity. Changing the name does not conjure a "more specific" relation out of thin air; subsumption demands **proper containment** of atomic constraint sets, and whether the name changed has no bearing on it.

For `C2` to genuinely win, you must give it an atomic constraint beyond `C1`. Combining with `&&` does precisely that:

```cpp
template <typename T> concept A = requires(T t){ t.a(); };
template <typename T> concept B = requires(T t){ t.b(); };
template <typename T> concept C = A<T> && B<T>;   // atomic constraints = { A<T>, B<T> }

void f(A auto) { /* ... */ }
void f(B auto) { /* ... */ }
void f(C auto) { /* ... */ }   // C subsumes A, and subsumes B too

int main() { struct S{ void a(){} void b(){} } s; f(s); }
```

```bash
$ g++ -std=c++20 -Wall -Wextra conjunction.cpp -o conj && ./conj
C
```

`C`'s atomic constraint set `{ A<T>, B<T> }` is a superset of both `{ A<T> }` and `{ B<T> }`, so it subsumes both `A` and `B`, and when offered all three candidates the compiler picked `C`. This lays bare `&&`'s real role: `&&` is not "gluing the constraints into a brand-new one"; it **unions** the atomic constraints on both sides into one and the same set.

::: warning Subsumption compares atoms, not names
Subsumption compares the **atomic constraint sets** after normalization, not the concepts' names. `C2 = C1<T>` does not make `C2` more specific than `C1`, because their atomic constraints are identical. For an overload to win, its atomic constraint set must be a proper superset of the other side's, and the most common way to get there is stacking on one more constraint with `&&`. Remember this one rule, and later, when you write a family of constrained overloads, you will not be led in circles by "the names are clearly different, so why is it still ambiguous".
:::

<OnlineCompilerDemo allow-run
  title="Subsumption, the Full Demo: animal/dog and the conjunction C"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/subsumption_overloads.cpp"
  description="A complete run of subsumption_overloads.cpp: Cat takes the wide overload, Pup the narrow one, and Both, satisfying A/B/C at once, is selected as C."
/>

Full output:

```text
an animal
a dog
C
```

## Trap: Don't Use a concept as an is_same

The `std::same_as` concept easily leads people astray. It lets you write `template <std::same_as<int> T>`, nailing `T` down to being exactly `int`.

```cpp
template <std::same_as<int> T>
void only_int(T x) { /* ... */ }

only_int(42);       // fine
// only_int(3.14);  // fails to compile: double does not satisfy same_as<int>
```

It runs, but this is usually not good design. If your function accepts only `int`, writing an ordinary non-template function `void only_int(int x)` directly is clearer and simpler, and it spares the compiler one more template instantiation. The place where "type equivalence" constraints such as `same_as` truly shine is stating a requirement about the relationship **between two parameters** inside a template — for example `template <typename A, typename B> requires std::same_as<A, B>`, constraining "A and B must be the same type". Taking it and locking a single template parameter down to some concrete type is using the wrong tool.

Concepts in the signature, concepts in overload resolution — once these two are in place, generic code can be written clearly and with dispatch power. In the next piece we take apart the expression form of `requires`, the one most easily confused: it describes "which operations a type must provide" on the spot, and it is the foundation of every `requires(T t){ ... }` spelling above.
