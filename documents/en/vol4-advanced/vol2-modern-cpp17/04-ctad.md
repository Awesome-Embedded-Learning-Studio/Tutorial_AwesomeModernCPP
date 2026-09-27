---
chapter: 12
cpp_standard:
- 17
description: 'CTAD lets the compiler deduce a class template''s arguments from its constructor''s arguments, collapsing verbose spellings like std::pair<int,double> p(1, 2.5) down to std::pair p(1, 2.5). This piece makes sense of implicit deduction guides, hand-written deduction guides, and the traps around parentheses vs braces and the narrowest viable type'
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'Variadic Templates: Expanding Parameter Packs'
- 'Perfect Forwarding: Forwarding References and Reference Collapsing'
reading_time_minutes: 13
related:
- 'Capstone Project: A Type-Safe any'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
- 类型安全
title: 'CTAD: Class Template Argument Deduction'
translation:
  source: documents/vol4-advanced/vol2-modern-cpp17/04-ctad.md
  source_hash: 508922d5235eb0a87f9248c4ea7771767e04b69dfc17593d4fb1ca539e58d060
  translated_at: '2026-09-26T03:29:05+00:00'
  engine: anthropic
  token_count: 4800
---
# CTAD: Class Template Argument Deduction

Last time we covered perfect forwarding, whose core was how a function template's parameter types adapt automatically through `T&&` and reference collapsing. This time we turn in another direction: can the parameters of a *class* template be handed to the compiler to deduce as well? Before C++17, every use of a class template made you spell the arguments out in angle brackets: `std::pair<int, double> p(1, 2.5)`, `std::lock_guard<std::mutex> lk(m)`, `std::vector<int> v{1,2,3}`. The annoying part is that the compiler could perfectly well deduce those arguments from the constructor arguments — what goes in is an `int` and a `double`, so the template arguments should be `int` and `double`; why make me write them out a second time? C++17's CTAD (Class Template Argument Deduction) exists to eliminate exactly this layer of duplication.

In this piece we walk through CTAD from start to finish: where the deduction comes from, when implicit deduction is enough, when you have to write a deduction guide by hand, and a few traps that are remarkably easy to step into.

## Ditching a Pile of Angle Brackets: Basic CTAD Usage

Straight to the effect. The code below drops every angle bracket, and the compiler still deduces the right types:

```cpp
std::pair p(1, 2.5);                       // deduces pair<int, double>
std::pair p2(1, 2);                        // deduces pair<int, int>
std::tuple t(1, 2.5, "hi");                // deduces tuple<int, double, const char*>
std::vector v{1, 2, 3};                    // deduces vector<int>
std::mutex m;
std::lock_guard lk(m);                     // deduces lock_guard<std::mutex>
```

<OnlineCompilerDemo allow-run
  title="Basic CTAD usage: pair/tuple/vector/lock_guard drop the angle brackets"
  source-path="code/examples/vol4/vol2-modern-cpp17/basic_ctad.cpp"
  description="static_assert verifies that the deduced types are exactly the same as the hand-written angle-bracket spellings."
/>

Output:

```text
p   = (1, 2.5)
t   = (1, 2.5, hi)
v   = {1, 2, 3}
所有 static_assert 通过,CTAD 推导结果与手写尖括号一致
```

`std::pair p(1, 2.5)` deduces `pair<int, double>`, and `std::pair p2(1, 2)` deduces `pair<int, int>`. Put the two side by side and you can already read CTAD's temperament: where the template arguments come from follows the types of the constructor arguments entirely. The `std::lock_guard` example is even more intuitive — it has exactly one template parameter, the mutex type. Pass a `std::mutex m` in, and what comes out is naturally `lock_guard<std::mutex>`; that old string of angle brackets was pure redundancy.

## Where the Deduction Comes From: Implicit Guides

CTAD is not conjured out of thin air; its basis is the **deduction guide** — a rule that tells the compiler "when you see this kind of constructor argument, deduce this kind of template argument". If you don't write one, the compiler implicitly generates one for every constructor anyway. Here is a minimal example:

```cpp
template <typename T>
struct Box {
    T value;
    Box(T v) : value(v) {}
};

Box b(42);      // implicit guide: Box(T) -> Box<T>, the argument is int, deduce Box<int>
```

`Box` has one constructor, `Box(T v)`, and from it the compiler implicitly generates the deduction guide `Box(T) -> Box<T>` (what follows the arrow is the deduced template instantiation). You write `Box b(42)`; the compiler matches the argument `42` (of type `int`) against this guide, gets `T = int`, and deduces `Box<int>`. The whole process is equivalent to you writing `Box<int> b(42)` by hand.

::: warning Implicit guides only look at the constructor's parameters
The "clues" an implicit guide gets are exclusively the types of the constructor's parameters. If some template parameter never appears in a constructor signature at all (a non-type parameter `N`, say), the implicit guides cannot deduce it. That is exactly why `std::array` needs a hand-written guide — we get to that right below.
:::

