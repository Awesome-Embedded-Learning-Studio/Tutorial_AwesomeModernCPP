---
chapter: 12
cpp_standard:
- 17
description: 'if constexpr picks a branch at compile time and discards the rest, so dispatching a template by type no longer needs a pile of overloads or partial specializations.'
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'TMP Core Techniques: The World Before Concepts'
- 'Specialization and Partial Specialization: The Art of Pattern Matching'
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
title: 'if constexpr: Compile-Time Branching'
---
# if constexpr: Compile-Time Branching

In the previous piece we looked at the old TMP toolkit from before concepts — SFINAE, `void_t`, fold expressions. Those tools answer "is this type qualified" and "compute a value on the type." But there's another, much simpler need that shows up constantly in generic code: inside one template function, I want `int` to take one branch, `std::string` another, and everything else a third. SFINAE can do it, but you end up writing either a pile of overloads or a set of partial specializations, and the boilerplate piles up. C++17 gives a much lighter answer. `if constexpr` picks a branch at compile time, and the branch it drops is never instantiated.

This piece works through the mechanism, how much boilerplate it replaces, and the one boundary where people most often misunderstand it.

## First, why a plain `if` won't compile

Let's start with the smallest possible example. I want a `double_it` that doubles an integer and concatenates onto a string. The first instinct is usually something like this:

```cpp
template <typename T>
auto double_it(T x) {
    if (std::is_integral_v<T>) {
        return x + x;          // integer: addition
    } else {
        return x + " world";   // string: concatenation
    }
}
```

This looks natural, but it won't compile. Switch the `if constexpr` above to a plain `if`, call only `double_it(21)`, and the heart of what GCC 16.1.1 prints is this one line:

```text
error: inconsistent deduction for auto return type
   9 |         return x + " world";
     |                ~~^~~~~~~~~~
note: could be 'int'
note: or 'const char*'
```

The problem isn't that `is_integral_v` judged wrong, and it isn't that the wrong branch would actually run. The problem is that **both branches of a plain `if` must be instantiated** — the compiler has to instantiate the entire function body for `T = int`, including the `x + " world"` line in the `else`. For `int`, the first `return` deduces `auto` as `int`, the second deduces `const char*`, the two disagree, and `auto` deduction fails. Only one branch ever runs at runtime, but the compiler has to compile both. That's the root of every wall a plain `if` runs into inside a template.

## `if constexpr`: drop the branch you won't take

Swap `if` for `if constexpr`, and the same code compiles:

```cpp
template <typename T>
auto double_it(T x) {
    if constexpr (std::is_integral_v<T>) {
        return x + x;          // integer: addition, returns int
    } else {
        return x + " world";   // other types: concatenation, returns string
    }
}
```

<OnlineCompilerDemo allow-run
  title="if constexpr: the discarded branch isn't instantiated, so auto no longer conflicts"
  source-path="code/examples/vol4/vol2-modern-cpp17/if_vs_plain_if.cpp"
  description="Passing int, the else branch is dropped entirely and never instantiated, so auto deduces only int and no longer clashes with const char*."
/>

Run it:

```text
42
hello world
```

The whole difference is one word: `constexpr`. The condition of `if constexpr` is evaluated at compile time, and **the branch whose condition is false never enters instantiation**. In other words, when `double_it<int>` is instantiated, the compiler only sees `return x + x;` — the `else` branch doesn't exist for the `int` instantiation. `auto` has only one deduction result, `int`, so there's nothing to conflict with. Instantiating `double_it<std::string>` is the mirror image: it only sees `return x + " world";`.

That's the entire core of `if constexpr`: it lets template code branch like ordinary code, but each instantiation keeps only the branch it would actually take. What used to require a pile of overloads or partial specializations now fits in one function body.

::: warning The condition must be a compile-time constant
The condition of `if constexpr` has to be a constant expression. If you feed it something that's only known at runtime, the compiler refuses outright:

```cpp
int main(int argc, char**) {
    if constexpr (argc > 1) {   // argc is a runtime argument, not a constant expression
        return 0;
    }
    return 1;
}
```

GCC reports `error: 'argc' is not a constant expression`. If you want to branch on a runtime value, use a plain `if`. `if constexpr` only serves conditions the compiler already knows.
:::

## `else if constexpr`: flatten a pile of overloads

Where `if constexpr` really pays off is replacing that stack of "dispatch by type" overloads. Let's write an `inspect` that prints an integer doubled, appends an exclamation mark to a string (and reports its length), and calls `foo()` on anything else:

```cpp
template <typename T>
void inspect(T& x) {
    if constexpr (std::is_integral_v<T>) {
        std::cout << "integer:" << x << ", doubled:" << (x + x) << "\n";
    } else if constexpr (std::is_same_v<T, std::string>) {
        std::cout << "string length:" << x.size() << "\n";   // only string has size
        x.append("!");
    } else {
        x.foo();   // only types that land here need foo
    }
}
```

<OnlineCompilerDemo allow-run
  title="else if constexpr dispatch: operations depending on T don't error in discarded branches"
  source-path="code/examples/vol4/vol2-modern-cpp17/multi_dispatch.cpp"
  description="Pass int and the integral branch is kept; the string branch doesn't exist for int. Pass string and you hit the second branch; the x.foo() in else is never instantiated."
/>

Run it:

