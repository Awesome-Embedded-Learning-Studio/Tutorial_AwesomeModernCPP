---
chapter: 12
cpp_standard:
- 11
- 14
- 17
description: 'How variadic templates swallow any number of arguments — declaring parameter packs, counting with sizeof..., how pattern expansion differs from folds, three expansion styles compared side by side, and the empty-pack pitfall.'
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
translation:
  source: documents/vol4-advanced/vol2-modern-cpp17/02-variadic-templates.md
  source_hash: 17e3fd10ae4d2d40f5f2fd2db5416b67b1b18499630eb6ae9bff07ec32eed2bd
  translated_at: '2026-09-26T03:08:35+00:00'
  engine: anthropic
  token_count: 7000
---
# Variadic Templates: Expanding Parameter Packs

Last piece we worked `if constexpr` all the way through: it lets a template function pick a branch on a compile-time condition, and the discarded branch is never instantiated. This piece picks up a need that is older and even harder to dodge — letting one template swallow **any number** of arguments. `std::make_unique<T>(arg1, arg2, arg3)`, `std::tuple<int, double, std::string>`, `std::printf("%d %d", a, b)`: behind these APIs sits the same machinery, the variadic template.

Variadic templates entered the standard with C++11, and the core is a single concept: the **parameter pack** — a placeholder that can hold anywhere from zero to N types or values. Getting things in is the easy part; getting them out and using them is where the real difficulty lives. This piece makes the pack's expansion machinery clear: how `sizeof...` counts elements, how **pattern expansion** processes them one by one, how it differs from a fold, and the empty-pack pitfall that is easiest to step in.

## What a parameter pack looks like

Start with the declarations. There are two kinds of parameter packs.

One is the **template parameter pack**, written in the template parameter list and marked with the ellipsis `...`:

```cpp
template <typename... Ts>   // Ts is a template parameter pack; it can match any number of types
struct Tuple {};
```

The other is the **function parameter pack**, which lives in a function signature; its types are usually that template parameter pack from above, expanded out into arguments:

```cpp
template <typename... Ts>
void f(Ts... args);   // args is a function parameter pack; it can receive any number of arguments
```

Here `Ts...` means "take each type in the pack `Ts`, in order, and use it as a parameter type." Call `f(1, 2.5, "hi")` and `Ts` is deduced as `int, double, const char*`, with `args` being those three arguments. Note that `Ts` is a pack of types while `args` is a pack of values, and the two correspond one to one.

Counting the elements is the first and most common need. `sizeof...(pack)` hands you the number of elements in the pack at compile time:

<OnlineCompilerDemo allow-run
  title="sizeof... counts the pack; pattern expansion calls per element with independent types"
  source-path="code/examples/vol4/vol2-modern-cpp17/pack_sizeof_and_print.cpp"
  description="sizeof...(args) returns the number of elements in the pack (a compile-time constant); the pattern expansion print_one(args)... makes an independent call for each element, with each element's type deduced on its own."
/>

Output:

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

`sizeof...(args)` returns a `size_t`, and it is a constant expression, so `static_assert(pack_size(1, 2, 3) == 3)` passes at compile time. With the count in hand, we can move on to expansion.

## Pattern expansion: do the same thing to every element

Pattern expansion is the most central mechanism of parameter packs, and it is a different thing from a fold — plenty of people blur the two together, so let's pull them apart here.

What is pattern expansion? When you write `f(args)...`, that `f(args)` part is a **pattern**, and the ellipsis `...` says "apply this pattern once to every element of the `args` pack, then stitch the results together in order." The compiler expands `print_one(args)...` (say `args` holds three elements `a0, a1, a2`) into `print_one(a0), print_one(a1), print_one(a2)`. Every element is expanded **independently**, and that is exactly how it copes with heterogeneous packs: `print_one` is a function template, instantiated as `print_one<int>` for `a0` and `print_one<double>` for `a1`, each element's type deduced on its own, none of them interfering with the others.

