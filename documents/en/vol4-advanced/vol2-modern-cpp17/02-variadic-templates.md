---
chapter: 12
cpp_standard:
- 11
- 14
- 17
description: 'How variadic templates swallow any number of arguments — declaring parameter packs, counting with sizeof..., pattern expansion vs fold, three expansion styles compared, and the empty-pack pitfall.'
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'if constexpr: Compile-Time Branching'
- 'TMP Core Techniques: The World Before Concepts'
reading_time_minutes: 14
related:
- 'if constexpr: Compile-Time Branching'
- 'Perfect Forwarding: Forwarding References and Reference Collapsing'
- 'TMP Core Techniques: The World Before Concepts'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
- 编译期计算
title: 'Variadic Templates: Expanding Parameter Packs'
---
# Variadic Templates: Expanding Parameter Packs

In the previous piece we worked through `if constexpr`, which lets a template function pick a branch on a compile-time condition and discard the rest. This piece picks up a need that predates it and shows up even more often: letting a template accept **any number of arguments**. `std::make_unique<T>(arg1, arg2, arg3)`, `std::tuple<int, double, std::string>`, `std::printf("%d %d", a, b)`. Behind all of these sits the same machinery: variadic templates.

Variadic templates entered the standard in C++11, and the whole thing turns on one concept: the **parameter pack**, a placeholder that can hold zero to N types or values. "Holding them" is the easy part. "Getting them back out to use" is where the real difficulty lives. This piece works through the expansion mechanism: how `sizeof...` counts the elements, how **pattern expansion** processes them one by one and why it isn't the same thing as a fold, and the empty-pack pitfall that's easiest to trip over.

## What a parameter pack looks like

First, the declaration. There are two kinds of packs.

One is a **template parameter pack**, written in the template parameter list and marked with an ellipsis `...`:

```cpp
template <typename... Ts>   // Ts is a template parameter pack; matches any number of types
struct Tuple {};
```

The other is a **function parameter pack**, written in a function signature, its type usually the template pack above expanded into arguments:

```cpp
template <typename... Ts>
void f(Ts... args);   // args is a function parameter pack; receives any number of arguments
```

`Ts...` here means "take each type in the pack `Ts` and use it, in turn, as a parameter type." When you call `f(1, 2.5, "hi")`, `Ts` is deduced as `int, double, const char*`, and `args` is those three arguments. Note that `Ts` is a pack of types and `args` is a pack of values; they correspond one to one.

Counting the elements is the first common need. `sizeof...(pack)` gives you the number of elements in the pack at compile time:

<OnlineCompilerDemo allow-run
  title="sizeof... counts the pack; pattern expansion calls each element of a heterogeneous pack independently"
  source-path="code/examples/vol4/vol2-modern-cpp17/pack_sizeof_and_print.cpp"
  description="sizeof...(args) returns the number of elements as a compile-time constant; pattern expansion print_one(args)... calls each element independently, with each element's type deduced on its own."
/>

Run it:

```text
sizeof... 数包大小:
  pack_size():             0
  pack_size(1):            1
  pack_size(1,2.5,"hi"):  3

模式展开(异类型包,每个元素类型独立推导):
  [i] 1
  [d] 2.5
  [A3_c] hi

逗号 fold 同样效果:
  (fold 写法)
  [i] 1
  [d] 2.5
  [A3_c] hi
```

`sizeof...(args)` returns a `size_t`, and it's a constant expression, so `static_assert(pack_size(1, 2, 3) == 3)` passes at compile time. With the count in hand, we can move on to expansion.

## Pattern expansion: do the same thing to each element

Pattern expansion is the core mechanism of parameter packs, and it's a different thing from a fold. People conflate the two, so let's pull them apart.

What's a pattern expansion? When you write `f(args)...`, the `f(args)` is a **pattern**, and the ellipsis `...` means "apply this pattern once to each element of the `args` pack, and string the results together." The compiler expands `print_one(args)...` (say `args` has three elements `a0, a1, a2`) into `print_one(a0), print_one(a1), print_one(a2)`. Each element is expanded **independently**, and that's exactly why it handles heterogeneous packs: `print_one` is a function template, instantiating `print_one<int>` for `a0`, `print_one<double>` for `a1`, with each element's type deduced on its own, none of them touching the others.

