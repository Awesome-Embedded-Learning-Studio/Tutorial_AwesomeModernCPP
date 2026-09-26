---
chapter: 12
cpp_standard:
- 17
description: 'if constexpr picks a branch at compile time and the discarded branch is never instantiated, so dispatching by type inside a template no longer depends on a stack of overloads or partial specializations.'
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'TMP Core Techniques: The World Before Concepts'
- 'Template Specialization and Partial Specialization: The Art of Pattern Matching'
reading_time_minutes: 12
related:
- 'Variadic Templates: Expanding Parameter Packs'
- 'Perfect Forwarding: Forwarding References and Reference Collapsing'
tags:
- host
- cpp-modern
- intermediate
- if_constexpr
- 模板
- 编译期计算
title: "if constexpr: Compile-Time Branching"
translation:
  source: documents/vol4-advanced/vol2-modern-cpp17/01-if-constexpr.md
  source_hash: 376f3302907c3e0024c41f600561e371ea3e4e5c535bfaff9c10389eeaf7beaa
  translated_at: '2026-09-26T03:11:05+00:00'
  engine: anthropic
  token_count: 4500
---
# if constexpr: Compile-Time Branching

In the previous piece we looked at the old TMP toolkit from the era before concepts — SFINAE, `void_t`, fold expressions. Those tools answer questions like "is this type qualified" and "compute a value on this type." But generic code also has a much humbler need: inside one and the same function template, I want `int` to take this branch, `std::string` to take that branch, and everything else to take a third. SFINAE can do it, but only by writing a stack of overloads or a partial specialization — heavy boilerplate. C++17 offers a much lighter answer. `if constexpr` picks the branch at compile time, and the discarded branch is never instantiated at all.

In this piece we'll make three things clear: how `if constexpr` works, how much boilerplate it can replace, and the one boundary where it gets misunderstood most.

## First, why a plain `if` won't compile

Let's start with a minimal example. I want to write a `double_it`: pass in an integer and it returns the doubled value; pass in a string and it returns the concatenated result. The first instinct usually looks like this:

```cpp
template <typename T>
auto double_it(T x) {
    if (std::is_integral_v<T>) {
        return x + x;          // integers: addition
    } else {
        return x + " world";   // strings: concatenation
    }
}
```

This code looks natural, but it won't compile. With the plain `if` left in place and only one call site, `double_it(21)`, the heart of what GCC 16.1.1 spits out is this one line:

```text
error: inconsistent deduction for auto return type
   9 |         return x + " world";
     |                ~~^~~~~~~~~~
note: could be 'int'
note: or 'const char*'
```

The problem isn't that `is_integral_v` judged wrong, nor which branch actually runs. The problem is that **both branches of a plain `if` must be instantiated** — for `T = int` the compiler instantiates the entire function body, including that `x + " world"` in the `else`. For `int`, the first `return` deduces `auto` as `int`, the second deduces `const char*`; the two disagree, and the `auto` deduction fails. At runtime only one branch runs, but the compiler has to compile both — that is the root of why a plain `if` keeps hitting walls inside templates.

## `if constexpr`: discarding the untaken branch entirely

Swap the `if` for `if constexpr`, and the very same code compiles:

```cpp
template <typename T>
auto double_it(T x) {
    if constexpr (std::is_integral_v<T>) {
        return x + x;          // integers take addition, returns int
    } else {
        return x + " world";   // other types take concatenation, returns string
    }
}
```

<OnlineCompilerDemo allow-run
  title="if constexpr: discarded branches are not instantiated, so auto no longer conflicts"
  source-path="code/examples/vol4/vol2-modern-cpp17/if_vs_plain_if.cpp"
  description="With an int argument the else branch is dropped entirely and never instantiated, so auto has a single deduction result, int, and no longer clashes with const char*."
/>

Output:

```text
42
hello world
```

