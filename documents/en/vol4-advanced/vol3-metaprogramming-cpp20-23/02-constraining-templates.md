---
chapter: 13
cpp_standard:
- 20
description: 'Writing a concept into the signature is only the first step. What actually changes how you write generic code is that concepts take part in overload resolution. How subsumption (constraint entailment) picks the best fit among constrained overloads, and the trap hidden in atomic constraints.'
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Concepts: Putting Constraints in the Signature'
reading_time_minutes: 14
related:
- 'Concepts: Putting Constraints in the Signature'
- 'Requires expressions, in depth: the four kinds'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
- concepts
- 类型安全
title: 'Constraining Templates with Concepts: Subsumption and Overloading'
---
# Constraining Templates with Concepts: Subsumption and Overloading

In the last piece we put a concept into the signature and watched the error go from `enable_if` internal expansion to a plain "constraint not satisfied." But the thing that actually changes how you write generic code is something else. Concepts make template **overloading** workable. In C++17 and earlier, two function templates would clash unless their parameter counts or types were clearly different, and conditional overloading through `enable_if` was painful to write. With concepts, you can write a pile of same-named overloads with distinct constraints and let the compiler pick based on which constraint the argument satisfies. The rule for picking is called **subsumption** (constraint entailment), and it is the main character of this piece.

## First, untangle two same-named things: the requires clause and the requires expression

Before going further, we have to separate the two uses of the word `requires`, or everything below turns into a blur.

A **requires clause** (requires-clause) shows up after the template parameter list. Its job is "add a constraint to the template." We saw it as Form 2 in the last piece:

```cpp
template <typename T>
    requires Numeric<T>      // this whole line is a requires clause
T add(T a, T b) { return a + b; }
```

A **requires expression** (requires-expression) is an expression that evaluates to `bool` at compile time. It describes on the spot "what operations the type must provide." The next piece takes it apart. For now, a glance:

```cpp
requires(T t) { t + t; t.size(); }   // this is a requires expression, value is bool
```

The difference is that the clause is a syntactic position where you "set a rule" for the template, while the expression is the formula that describes the rule and produces a truth value. A clause often contains an expression, like `requires requires(T t){ t+t; }` (outer clause, inner expression). That's where the Form 4 double-`requires` from last piece comes from. This piece focuses on how the clause is used and how constraints participate in overloading. The expression gets its own piece next.

## Where you can attach a constraint

A concept's constraint is not only for free function templates. Function templates, class templates, member functions, even abbreviated `auto` parameters can all be constrained. A combined example:

```cpp
#include <concepts>

template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

// 1. function template
template <Numeric T>
T square(T x) { return x * x; }

// 2. class template: only instantiates for numeric types
template <Numeric T>
struct SafeNumber {
    T value;
    SafeNumber(T v) : value(v) {}
    // 3. a member function can pile on its own constraint
    SafeNumber& operator+=(Numeric auto other) {
        value += other;
        return *this;
    }
};

// 4. abbreviated syntax: constraint goes right before auto
Numeric auto half(Numeric auto x) { return x / 2; }
```

<OnlineCompilerDemo allow-run
  title="Where constraints can go: function, class, member, auto"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/constraints_everywhere.cpp"
  description="The Numeric constraint attached to a function template, a class template, a member function, and abbreviated auto. All four positions compile."
/>

Run it:

```text
square(4) = 16
square(2.5) = 6.25
SafeNumber(3) + 4 = 7
half(10) = 5
```

All four positions compile. Pay attention to the class template in particular. Once you constrain a class template, an instantiation like `SafeNumber<std::string>` that doesn't satisfy `Numeric` fails the constraint right at the declaration, instead of waiting to blow up when you use a member. The constraint pulls "this class is only for numeric types" forward to the moment of instantiation.

## Subsumption: the compiler picks overloads by constraint entailment

Now the main event. Let's write two same-named overloads, one looser and one tighter, and see what the compiler picks.

```cpp
#include <concepts>
#include <iostream>

template <typename T>
concept Animal = requires(T t) { t.eat(); };

template <typename T>
concept Dog = Animal<T> && requires(T t) { t.bark(); };   // Dog wants one more thing: bark()

void describe(Animal auto) { std::cout << "an animal\n"; }   // loose overload
void describe(Dog auto)    { std::cout << "a dog\n"; }       // tight overload

struct Cat { void eat() {} };
struct Pup { void eat() {} void bark() {} };

int main() {
    describe(Cat{});   // Cat only satisfies Animal
    describe(Pup{});   // Pup satisfies both Animal and Dog
}
```

This is the core excerpt. The full file (including the `Both/C` covered in the conjunction section below) is at [subsumption_overloads.cpp](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/examples/vol4/vol3-metaprogramming-cpp20-23/subsumption_overloads.cpp).

Run it (animal/dog part):

```text
an animal
a dog
```

`Cat` only satisfies `Animal`. There's no second overload to pick, so it goes to the loose one. `Pup` satisfies both `Animal` and `Dog`, but it goes to the **tight overload** `Dog`. That's subsumption at work. `Dog`'s requirement is `Animal<T> && bark requirement`, which folds all of `Animal`'s requirements in. We say **`Dog` subsumes `Animal`**. When both overloads match, the compiler picks the one with the tighter, more specific constraint.