A fold is different. `(args + ...)` folds the whole pack with one operator, which means all the elements have to fit into a single expression. The product of a pattern expansion is "a sequence of independent calls"; the product of a fold is "one value." In the example above the two styles happen to produce the same output, but the mechanism differs: `print_one(args)...` is three independent calls, while the comma fold `(print_one(args), ...)` glues three calls into one expression with the comma operator.

Here's a wall beginners run into: **pattern expansion can't sit directly in statement position.** You might instinctively write:

```cpp
template <typename... Ts>
void print_all(Ts... args) {
    print_one(args)...;   // won't compile
}
```

GCC 16.1.1 prints:

```text
error: expected ';' before '...' token
   2 |     print_all(Ts... args) {
note: parameter packs not expanded with '...':
   3 |     print_one(args)...;
```

The ellipsis has to attach to a legal context: a function argument list, an initializer list, a comma expression, a base-class list, or a template argument list. The classic C++11 trick was to stuff the pattern into an array initializer list and use a comma expression to chain the side effects:

```cpp
template <typename... Ts>
void print_all(const Ts&... args) {
    using expand_t = int[];
    (void)expand_t{0, (print_one(args), 0)...};   // the old way
}
```

It looks convoluted, but the idea is plain. `(print_one(args), 0)` is a comma expression: it runs `print_one(args)` for its side effect and then evaluates to `0`. The whole `{0, (print_one(args), 0)...}` expands into `{0, (print_one(a0), 0), (print_one(a1), 0), (print_one(a2), 0)}`, initializing a temporary `int[]` array, with the side effect of printing each element. The leading `0` guards against an empty pack (an empty pack would make the array zero-sized, which is illegal). The `(void)` tells the compiler "I know this array is unused, don't warn."

Come C++17, the comma fold collapses this into a single line: `(void)((print_one(args), ...));`. So for new code you'll usually hand pattern-expansion work to a fold. But when you read old libraries or C++11-era code, that array-initializer trick is everywhere, and you have to recognize it.

## Where pattern expansion can appear

There's a fixed list of places a pattern expansion can appear. This table is enough to remember:

| Location | Example | Expands into |
|---|---|---|
| Function argument list | `f(args)...` | `f(a0), f(a1), ...` a sequence of arguments |
| Initializer list | `{args...}` or `{f(args)...}` | a sequence of init elements |
| Comma expression / fold | `(f(args), ...)` | one expression glued with commas |
| Base-class list | `struct D : Bases... {};` | a sequence of base classes |
| Template argument list | `std::tuple<Ts...>` | a sequence of template arguments |
| Lambda capture | `[args...] {}` | capture the whole pack |

The last row is particularly useful: the perfect-forwarding pack is pattern expansion in a function argument list, which we'll look at next.

## Three expansion styles compared

With the mechanism in hand, let's land on a concrete need: summing any number of arguments. The same need has three very different answers across C++11 and C++17. Lining them up shows you how this machinery evolved.

**Style one: template recursion plus a terminating overload (C++11).** This was the original canonical form for variadic templates. Two function templates: a recursive one that peels off the first argument, and a terminator that matches the "only one argument left" case.

```cpp
template <typename T>
constexpr T sum_rec(T first) {
    return first;   // terminator: only one left, return it
}

template <typename T, typename... Rest>
constexpr T sum_rec(T first, Rest... rest) {
    return first + sum_rec(rest...);   // peel the first, recurse on the rest
}
```

The call chain for `sum_rec(1, 2, 3)` is `sum_rec(1, 2, 3)` → `1 + sum_rec(2, 3)` → `1 + (2 + sum_rec(3))`. That last `sum_rec(3)` matches the single-argument terminator, and recursion unwinds. It works, but it needs two functions, the boilerplate is heavy, and it can't handle an empty pack (an empty pack matches neither the single-argument overload nor the variadic one, so it fails to compile).