The difference is a single word: `constexpr`. The condition of `if constexpr` is evaluated at compile time, and the branch whose condition is false **never enters instantiation**. Put differently, when instantiating `double_it<int>`, the compiler sees only `return x + x;` — the `else` branch simply does not exist for the `int` instantiation. `auto` has exactly one deduction result, `int`, so the conflict vanishes on its own. Instantiating `double_it<std::string>` is the mirror image: it sees only `return x + " world";`.

That is the whole core of `if constexpr`: it lets template code branch on a condition just like ordinary code, while each instantiation keeps only the branch it will actually take. What used to require a stack of overloads or partial specializations is now handled by a single function body.

::: warning The condition must be a compile-time constant
The condition of `if constexpr` must be a constant expression. If you feed it a value that is only known at runtime, the compiler rejects it outright:

```cpp
int main(int argc, char**) {
    if constexpr (argc > 1) {   // argc is a runtime argument, not a constant expression
        return 0;
    }
    return 1;
}
```

GCC reports `error: 'argc' is not a constant expression`. To branch on a runtime value, use a plain `if`; `if constexpr` serves only conditions known at compile time.
:::

## `else if constexpr`: flattening a stack of overloads

Where `if constexpr` really pays off is replacing those "dispatch by type" stacks of overloads. Let's write an `inspect`: integers print their doubled value, strings append an exclamation mark and report their length, and any other type gets its `foo()` called:

```cpp
template <typename T>
void inspect(T& x) {
    if constexpr (std::is_integral_v<T>) {
        std::cout << "整数:" << x << ",翻倍:" << (x + x) << "\n";
    } else if constexpr (std::is_same_v<T, std::string>) {
        std::cout << "字符串长度:" << x.size() << "\n";   // only string has size
        x.append("!");
    } else {
        x.foo();   // only types that land in the else branch need foo
    }
}
```

<OnlineCompilerDemo allow-run
  title="else if constexpr multi-way dispatch: T-dependent operations in discarded branches don't error"
  source-path="code/examples/vol4/vol2-modern-cpp17/multi_dispatch.cpp"
  description="int takes the integral branch, so the string branch doesn't exist for int. Passing a string takes the second branch; the x.foo() in else is never instantiated."
/>

Output:

```text
整数:7,翻倍:14
字符串长度:2
s 现在是:hi!
```

Here's a key point that's easy to miss. The three statements `x.size()`, `x.append("!")`, and `x.foo()` each exist only for particular types. But when an `int` comes in, the compiler instantiates `inspect<int>`: the condition `is_integral_v<int>` is true, the first branch is kept, and the following two are discarded wholesale — `int` has no `size`, no `append`, no `foo`, and none of that matters, because those two branches don't exist for `int`. When a `HasFoo` (a struct with nothing but `foo()`) comes in, both earlier conditions are false, control falls into the `else`, and `h.foo()` compiles fine.

This "one function body replaces three specializations" style is the most everyday use of `if constexpr`. What used to be three partial specializations or three overloads is now just a chain of `else if constexpr` keyed on type conditions, reading as smoothly as an ordinary `if-else`.

## Exactly how far the compiler checks a discarded branch

At this point one boundary needs spelling out — this is probably the most misunderstood aspect of `if constexpr`. **A discarded branch still has to pass syntax checking; only instantiation is skipped.** Just the semantic checks that depend on the template parameter get skipped along with instantiation.

Let's look at a contrast. In the following snippet, the condition `sizeof(T) > 100` is false for the vast majority of types, so the branch gets discarded:

```cpp
template <typename T>
void f() {
    if constexpr (sizeof(T) > 100) {
        T x = ;   // illegal syntax: nothing on the right of the equals sign
    }
}
```

Even though the branch is discarded, GCC still errors out:

```text
error: expected primary-expression before ';' token [-Wtemplate-body]
    4 |         T x = ;
      |               ^
```

What's wrong with `T x = ;` doesn't depend on what `T` is — the right-hand side of the `=` is empty, which is a syntax error to any reader. Errors like this are caught at the parsing stage, before "discard the branch" ever gets a chance to wave it through. Conversely, a question like `x.foo()` — does this `T` even have a `foo`? — can only be answered once `T` is substituted in; that's an instantiation-stage check, so a discarded branch naturally skips it.