A fold is different. `(args + ...)` folds the whole pack up with a single operator, which demands that all the elements fit into one expression. The product of pattern expansion is "a sequence of independent calls"; the product of a fold is "one value." In the example above the two spellings have the same effect, but the machinery differs: `print_one(args)...` is three independent calls, while the comma fold `(print_one(args), ...)` glues the three calls into a single expression with the comma operator.

Here is the wall beginners run into: **a pattern expansion cannot sit directly in statement position**. You might instinctively write:

```cpp
template <typename... Ts>
void print_all(Ts... args) {
    print_one(args)...;   // won't compile
}
```

GCC 16.1.1 spits out:

```text
error: expected ';' before '...' token
   2 |     print_all(Ts... args) {
note: parameter packs not expanded with '...':
   3 |     print_one(args)...;
```

The ellipsis has to attach to a legal context: a function argument list, an initializer list, a comma expression, a base class list, a template argument list. In the C++11 era the most common construction was to stuff the pattern into an array initializer list and let comma expressions chain the side effects together:

```cpp
template <typename... Ts>
void print_all(const Ts&... args) {
    using expand_t = int[];
    (void)expand_t{0, (print_one(args), 0)...};   // the old way
}
```

This looks convoluted, but the principle is plain. `(print_one(args), 0)` is a comma expression: it runs `print_one(args)` to print, then evaluates to `0`. The whole `{0, (print_one(args), 0)...}` expands into `{0, (print_one(a0), 0), (print_one(a1), 0), (print_one(a2), 0)}`, initializing a temporary `int[]` array, and the side effect is the printing, one element after another. That leading `0` guards against the empty pack (an empty pack would leave the array with zero size, which is not legal). The `(void)` tells the compiler "I know this array goes unused, don't warn."

Come C++17, the comma fold cleans this up completely: `(void)((print_one(args), ...));` and one line finishes the job. So today, writing new code, most pattern-expansion work is handed to folds. But when you read older libraries, code from the C++11 era, the array-initializer trick is everywhere — you have to recognize it on sight.

## Where pattern expansion can appear

The places a pattern expansion can appear form a fixed list; memorize this table and you are covered:

| Where | Example syntax | Expands to |
|---|---|---|
| Function argument list | `f(args)...` | a list of arguments `f(a0), f(a1), ...` |
| Initializer list | `{args...}` or `{f(args)...}` | a list of initializer elements |
| Comma expression / fold | `(f(args), ...)` | one expression stitched together with commas |
| Base class list | `struct D : Bases... {};` | a list of base classes |
| Template argument list | `std::tuple<Ts...>` | a list of template arguments |
| Lambda capture | `[args...] {}` | capture the whole pack |

That last row is especially useful: the perfectly forwarded pack is exactly pattern expansion used in a function argument list, and we will look at it in a moment.

## Three ways to expand, compared

Machinery covered, let's land on a concrete task: summing any number of arguments. For that one task, C++11 and C++17 offer three very different spellings; set them side by side and you can watch this machinery evolve.

**Style one: template recursion + a terminating overload (C++11)**. This is the original canonical form of variadic templates. It takes two function templates: a recursive one that peels off the first argument, and a terminating one that matches the "only one argument left" case.

```cpp
template <typename T>
constexpr T sum_rec(T first) {
    return first;   // termination: only one left, return it directly
}

template <typename T, typename... Rest>
constexpr T sum_rec(T first, Rest... rest) {
    return first + sum_rec(rest...);   // peel off the first argument, recurse on the rest
}
```

The call chain of `sum_rec(1, 2, 3)` is `sum_rec(1, 2, 3)` → `1 + sum_rec(2, 3)` → `1 + (2 + sum_rec(3))`; that final `sum_rec(3)` matches the single-argument terminating overload, and only then does the recursion unwind. This style runs, but it takes two functions, the boilerplate is heavy, and the empty pack is unhandled (an empty pack matches neither the single-argument overload nor the multi-argument one — compilation fails).