What did this look like in the SFINAE era? You'd use `enable_if` with layer upon layer of "does this member function exist" probing, or reach for tag dispatch. The code doubled in size and still wasn't readable. Concepts hand "which is more specific" to the compiler, computed from the constraint relationship. That is the real reason they change how you write generic code.

## Two constraints that don't subsume each other: ambiguity

Subsumption only sorts things out when one constraint strictly entails the other. If two constraints are independent, neither containing the other, and some type satisfies both, the compiler can't pick.

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

`Swimmable` and `Flyable` don't subsume each other, `Duck` satisfies both, and the compiler has no reason to prefer one. It reports ambiguity. The fix is either to add a tight overload `Duck = Swimmable<T> && Flyable<T>` (it subsumes both, so it gets picked), or to write `act<ConcreteType>` explicitly at the call site. The key point: **subsumption only resolves ambiguity when there's a containment relationship. It can't resolve a peer-level tie.**

## Atomic constraints: the real unit that decides subsumption

How did `Dog` subsuming `Animal` get computed above? That brings us to **atomic constraints**. The compiler doesn't look at "which of the names `Dog` and `Animal` contains the other." It breaks each constraint down into minimal, indivisible atomic constraints and looks at set containment.

`concept Dog = Animal<T> && bark requirement`, after normalization, has the atomic constraint set `{ Animal<T>, bark requirement }`. `Animal`'s atomic constraint set is `{ Animal<T> }`. The former is a proper superset of the latter, so `Dog` subsumes `Animal`.

There's a trap here that gets everyone. Let's run it. Intuitively, writing `C2 = C1<T>`, handing one concept to another unchanged, feels like `C2` ought to be more specific than `C1`. Let's try:

```cpp
template <typename T> concept C1 = std::is_integral_v<T>;
template <typename T> concept C2 = C1<T>;          // just a rename

void g(C1 auto) { /* ... */ }
void g(C2 auto) { /* ... */ }

int main() { g(42); }   // int satisfies both C1 and C2
```

```text
atomic.cpp:14:15: error: call of overloaded 'g(int)' is ambiguous
```

Ambiguous. Why? Because `C2 = C1<T>`, after normalization, has the atomic constraint `C1<T>` itself, **identical** to `C1`'s atomic constraint. The two overloads' constraint sets are equal. Neither properly contains the other, neither subsumes, and it falls back to plain ambiguity. Renaming doesn't conjure up a "more specific" relationship. Subsumption wants a **proper superset** of atomic constraints. A name change doesn't affect that.

To make `C2` actually win, you have to give it an atomic constraint beyond `C1`. The `&&` combinator does exactly this:

```cpp
template <typename T> concept A = requires(T t){ t.a(); };
template <typename T> concept B = requires(T t){ t.b(); };
template <typename T> concept C = A<T> && B<T>;   // atomic constraints = { A<T>, B<T> }

void f(A auto) { /* ... */ }
void f(B auto) { /* ... */ }
void f(C auto) { /* ... */ }   // C subsumes A, and subsumes B

int main() { struct S{ void a(){} void b(){} } s; f(s); }
```

```bash
$ g++ -std=c++20 -Wall -Wextra conjunction.cpp -o conj && ./conj
C
```

`C`'s atomic constraint set `{ A<T>, B<T> }` is a superset of both `{ A<T> }` and `{ B<T> }`, so it subsumes `A` and `B`, and the compiler picks `C` out of the three. This spells out the real role of `&&`: it isn't "glue two constraints into a new one." It takes the atomic constraints from both sides and **unions** them into one set.

::: warning Constraints are compared by atoms, not by name
Subsumption compares the **atomic constraint set** after normalization, not the concept names. `C2 = C1<T>` doesn't make `C2` more specific than `C1`, because their atomic constraints are the same. If you want an overload to win, its atomic constraint set has to be a proper superset of the other, and the usual way to get there is to stack another constraint with `&&`. Remember this, and you won't get tangled in "the names are clearly different, why is it still ambiguous" when you write constrained overload families.
:::

<OnlineCompilerDemo allow-run
  title="Subsumption in full: animal/dog plus the conjunction case C"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/subsumption_overloads.cpp"
  description="Full run of subsumption_overloads.cpp: Cat goes to the loose overload, Pup to the tight one, and Both (satisfying A, B, and C) is picked as C."
/>

Full run output:

```text
an animal
a dog
C
```

## A trap: don't use a concept as is_same

`std::same_as` is a concept that leads people astray. You can write `template <std::same_as<int> T>` to pin `T` down to exactly `int`.

```cpp
template <std::same_as<int> T>
void only_int(T x) { /* ... */ }

only_int(42);       // fine
// only_int(3.14);  // fails to compile: double doesn't satisfy same_as<int>
```

It works, but it's usually bad design. If your function only takes `int`, writing an ordinary non-template function `void only_int(int x)` is clearer, simpler, and saves the compiler one instantiation. The real home for a "type equivalence" constraint like `same_as` is inside a template, requiring a relationship between **two parameters**, like `template <typename A, typename B> requires std::same_as<A, B>`, which says "A and B must be the same type." Using it to lock a single template parameter to one concrete type is the wrong tool.

With concepts in the signature and participating in overloading, you can write generic code that is both clear and able to dispatch. In the next piece we take apart the expression form of `requires`, the one most easily confused, which describes on the spot "what operations a type must provide." It's the foundation of every `requires(T t){ ... }` you've seen above.