Remember this one distinction and you're set: **the syntax has to be legal — that's the floor; only the semantic checks that depend on the template parameter get skipped along with the discard.** So writing `t.someMember()` inside a discarded branch is fine, but miss a semicolon or leave a bracket unbalanced, and the error is still reported.

<OnlineCompilerDemo allow-run
  title="The boundary of a discarded branch: compiles by default, -DSYNTAX_ERR reproduces the syntax error"
  source-path="code/examples/vol4/vol2-modern-cpp17/discarded_boundary.cpp"
  description="By default it shows a T-dependent operation in a discarded branch triggering no instantiation; add -DSYNTAX_ERR to reproduce: a syntax error is still reported even when the branch is discarded."
/>

## Two handy forms to pick up

`if constexpr` has two more common uses worth jotting down.

The first is the form with an init-statement, same as on a plain `if`, available since C++17:

```cpp
template <typename T>
void first_kind(const T& container) {
    if constexpr (auto v = *container.begin(); std::is_integral_v<decltype(v)>) {
        std::cout << "装的是整数,第一个:" << v << "\n";   // v is visible here
    } else {
        std::cout << "装的不是整数\n";
    }
}
```

`auto v = *container.begin()` grabs the first element up front, and `v` is visible in the condition and in both branches, saving you a variable declared outside.

The second is pairing it with generic lambdas — the standard companion of `std::visit`. Visiting a multi-type `std::variant` used to mean writing a visitor with a pile of `operator()` overloads; a generic lambda plus `if constexpr` takes care of it in one stroke:

```cpp
auto print_variant = [](const auto& v) {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, int>) {
        std::cout << "int:" << v << "\n";
    } else if constexpr (std::is_same_v<T, double>) {
        std::cout << "double:" << v << "\n";
    } else {
        std::cout << "string:" << v << "\n";
    }
};

std::variant<int, double, std::string> var = 42;
std::visit(print_variant, var);   // int:42
```

<OnlineCompilerDemo allow-run
  title="init-statements and std::visit with generic lambdas"
  source-path="code/examples/vol4/vol2-modern-cpp17/visit_init.cpp"
  description="The if constexpr (init; cond) form, plus if constexpr inside a generic lambda replacing a stack of operator() overloads."
/>

Output:

```text
装的是整数,第一个:10
装的不是整数
int:42
string:hello
```

## When to use it, and when not to

`if constexpr` is handy, but not every "case-by-type" situation should use it. Here's my rule of thumb.

Whenever you need **different implementations by type inside one function body**, and those implementations share most of their context (same parameters, same prelude and epilogue), `if constexpr` is the most direct way to write it — far cleaner than splitting into a stack of overloads or partial specializations. Generic lambdas for `std::visit`, and template logic that handles numeric types and string types separately, both fall in this camp.

Conversely, if what you want is to **constrain whether a template parameter qualifies** (say, "this function only accepts integral types"), that's a job for concepts (`requires std::integral<T>`), not something to cobble together from `if constexpr` plus `static_assert`. Concepts participate in overload resolution and produce errors that name the violated constraint — things `if constexpr` cannot give you. The previous piece walked through the SFINAE-to-concepts migration comparison, and the conclusion carries over unchanged: constraints belong to concepts, dispatch belongs to `if constexpr`, each minding its own turf.

There's also a class of easy mistakes to watch for: branch conditions that don't actually depend on the type and only exist for convenience. Writing `if constexpr (sizeof(int) == 4)` inside an ordinary function is legal, but the condition is always true or always false; this "if on a compile-time constant" carries little value, and it tends to bury platform-dependent code that really belongs behind `#ifdef`. The value of `if constexpr` is that the condition **depends on the template parameter** and changes from instantiation to instantiation — that's what sets it apart from both a plain `if` and preprocessor directives.

In the next piece we'll put `if constexpr` somewhere it really shines: variadic templates. Before C++17, handling "any number of arguments" took template recursion plus a termination overload; `if constexpr` combined with fold expressions cleans that whole apparatus right up.