```text
整数:7,翻倍:14
字符串长度:2
s 现在是:hi!
```

There's a detail here that's easy to miss. The three calls `x.size()`, `x.append("!")`, and `x.foo()` each exist only on certain types. But when you pass in an `int`, the compiler instantiates `inspect<int>`, the condition `is_integral_v<int>` is true, the first branch is kept, and the other two are dropped entirely — `int` has no `size`, no `append`, no `foo`, and none of it matters, because those branches don't exist for `int`. Pass in a `HasFoo` (a struct with only `foo()`), and the first two conditions are false, so it falls into `else`, and `h.foo()` compiles fine.

This "one function body replaces three specializations" shape is the most everyday use of `if constexpr`. What used to be three partial specializations or three overloads is now a chain of `else if constexpr` keyed on type conditions, reading as smoothly as an ordinary if-else.

## How far does the compiler check a discarded branch

That brings us to a boundary that has to be stated plainly, because it's where `if constexpr` gets misunderstood most. **A discarded branch still goes through parsing, it just isn't instantiated.** Only the semantic checks that depend on the template parameter get skipped along with instantiation.

Consider this comparison. The condition `sizeof(T) > 100` is false for almost every type, so the branch is discarded:

```cpp
template <typename T>
void f() {
    if constexpr (sizeof(T) > 100) {
        T x = ;   // syntactically illegal: nothing on the right of the =
    }
}
```

Even though the branch is discarded, GCC still errors:

```text
error: expected primary-expression before ';' token [-Wtemplate-body]
    4 |         T x = ;
      |               ^
```

The flaw in `T x = ;` doesn't depend on what `T` is; there's nothing after the equals sign, that's a syntax error no matter who's reading, and it gets caught at parse time, before "discard the branch" ever gets a say. The reverse case, like `x.foo()` (does `T` even have `foo`?), can only be answered once `T` is substituted in, so it's an instantiation-time check, and discarding the branch skips it naturally.

One rule of thumb is enough: **the syntax has to be legal, that's the floor; only the semantic checks that depend on the template parameter get skipped along with the discard.** So writing `t.someMember()` in a discarded branch is fine, but forget a semicolon or leave a parenthesis unbalanced and it'll still error.

<OnlineCompilerDemo allow-run
  title="The boundary of a discarded branch: compiles by default, -DSYNTAX_ERR reproduces the error"
  source-path="code/examples/vol4/vol2-modern-cpp17/discarded_boundary.cpp"
  description="By default it shows a T-dependent operation not triggering instantiation in a discarded branch. Add -DSYNTAX_ERR to reproduce: a syntax error still errors even when the branch is discarded."
/>

## Two patterns worth knowing

Two more common uses of `if constexpr`, worth picking up.

The first is the init-statement form, same as the init-statement on a plain `if`, available since C++17:

```cpp
template <typename T>
void first_kind(const T& container) {
    if constexpr (auto v = *container.begin(); std::is_integral_v<decltype(v)>) {
        std::cout << "holds integers, first:" << v << "\n";   // v is visible here
    } else {
        std::cout << "doesn't hold integers\n";
    }
}
```

`auto v = *container.begin()` grabs the first element up front, and `v` is visible in the condition and in both branches, saving you a separate declaration outside.

The second is pairing it with a generic lambda, the standard companion to `std::visit`. A `std::variant` holding several types normally wants a visitor with a stack of `operator()` overloads. A generic lambda plus `if constexpr` collapses that into one:

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
  title="init-statement and a generic lambda for std::visit"
  source-path="code/examples/vol4/vol2-modern-cpp17/visit_init.cpp"
  description="The if constexpr (init; cond) form, plus a generic lambda using if constexpr to replace a stack of operator() overloads."
/>

Run it:

```text
装的是整数,第一个:10
装的不是整数
int:42
string:hello
```

## When to reach for it, and when not to

`if constexpr` is handy, but not every "branch by type" should use it. One guideline.

Whenever you want to **give different implementations inside one function body by type**, and those implementations share most of their context (same parameters, same setup and teardown), `if constexpr` is the most direct tool, cleaner than splitting into a pile of overloads or partial specializations. The generic lambda for `std::visit`, or template logic that handles numeric and string types separately, both live here.

The reverse: if what you actually want is to **constrain the template parameter** (say, "this function only accepts integers"), use a concept (`requires std::integral<T>`), not an `if constexpr` paired with `static_assert`. Concepts participate in overload resolution and produce error messages that name the constraint, neither of which `if constexpr` can give you. In the previous piece we saw SFINAE migrate onto concepts; the conclusion is the same here: constraints belong to concepts, dispatch belongs to `if constexpr`, each has its own job.

There's also a class of mistake to watch for: the branch condition doesn't really depend on type, it's just there for convenience. Writing `if constexpr (sizeof(int) == 4)` in an ordinary function is legal, but the condition is always true or always false, and this "compile-time-constant if" doesn't do much — it also tends to hide platform-specific code that really belongs in `#ifdef`. The value of `if constexpr` is that the condition **depends on a template parameter** and shifts with each instantiation. That's what sets it apart from both a plain `if` and a preprocessor directive.

In the next piece we put `if constexpr` to work somewhere it really shines — variadic templates. Before C++17, handling "any number of arguments" meant template recursion with a terminating overload. `if constexpr` paired with fold expressions collapses that whole apparatus into something clean.