## Hand-Written Deduction Guides: How `array`'s `N` Gets Deduced

`std::array` has two template parameters: the element type `T` and the size `N`. `std::array a{1, 2, 3}` can deduce `std::array<int, 3>`, but implicit guides cannot pull this off. The reason is that `std::array` is an aggregate — it has no constructor of the "put N into the parameter list" kind at all, so the compiler sees no clue about `N` in any constructor signature. Let's imitate one ourselves to see this point clearly:

```cpp
template <typename T, std::size_t N>
struct NoGuide {
    T data[N];
    NoGuide(T v) { for (std::size_t i = 0; i < N; ++i) data[i] = v; }
};

NoGuide ng(42);   // T=int deduces fine, but what is N? No way to deduce it
```

Only `T` ever appears in the `NoGuide(T v)` constructor; `N` is completely absent from the signature. The compiler's implicitly generated guide is `NoGuide(T) -> NoGuide<T, N>`, but `N` has no source, so deduction fails outright. The core of what GCC 16.1.1 reports is this:

```text
error: class template argument deduction failed:
error: no matching function for call to 'NoGuide(int)'
    template argument deduction/substitution failed:
      couldn't deduce template parameter 'N'
```

How does the standard library's `std::array` get around this? With a hand-written deduction guide that fishes `N` out of "the number of elements". Let's equip our own `MyArray` with one:

```cpp
template <typename T, std::size_t N>
struct MyArray {
    T data[N];
    MyArray(const T (&arr)[N]) {
        for (std::size_t i = 0; i < N; ++i) data[i] = arr[i];
    }
};

// Hand-written deduction guide: recover the element type and size from a reference to a C array
template <typename U, std::size_t N>
MyArray(const U (&)[N]) -> MyArray<U, N>;

int raw[] = {1, 2, 3, 4};
MyArray ma(raw);   // the guide steps in: MyArray<int, 4>
```

The syntax of a guide is `TemplateName(parameter pattern) -> Name<deduced arguments>`. This guide's parameter pattern is `const U (&)[N]` — a reference to an array of `U` with length `N`. Pass an `int raw[4]` in, and the compiler simultaneously deduces `U = int` and `N = 4` from the array type, so `MyArray ma(raw)` ends up with `MyArray<int, 4>`. The non-type parameter `N`, hidden until now, gets dug out of the array's size by this guide.

<OnlineCompilerDemo allow-run
  title="CTAD for std::array, plus a hand-written array-style deduction guide of our own"
  source-path="code/examples/vol4/vol2-modern-cpp17/array_ctad.cpp"
  description="The standard library's std::array a{1,2,3} deduces array<int,3>; our own MyArray gets a hand-written guide that recovers N from a C array."
/>

Output:

```text
std::array a: size=3  [0]=1  [2]=3
MyArray ma: 1 2 3 4
```

The guide the standard library actually writes for `std::array` is more refined than this one (it uses a variadic pack together with `common_type` to handle braced initializer lists, which is why the `std::array a{1,2,3}` spelling works directly), but the underlying logic is the guide above: since `N` cannot be deduced from the constructor's arguments, find another path where `N` is visible, and write that into a guide.

## Writing a Deduction Guide of Your Own

The most common use of a hand-written guide is to give a default deduction to types where "part of the template arguments is something the user shouldn't have to worry about". Let's write a `Scaled<T, Scale>` that takes only a single value at construction, with `Scale` pinned to `1` in the guide:

```cpp
template <typename T, int Scale>
struct Scaled {
    T value;
    constexpr Scaled(T v) : value(v * Scale) {}
};

// Guide: sees only T, Scale is pinned to 1
template <typename T>
Scaled(T) -> Scaled<T, 1>;

constexpr Scaled s(42);    // Scaled<int, 1>, value = 42
Scaled d(2.5);             // Scaled<double, 1>
Scaled<int, 10> big(5);    // Scale given explicitly, value = 50
```

The guide `Scaled(T) -> Scaled<T, 1>` tells the compiler: when you see `Scaled(some value of type T)`, deduce `Scaled<T, 1>`, pinning `Scale` to `1`. If you want a different `Scale`, just bypass CTAD and write the angle brackets out in full — `Scaled<int, 10> big(5)` is not constrained by the guide. What a guide provides is "the most commonly used default path", not the only entrance.

<OnlineCompilerDemo allow-run
  title="A hand-written deduction guide: pin one template parameter, keep the element type"
  source-path="code/examples/vol4/vol2-modern-cpp17/custom_deduction_guide.cpp"
  description="Scaled(T) -> Scaled<T,1> pins Scale to 1; Wrapper(T) -> Wrapper<T> keeps the element type (including const char*)."
/>

Output:

```text
s.value=42
d.value=2.5
big.value=50
w.data=hello
```