**Style two: `if constexpr` termination (C++17).** The `if constexpr` from the previous piece is exactly what's needed. One function body uses a compile-time branch on `sizeof...(rest) == 0` as the termination condition, fusing the recursive and terminating versions.

```cpp
namespace detail {
template <typename T, typename... Rest>
constexpr auto sum_ifc_impl(T first, Rest... rest) {
    if constexpr (sizeof...(rest) == 0) {
        return first;   // compile-time branch: zero left, terminate
    } else {
        return first + sum_ifc_impl(rest...);
    }
}
}   // namespace detail

template <typename... Args>
constexpr auto sum_ifc(Args... args) {
    if constexpr (sizeof...(args) == 0) {
        return 0;   // outer layer handles the empty pack
    } else {
        return detail::sum_ifc_impl(args...);
    }
}
```

Style two is cleaner than style one: one function body holds both the recursion and the termination, no separate terminator overload. More importantly, the outer `if constexpr (sizeof...(args) == 0)` lets it handle an empty pack (which style one cannot). The cost is that the helper has to be split out, because inside `if constexpr` you need to talk about "the first argument" and "the rest of the pack" separately, and an empty pack has no first argument to take.

**Style three: fold (C++17).** One line, no recursion at all.

```cpp
template <typename... Ts>
constexpr auto sum_fold(Ts... ts) {
    return (ts + ...);   // unary right fold
}
```

`(ts + ...)` folds the whole pack with `+`. The four forms of fold we covered in detail in vol3-04 last time; we won't repeat them here. The one thing to say: a fold is a special case of pattern expansion, one that glues a sequence of patterns into a single expression with an operator, rather than producing "a sequence of independent calls" the way general pattern expansion does.

All three styles produce the same result:

<OnlineCompilerDemo allow-run
  title="Three sum styles compared: recursion + terminator / if constexpr / fold"
  source-path="code/examples/vol4/vol2-modern-cpp17/recursion_vs_ifconstexpr_vs_fold.cpp"
  description="The same summing need in three styles: sum_rec / sum_ifc / sum_fold all agree; style two handles an empty pack, styles one and three do not."
/>

Run it:

```text
sum_rec(1,2,3,4,5):  15
sum_ifc(1,2,3,4,5):  15
sum_fold(1,2,3,4,5): 15

空包处理:
  sum_ifc(): 0   (if constexpr 终止,空包返回 0)

static_assert 全过:三种写法结果一致
```

How to choose? For new code, default to a fold: it's the shortest and most direct, and it covers most "do a binary operation across a pack" needs. When you need to do a different thing to each element (say, call a type-dependent function on each), use pattern expansion paired with a comma fold. When you need to handle an empty pack, or the termination logic isn't just "return an initial value," reach for `if constexpr`. Style one's recursion-plus-terminator is rarely written today, but it's still the prevailing style in C++11-era libraries (including a lot of standard library implementations), so you have to be able to read it.

## The perfect-forwarding pack

One of the most common and most elegant uses of pattern expansion is the perfect-forwarding pack. The next piece is dedicated to the mechanism of forwarding references and reference collapsing; here we just look at what the pack side looks like.

`std::make_unique<T>(args...)` is the classic example: it takes any number of arguments and forwards their value categories, untouched, to `T`'s constructor. An lvalue coming in has to leave as an lvalue; an rvalue coming in has to leave as an rvalue; nothing can quietly turn into a copy along the way. `std::forward<Args>(args)...` is the pattern expansion that does this:

```cpp
template <typename T, typename... Args>
std::unique_ptr<T> make_tracked(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
```

`std::forward<Args>(args)...` expands into `std::forward<A0>(a0), std::forward<A1>(a1), ...`. Each argument is forwarded under its own template parameter `Ai`, which is a direct payoff of pattern expansion's "each element independent" property. A pack of arguments can be some lvalues and some rvalues, each keeping its own value category, none disturbing the others. Note that the `&&` in `Args&&...` is not an rvalue reference; it's a forwarding reference, and only together with `std::forward` does it preserve the value category. The next piece works through that mechanism in full.

