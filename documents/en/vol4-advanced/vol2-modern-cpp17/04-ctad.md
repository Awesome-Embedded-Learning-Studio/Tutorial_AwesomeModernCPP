---
chapter: 12
cpp_standard:
- 17
description: 'CTAD lets the compiler deduce a class template''s arguments from constructor parameters, collapsing std::pair&lt;int,double> p(1, 2.5) into std::pair p(1, 2.5). Covers implicit deduction guides, hand-written deduction guides, and the traps around parentheses vs braces and the narrowest viable type.'
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'Variadic Templates: Expanding Parameter Packs'
- 'Perfect Forwarding: Forwarding References and Reference Collapsing'
reading_time_minutes: 13
related:
- 'A Type-Safe any: A Capstone Project'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
- 类型安全
title: 'CTAD: Class Template Argument Deduction'
---
# CTAD: Class Template Argument Deduction

In the previous piece we worked through perfect forwarding, where the parameter type of a function template lines itself up automatically through `T&&` and reference collapsing. This piece turns in the other direction: can the arguments of a class template be deduced the same way? Before C++17, every use of a class template demanded its arguments in angle brackets — `std::pair<int, double> p(1, 2.5)`, `std::lock_guard<std::mutex> lk(m)`, `std::vector<int> v{1,2,3}`. The annoyance is that the compiler could already read those arguments off the constructor parameters: you pass in an `int` and a `double`, so the template arguments should be `int` and `double`, no need to write them twice. C++17's CTAD (Class Template Argument Deduction) is what closes that gap.

This piece works through where CTAD comes from, when implicit deduction is enough, when you have to write a deduction guide by hand, and a few traps that are easy to step into.

## Drop the pile of angle brackets: the basics of CTAD

Straight to the effect. This block leaves out every angle bracket, and the compiler still arrives at the right type:

```cpp
std::pair p(1, 2.5);                       // deduces pair<int, double>
std::pair p2(1, 2);                        // deduces pair<int, int>
std::tuple t(1, 2.5, "hi");                // deduces tuple<int, double, const char*>
std::vector v{1, 2, 3};                    // deduces vector<int>
std::mutex m;
std::lock_guard lk(m);                     // deduces lock_guard<std::mutex>
```

<OnlineCompilerDemo allow-run
  title="CTAD basics: pair/tuple/vector/lock_guard without the angle brackets"
  source-path="code/examples/vol4/vol2-modern-cpp17/basic_ctad.cpp"
  description="Static asserts confirm that the deduced types match what you would write by hand."
/>

Run it:

```text
p   = (1, 2.5)
t   = (1, 2.5, hi)
v   = {1, 2, 3}
所有 static_assert 通过,CTAD 推导结果与手写尖括号一致
```

`std::pair p(1, 2.5)` deduces `pair<int, double>`, and `std::pair p2(1, 2)` deduces `pair<int, int>`. Put the two side by side and you can read CTAD's disposition: where the template arguments come from tracks the types of the constructor arguments entirely. The `std::lock_guard` example is even more direct — it has only one template parameter, the mutex type, and you passed in a `std::mutex m`, so naturally it deduces `lock_guard<std::mutex>`. The angle brackets you used to write were pure redundancy.

## Where deduction comes from: implicit guides

CTAD doesn't come out of thin air. It rests on "deduction guides" — rules that tell the compiler "when you see this shape of constructor arguments, deduce this template instantiation." If you don't write one, the compiler generates one implicitly for every constructor. Here is the smallest example:

```cpp
template <typename T>
struct Box {
    T value;
    Box(T v) : value(v) {}
};

Box b(42);      // implicit guide Box(T) -> Box<T>; argument is int, deduces Box<int>
```

`Box` has a constructor `Box(T v)`, so the compiler synthesizes an implicit guide `Box(T) -> Box<T>` (what follows the arrow is the deduced template instantiation). You write `Box b(42)`, the compiler matches the argument `42` (type `int`) against this guide, `T = int`, and out comes `Box<int>`. The whole thing is equivalent to writing `Box<int> b(42)` by hand.

::: warning Implicit guides only see the constructor parameters
The only "clues" an implicit guide has are the types in the constructor signature. If a template parameter never shows up in the constructor parameters (say, a non-type parameter `N`), the implicit guide can't deduce it. That is exactly why `std::array` needs a hand-written guide, which we get to next.
:::

## A hand-written guide: how `array`'s `N` gets deduced