**Style two: `if constexpr` termination (C++17)**. The `if constexpr` we just picked up in the last piece fits right in. One function body uses a compile-time branch testing `sizeof...(rest) == 0` as the termination condition, so the recursive version and the terminating version merge into one.

```cpp
namespace detail {
template <typename T, typename... Rest>
constexpr auto sum_ifc_impl(T first, Rest... rest) {
    if constexpr (sizeof...(rest) == 0) {
        return first;   // compile-time branch: zero left means terminate
    } else {
        return first + sum_ifc_impl(rest...);
    }
}
}   // namespace detail

template <typename... Args>
constexpr auto sum_ifc(Args... args) {
    if constexpr (sizeof...(args) == 0) {
        return 0;   // the outer layer handles the empty pack
    } else {
        return detail::sum_ifc_impl(args...);
    }
}
```

Style two is cleaner than style one: recursion and termination live in one function body, and there is no separate terminating overload to write. More importantly, that outer `if constexpr (sizeof...(args) == 0)` lets it handle the empty pack (style one cannot). The cost is one extra helper layer, because inside the `if constexpr` we need to touch both "the first argument" and "the rest of the pack", and an empty pack has no first argument to take.

**Style three: fold (C++17)**. One line wraps it up — not even recursion needed.

```cpp
template <typename... Ts>
constexpr auto sum_fold(Ts... ts) {
    return (ts + ...);   // unary right fold
}
```

`(ts + ...)` folds the whole pack up with `+`. We walked through all four fold forms in detail in vol3-04, so no repetition here — just one sentence: a fold is a special case of pattern expansion. It stitches a sequence of patterns into one expression with an operator, instead of producing "a sequence of independent calls" the way general pattern expansion does.

All three styles produce exactly the same results:

<OnlineCompilerDemo allow-run
  title="Three summation styles compared: recursion + terminating overload / if constexpr termination / fold"
  source-path="code/examples/vol4/vol2-modern-cpp17/recursion_vs_ifconstexpr_vs_fold.cpp"
  description="Three spellings of the same summation task; sum_rec / sum_ifc / sum_fold agree on the results. Style two handles the empty pack; styles one and three both fail on it."
/>

Output:

```text
sum_rec(1,2,3,4,5):  15
sum_ifc(1,2,3,4,5):  15
sum_fold(1,2,3,4,5): 15

空包处理:
  sum_ifc(): 0   (if constexpr 终止,空包返回 0)

static_assert 全过:三种写法结果一致
```

How to choose? New code defaults to the fold: it is the shortest and most direct, and it covers the vast majority of "apply one binary operation to the whole pack" needs. When each element needs something different done to it (say, calling a type-specific function per element), use pattern expansion paired with a comma fold. When you must handle the empty pack, or the termination logic is more than "return an initial value", use `if constexpr`. Style one's recursion + terminating overload is essentially never written today, but reading C++11-era libraries (including plenty of standard library implementations) is still the mainstream — you have to be able to read it.

## The pack in perfect forwarding

One of the most everyday, and most brilliant, uses of pattern expansion is the perfectly forwarded pack. Next piece we will cover forwarding references and reference collapsing in dedicated detail; here we just look at what the pack side of it looks like.

`std::make_unique<T>(args...)` is the classic example: it receives any number of arguments and forwards their value categories untouched to `T`'s constructor. An lvalue coming in must go out an lvalue, an rvalue coming in must go out an rvalue — nothing may quietly turn into a copy along the way. `std::forward<Args>(args)...` is the pattern expansion that does exactly this job:

```cpp
template <typename T, typename... Args>
std::unique_ptr<T> make_tracked(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
```