The last one, `Wrapper w("hello")`, deduces `Wrapper<const char*>` because the guide `Wrapper(T) -> Wrapper<T>` leaves `T` for the compiler to deduce from the argument; a string literal has type `const char*`, so that is what `T` settles on. Writing a guide comes down to two steps: on the left of the arrow, write "what kind of constructor arguments the compiler sees"; on the right, write "what template instantiation to deduce from that".

## Three Traps That Are Easy to Step Into

CTAD speeds you up quickly once you get the hang of it, but a few pits are worth knowing about in advance.

**First, parentheses and braces do not mean the same thing.** This is the most treacherous part of CTAD, because it hooks directly into constructor overload resolution. The same `std::vector`, written two different ways, gives completely different results:

```cpp
std::vector a(10, 0);     // (count, value): ten 0s
std::vector b{10, 0};     // {initializer_list}: two elements, 10 and 0
```

`a` has size 10 with every element 0; `b` has size 2, holding the two values 10 and 0. The parentheses take the `(count, value)` constructor; the braces prefer to match the `initializer_list` constructor. CTAD itself hasn't changed — what changed is "which construction path the compiler picked", and the deduction result follows along differently.

**Second, initializer_list deduction takes the "common type", not the first element's type.** This is a place I guessed wrong at first, so it deserves a dedicated word. Look:

```cpp
std::vector same_int{1, 2, 3};    // all int -> vector<int>
std::vector mix{1, 2.5};          // int+double -> vector<double>
```

What `mix` deduces is `vector<double>`, not `vector<int>`. The reason is that `std::vector`'s `initializer_list<T>` guide requires every element to fit into the same `T`; the common type of `int` and `double` is `double` (`int` promotes to `double` without narrowing, while the other direction would narrow), so `T = double`. This "take the common type, do not narrow" rule also explains why `std::pair p(1, 2.5)` deduces `pair<int, double>` instead of squeezing everything into one type — pair's constructor deduces each argument independently and needs no common type, whereas vector's `initializer_list<T>` has only a single `T` and must take the common value.

If you force in a pair of types that cannot converge without narrowing, deduction fails. For example:

```cpp
std::vector bad{1, 2, 3, 100000000000LL};   // int and long long have no non-narrowing common type
```

GCC 16.1.1 reports:

```text
error: class template argument deduction failed:
error: no matching function for call to 'vector(int, int, int, long long int)'
```

`std::array a{1, 2.5}` has exactly the same ailment, and it is even stricter than vector — array's guide requires all element types to be exactly identical, so `int` and `double` simply cannot deduce a unique `T`.

**Third, copy-construction deduction preserves the element type.** Initialize one container from another that already exists, and the deduction result follows the source container:

```cpp
std::vector<int> src{1, 2, 3};
std::vector cpy(src);    // copy-construction deduction, deduces vector<int>
```

What runs here is the copy constructor's implicit guide: the source is `vector<int>`, so the target deduces to `vector<int>` as well. The difference from the brace rule above is that the copy constructor's argument is already a `vector<int>` whose element type is fixed — the "take the common type" step never happens.

<OnlineCompilerDemo allow-run
  title="Parentheses vs braces, copy deduction vs initializer_list, common type without narrowing"
  source-path="code/examples/vol4/vol2-modern-cpp17/deduction_traps.cpp"
  description="By default it demonstrates contrasting cases for the three traps; add -DNARROW_FAIL to reproduce the error where vector{int..., long long} fails to deduce a common type."
/>

Output:

```text
a (10,0): size=10  [0]=0  [9]=0
b {10,0}: size=2  [0]=10  [1]=0
cpy: size=3  [2]=3
mix {1, 2.5}: [0]=1  [1]=2.5  (类型是 vector<double>)
```

## A Few Edge Cases, Mentioned in Passing

CTAD has two more edge cases; knowing they exist is enough — no need to dig deep.

One is the "non-deduced context". There are positions the compiler naturally never uses to reverse-deduce template parameters; the most typical is a nested type like `TypeName::value_type`. If you write a guide such as `Wrap(typename Wrap<X>::value_type) -> Wrap<X>`, the compiler will not reverse-deduce `X` from the argument's `value_type`. Fortunately, the implicit guides of the overwhelming majority of class templates are already enough; situations that require routing around a non-deduced context are rare.

The other is `explicit` constructors. `explicit` affects the "implicit conversion" path (if a function parameter is `ExplicitSingle<int>` and you pass a bare `42`, it gets blocked); it has no effect on the direct construction CTAD performs. `ExplicitSingle es(42)` still deduces `ExplicitSingle<int>` just fine — `explicit` only makes sure `42` cannot quietly turn into an `ExplicitSingle<int>`.

In the next piece we shift our view from class templates to type erasure — how to use `std::any` to hold values of arbitrary types while keeping as much type safety as possible. CTAD will make another appearance there, helping us trim a few more angle brackets.