`std::array` has two template parameters: the element type `T` and the size `N`. `std::array a{1, 2, 3}` deduces `std::array<int, 3>`, but implicit guides can't pull that off. The reason is that `std::array` is an aggregate: it has no constructor at all that puts `N` into its parameter list, so the compiler can read nothing about `N` off the constructor signature. Let's reproduce this with our own type:

```cpp
template <typename T, std::size_t N>
struct NoGuide {
    T data[N];
    NoGuide(T v) { for (std::size_t i = 0; i < N; ++i) data[i] = v; }
};

NoGuide ng(42);   // T=int works, but N is what? Can't deduce it
```

The constructor `NoGuide(T v)` only mentions `T`; `N` is absent from the signature entirely. The implicit guide the compiler synthesizes is `NoGuide(T) -> NoGuide<T, N>`, but `N` has no source, so deduction fails. The heart of what GCC 16.1.1 prints is:

```text
error: class template argument deduction failed:
error: no matching function for call to 'NoGuide(int)'
    template argument deduction/substitution failed:
      couldn't deduce template parameter 'N'
```

How does the standard library's `std::array` get around this? With a hand-written deduction guide that fishes `N` out of the count of elements. Let's give our `MyArray` one:

```cpp
template <typename T, std::size_t N>
struct MyArray {
    T data[N];
    MyArray(const T (&arr)[N]) {
        for (std::size_t i = 0; i < N; ++i) data[i] = arr[i];
    }
};

// Hand-written guide: recover element type and size from a C-array reference
template <typename U, std::size_t N>
MyArray(const U (&)[N]) -> MyArray<U, N>;

int raw[] = {1, 2, 3, 4};
MyArray ma(raw);   // guide fires: MyArray<int, 4>
```

The syntax of a guide is `TemplateName(parameter pattern) -> Name<deduced arguments>`. The pattern here is `const U (&)[N]` — a reference to an array of length `N` holding `U`. Pass in `int raw[4]` and the compiler reads `U = int` and `N = 4` straight off the array type, so `MyArray ma(raw)` lands at `MyArray<int, 4>`. The non-type parameter `N`, which was hidden a moment ago, gets dug out of the array's size by this guide.

<OnlineCompilerDemo allow-run
  title="std::array's CTAD, plus a hand-written array-style deduction guide"
  source-path="code/examples/vol4/vol2-modern-cpp17/array_ctad.cpp"
  description="The library's std::array a{1,2,3} deduces array<int,3>; our MyArray with a hand-written guide recovers N from a C-array."
/>

Run it:

```text
std::array a: size=3  [0]=1  [2]=3
MyArray ma: 1 2 3 4
```

The guide the library actually writes for `std::array` is more elaborate (it uses a variadic pack with `common_type` to handle brace-enclosed initializer lists, which is why `std::array a{1,2,3}` works directly), but the underlying logic is the one above: since `N` can't be deduced from the constructor parameters, find another path that can see `N`, and write it into a guide.

## Writing a guide of your own

The most common use of a hand-written guide is to give a "part of the template arguments the user shouldn't have to care about" a default. Let's write a `Scaled<T, Scale>` where you only pass in a value at construction and the guide pins `Scale` to `1`:

```cpp
template <typename T, int Scale>
struct Scaled {
    T value;
    constexpr Scaled(T v) : value(v * Scale) {}
};

// Guide: only see T, pin Scale to 1
template <typename T>
Scaled(T) -> Scaled<T, 1>;

constexpr Scaled s(42);    // Scaled<int, 1>, value = 42
Scaled d(2.5);             // Scaled<double, 1>
Scaled<int, 10> big(5);    // specify Scale explicitly, value = 50
```

The guide `Scaled(T) -> Scaled<T, 1>` tells the compiler: when you see `Scaled(some value of type T)`, deduce `Scaled<T, 1>`, fixing `Scale` to `1`. If you want a different `Scale`, skip CTAD and write the angle brackets out; `Scaled<int, 10> big(5)` is unaffected by the guide. A guide provides the "most common default path"; it isn't the only entrance.

<OnlineCompilerDemo allow-run
  title="A hand-written guide: pin one template argument, keep the element type"
  source-path="code/examples/vol4/vol2-modern-cpp17/custom_deduction_guide.cpp"
  description="Scaled(T) -> Scaled<T,1> pins Scale to 1; Wrapper(T) -> Wrapper<T> keeps the element type (including const char*)."
/>

Run it:

```text
s.value=42
d.value=2.5
big.value=50
w.data=hello
```