`std::forward<Args>(args)...` expands into `std::forward<A0>(a0), std::forward<A1>(a1), ...`. Each argument is forwarded independently against its own template parameter `Ai` — a direct payoff of pattern expansion's "every element independent" property: within one pack of arguments, some can be lvalues and some rvalues, each keeping its own value category without disturbing the others. One spot is easy to misread: the `&&` in `Args&&...` is a forwarding reference, not the same animal as an ordinary rvalue reference, and it takes `std::forward` to hold on to the value category. Next piece we will work this mechanism all the way through.

<OnlineCompilerDemo allow-run
  title="std::forward<Args>(args)... forwards the pack; lvalues and rvalues each keep their value category"
  source-path="code/examples/vol4/vol2-modern-cpp17/forward_pack.cpp"
  description="relay takes any number of forwarding references; sink has two overloads, const& and &&. Passing an lvalue picks the const& overload, passing an rvalue picks the && overload. The make_tracked factory forwards into the string constructor."
/>

Output:

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

The named object `t` is an lvalue; through `std::forward` it reaches `sink` on the `const Tracked&` overload. The temporary `Tracked{}` is an rvalue and lands on the `Tracked&&` overload. The value category survives — that is exactly what the pattern expansion `std::forward<Args>(args)...` is doing.

## The empty-pack pitfall: folds blow up, if constexpr doesn't

Finally, a counterintuitive pitfall — one that connects right back to the previous piece.

A unary fold over an **empty pack** is ill-formed. When `ts` has no elements at all, the compiler has no way to fold "nothing" together with `+`, and the standard makes that case a compile error. GCC's diagnostic names it outright:

```text
error: fold of empty expansion over operator+
   return (ts + ...);   // 空包调用时会编译失败
```

The standard cuts a backdoor for exactly three operators: a unary `&&` fold over an empty pack is `true`, `||` is `false`, and the comma operator gives `void()`. Every other operator (`+`, `*`, `|` included) has no default value for the empty pack — all of them are ill-formed. This rule came up in vol3-04 when we covered the four fold forms; here we see its real consequence on the argument side.

But `if constexpr`-terminated recursion is fine with the empty pack. The outer `if constexpr (sizeof...(args) == 0)` routes an empty pack into the "return 0" branch at compile time, so execution never gets anywhere near a fold. That is the extra capability style two holds over style three: it handles the zero-argument case gracefully.

<OnlineCompilerDemo allow-run
  title="The empty-pack pitfall: by default the if constexpr termination compiles; -DEMPTY_FOLD reproduces the fold error"
  source-path="code/examples/vol4/vol2-modern-cpp17/empty_pack_pitfall.cpp"
  description="By default, the sum_ifc() empty pack takes the if constexpr termination branch and compiles. Add -DEMPTY_FOLD to switch to the unary fold version, where the empty-pack call triggers the fold of empty expansion compile error."
/>

Output (default, if constexpr termination):

```text
sum_ifc():      0   (空包走 if constexpr 终止分支)
sum_ifc(1,2,3): 6

默认演示通过。加 -DEMPTY_FOLD 复现空包 fold 的编译报错。
```

Add `-DEMPTY_FOLD` to switch to the fold version, and the empty-pack call fails to compile:

```text
error: fold of empty expansion over operator+
   return (ts + ...);   // 空包调用时会编译失败
```

So when you write a variadic function whose callers might pass zero arguments, the fold is not the first choice: add an empty-pack branch with `if constexpr`, or use a binary fold with an initial value, `(0 + ... + ts)` (the initial `0` gives the empty pack something to fold onto). This trap is especially easy to hit when writing general-purpose libraries, because "can it be called with an empty pack" is usually the caller's business, not something you control.

---

That is the core of variadic templates: declaring parameter packs, counting with `sizeof...`, processing element by element with pattern expansion, folding with folds, and the empty-pack boundary. What truly makes this machinery shine is perfect forwarding — the pattern expansion `std::forward<Args>(args)...` is the cornerstone of standard library facilities like `make_unique`, `emplace_back`, and `tuple` construction. Next piece we go inside perfect forwarding, to see how the forwarding reference `Args&&` manages to hold on to value categories, and the reference collapsing rules behind it.