<OnlineCompilerDemo allow-run
  title="std::forward<Args>(args)... forwards the pack, lvalue/rvalue each keeping its category"
  source-path="code/examples/vol4/vol2-modern-cpp17/forward_pack.cpp"
  description="relay takes any number of forwarding references; sink has a const& overload and an && overload. Passing an lvalue picks the const& overload; passing an rvalue picks the && overload. make_tracked forwards to the string constructor."
/>

Run it:

```text
传一个具名对象(lvalue):
  Tracked() 默认构造
  -> sink(const Tracked&) 收到 lvalue

传一个临时对象(rvalue):
  Tracked() 默认构造
  -> sink(Tracked&&)      收到 rvalue

工厂转发给 string 构造函数("hello", 2):取前 2 个字符
  *p = "he"
```

The named object `t` is an lvalue, and after `std::forward` it reaches `sink` through the `const Tracked&` overload; the temporary `Tracked{}` is an rvalue, reaching `sink` through the `Tracked&&` overload. The value category survives, which is exactly what the `std::forward<Args>(args)...` pattern expansion does.

## The empty-pack pitfall: folds blow up, if constexpr doesn't

Finally, a counterintuitive pitfall that connects this piece to the previous one.

A unary fold over an **empty pack** is ill-formed. When `ts` has no elements at all, the compiler has nothing to fold with `+`, and the standard says this is a compile error. GCC's message names it directly:

```text
error: fold of empty expansion over operator+
   return (ts + ...);   // 空包调用时会编译失败
```

The standard opens a back door for only three operators: a unary fold of `&&` over an empty pack is `true`, `||` is `false`, and the comma operator is `void()`. Other operators (`+`, `*`, `|`, and so on) have no default for an empty pack and are all ill-formed. This rule came up in vol3-04 when we covered the four fold forms; here we see its real consequence on the argument side.

But `if constexpr` termination is fine with an empty pack. The outer `if constexpr (sizeof...(args) == 0)` steers the empty pack, at compile time, into the "return 0" branch, never reaching any fold at all. That's the extra capability style two has over style three: it handles the zero-argument case gracefully.

<OnlineCompilerDemo allow-run
  title="Empty-pack pitfall: compiles by default with if constexpr termination, -DEMPTY_FOLD reproduces the fold error"
  source-path="code/examples/vol4/vol2-modern-cpp17/empty_pack_pitfall.cpp"
  description="By default sum_ifc() on an empty pack goes through the if constexpr termination branch and compiles. Add -DEMPTY_FOLD to switch to the unary fold version, where the empty-pack call triggers a fold of empty expansion compile error."
/>

Run it (default, with if constexpr termination):

```text
sum_ifc():      0   (空包走 if constexpr 终止分支)
sum_ifc(1,2,3): 6

默认演示通过。加 -DEMPTY_FOLD 复现空包 fold 的编译报错。
```

With `-DEMPTY_FOLD`, switching to the fold version, the empty-pack call fails to compile:

```text
error: fold of empty expansion over operator+
   return (ts + ...);   // 空包调用时会编译失败
```

So when you write a variadic function and the caller might pass zero arguments, a fold is no longer the first choice. Use `if constexpr` with an empty-pack branch, or use a binary fold with an initial value `(0 + ... + ts)` (the initial `0` gives the empty pack something to fold). This pitfall is especially easy to hit when writing a general library, because "can it be called with an empty pack" is usually up to the caller, not up to you.

---

That's the core of variadic templates: declaring parameter packs, counting with `sizeof...`, processing each element with pattern expansion, folding with fold, and the empty-pack boundary. What really makes the machinery shine is perfect forwarding, where the `std::forward<Args>(args)...` pattern expansion is the cornerstone of `make_unique`, `emplace_back`, and `tuple` construction. The next piece goes inside perfect forwarding, to see how the `Args&&` forwarding reference preserves the value category, and the reference collapsing rules behind it.