That last one, `Wrapper w("hello")` deducing `Wrapper<const char*>`, works because the guide `Wrapper(T) -> Wrapper<T>` leaves `T` for the compiler to read off the argument; a string literal has type `const char*`, so `T` settles on that. Writing a guide is two steps: on the left of the arrow, what shape of constructor arguments you see; on the right, what template instantiation you deduce from it.

## Three traps that are easy to step into

CTAD gets comfortable fast, but a few traps are worth knowing up front.

**First, parentheses and braces mean different things.** This is the nastiest trap in CTAD, because it hooks directly into constructor overload resolution. The same `std::vector`, written two ways, does two completely different things:

```cpp
std::vector a(10, 0);     // (count, value): ten zeros
std::vector b{10, 0};     // {initializer_list}: two elements, 10 and 0
```

`a` has size 10 with every element 0; `b` has size 2 holding the values 10 and 0. The parentheses pick the `(count, value)` constructor; the braces prefer the `initializer_list` constructor. CTAD itself didn't change — what changed is "which constructor did the compiler pick," and the deduced result rides along with that.

**Second, initializer-list deduction takes the common type, not the first element's type.** This is something I guessed wrong at first, so it's worth saying plainly. Look at this:

```cpp
std::vector same_int{1, 2, 3};    // all int -> vector<int>
std::vector mix{1, 2.5};          // int+double -> vector<double>
```

`mix` deduces `vector<double>`, not `vector<int>`. The reason is that the `initializer_list<T>` guide for `std::vector` requires every element to fit one `T`, and the common type of `int` and `double` is `double` (promoting `int` to `double` doesn't narrow, the reverse would). So `T = double`. This "take the common type, no narrowing" rule is also why `std::pair p(1, 2.5)` deduces `pair<int, double>` rather than collapsing to a single type — pair's constructor deduces each argument independently and needs no common type, while vector's `initializer_list<T>` has only one `T` and must settle on a common value.

If you force in a pair of types that can't converge without narrowing, deduction fails. For example:

```cpp
std::vector bad{1, 2, 3, 100000000000LL};   // int and long long have no non-narrowing common type
```

GCC 16.1.1 reports:

```text
error: class template argument deduction failed:
error: no matching function for call to 'vector(int, int, int, long long int)'
```

`std::array a{1, 2.5}` fails for the same kind of reason, and it's even stricter than vector — array's guide requires all element types to match exactly, so `int` and `double` can't produce a single `T` at all.

**Third, copy-constructor deduction keeps the element type.** When you initialize one container from another that already exists, the deduced result follows the source:

```cpp
std::vector<int> src{1, 2, 3};
std::vector cpy(src);    // copy-constructor deduction, deduces vector<int>
```

This goes through the implicit guide for the copy constructor. The source is `vector<int>`, so the target deduces `vector<int>`. The difference from the braces rule above is that the argument here is already a `vector<int>` — its element type is fixed, so there's no "take the common type" step.

<OnlineCompilerDemo allow-run
  title="Parentheses vs braces, copy deduction vs initializer_list, common type without narrowing"
  source-path="code/examples/vol4/vol2-modern-cpp17/deduction_traps.cpp"
  description="By default it shows the three traps side by side; add -DNARROW_FAIL to reproduce the error when vector{int..., long long} can't deduce a common type."
/>

Run it:

```text
a (10,0): size=10  [0]=0  [9]=0
b {10,0}: size=2  [0]=10  [1]=0
cpy: size=3  [2]=3
mix {1, 2.5}: [0]=1  [1]=2.5  (类型是 vector<double>)
```

## Two boundaries, mentioned in passing

There are two more edges to CTAD; knowing they exist is enough, no need to dig deep.

The first is "non-deduced contexts." Certain positions the compiler simply won't use to reverse-deduce a template parameter, the most typical being a nested type like `TypeName::value_type`. If you write a guide like `Wrap(typename Wrap<X>::value_type) -> Wrap<X>`, the compiler won't walk the argument's `value_type` backward to recover `X`. In practice the implicit guides for the overwhelming majority of class templates are enough, and cases that need to work around a non-deduced context are rare.

The other is `explicit` constructors. `explicit` affects the "implicit conversion" path (for example, when a function parameter is `ExplicitSingle<int>` and you pass a bare `42`, it gets blocked), but it has no effect on the direct construction that CTAD performs. `ExplicitSingle es(42)` still deduces `ExplicitSingle<int>`; `explicit` only stops `42` from quietly becoming an `ExplicitSingle<int>`.

The next piece shifts focus off class templates and onto type erasure — how to hold a value of any type inside a `std::any` while keeping things as type-safe as possible. CTAD will show up once more there, helping us trim a few angle brackets.
