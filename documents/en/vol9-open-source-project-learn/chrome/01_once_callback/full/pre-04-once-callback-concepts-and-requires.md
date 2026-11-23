---
chapter: 0
cpp_standard:
- 20
description: "Start from the real problem of a template constructor hijacking the move constructor, and see how Concepts and requires constraints keep OnceCallback's constructors matching correctly"
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'OnceCallback prerequisite cheat sheet: a review of C++11/14/17 core features'
- 'OnceCallback prerequisite (I): function types and template partial specialization'
reading_time_minutes: 9
related:
- 'OnceCallback hands-on (II): building the core skeleton'
- 'OnceCallback prerequisite (V): std::move_only_function (C++23)'
tags:
- host
- cpp-modern
- intermediate
- concepts
- 模板
title: 'OnceCallback prerequisite (IV): Concepts and requires constraints'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/pre-04-once-callback-concepts-and-requires.md
  source_hash: 9d7dceb96e13497e4fa2766e045901f69e26a9643c6af65a3e6a70d68f945806
  translated_at: '2026-11-23T01:12:08+00:00'
  engine: glm-5.3-fence-fix
  token_count: 6825
---
# OnceCallback prerequisite (IV): Concepts and requires constraints

OnceCallback's constructor carries a constraint that looks downright redundant:

```cpp
template<typename Functor>
    requires not_the_same_t<Functor, OnceCallback>
explicit OnceCallback(Functor&& function);
```

The first time we read this line, our gut reaction was: isn't this pointless? Just `template<typename Functor>` and we're done — who exactly is `requires not_the_same_t` supposed to guard against?

Only after stepping on the rake for real did we learn what it guards against: a rather devious trap in C++ overload resolution — **a template constructor hijacking the move constructor**. Concepts and `requires` constraints are the defensive weapons C++20 left us. In this piece we'll dig the trap out from the top, and walk through the concepts syntax along the way.

## The problem: a template constructor going "offside"

Let's reconstruct the trap first.

Suppose we write a simple wrapper class that accepts any callable:

```cpp
template<typename FuncSignature>
class Callback;

template<typename R, typename... Args>
class Callback<R(Args...)> {
public:
    // 模板构造函数：接受任意可调用对象
    template<typename Functor>
    explicit Callback(Functor&& f) {
        // 用 f 初始化内部存储...
    }

    // 编译器隐式生成的移动构造函数
    // Callback(Callback&& other) noexcept;
};
```

We casually write `Callback cb2 = std::move(cb1);`, and the intent is plain: go through move construction. But the compiler actually has two paths in front of it: the implicitly generated move constructor `Callback(Callback&&)`, and the template constructor instantiated as `Callback(Callback&&)` (with `Functor = Callback`).

Intuitively you'd bet on the move constructor winning hands down — after all, it's "designed for this exact type." But C++ overload resolution doesn't follow intuition. The forwarding reference `Functor&&` on the template constructor is far too greedy: it perfectly matches anything, including a `Callback&&` itself, while the move constructor's parameter type is hard-wired as `Callback&&`. When it comes to "which one matches more exactly," the version instantiated from the template can sometimes look like the closer fit.

Luckily, C++ keeps a tie-breaker rule: when the template and non-template versions match equally well, the **non-template wins**. So in most cases the move constructor still comes out ahead. But it's not that clean — once forwarding references and perfect matches get involved, behavior starts drifting across compilers and versions. Worse, even when the move constructor wins, the template constructor still lies there in the candidate list, and some SFINAE contexts cough up baffling compile errors.

### A minimal reproduction

```cpp
struct Wrapper {
    // 模板构造函数：接受任何类型
    template<typename T>
    Wrapper(T&& x) {
        std::cout << "template constructor\n";
    }

    // 移动构造函数（编译器隐式生成或显式声明）
    Wrapper(Wrapper&& other) noexcept {
        std::cout << "move constructor\n";
    }
};

Wrapper a;
Wrapper b = std::move(a);  // 你期望输出 "move constructor"
                            // 在某些情况下可能输出 "template constructor"
```

The fix is to put a constraint on the template constructor so it stops reaching for `Wrapper`'s own type — and that's where the `requires` clause enters the stage.

---

## What exactly are Concepts

C++20 introduced Concepts. The official definition is a mouthful — "a mechanism for naming constraints." We think that phrasing just tangles people up further. The word concept does what it says on the tin: it means "concept."

Step back and think: before concepts came along, expressing "I only accept integer types" meant the whole `enable_if` machinery — `typename std::enable_if<std::is_integral_v<T>::value, int>::type = 0`, a long obscure string that readers have to mentally unwind before they figure out what you mean. What a concept does is let you **just say what the concept is**: its name is `Integral`, and it is the concept of "integer." That simple. If `T` satisfies `Integral`, `T` is an integer; if not, it doesn't get in the door.

Declaring a concept looks like this:

```cpp
template<typename T>
concept Integral = std::is_integral_v<T>;
```

`Integral` checks whether `T` is an integer type; `std::is_integral_v<T>` is a compile-time boolean constant. That's the entire idea we want to express — just give me an integer. With the concept in hand, the next step is feeding it to `requires`.

Hang a `requires` clause onto the back of a template declaration, and you've put a gate in front of the template parameter:

```cpp
template<typename T>
    requires Integral<T>
void foo(T x) {
    // 只有 T 是整数类型时，这个函数才会被实例化
}

foo(42);    // OK：int 是整数
foo(3.14);  // 编译错误：double 不满足 Integral
```

The `<concepts>` header also stocks a pile of ready-made library concepts; a few commonly used ones look like this:

```cpp
#include <concepts>

// std::invocable<F, Args...>：F 是否可以用 Args... 调用
static_assert(std::invocable<int(*)(int), int>);

// std::same_as<A, B>：A 和 B 是否是同一类型
static_assert(std::same_as<int, int>);

// std::convertible_to<From, To>：From 是否能隐式转换到 To
static_assert(std::convertible_to<int, double>);
```

---

## Taking `not_the_same_t` apart

Now let's turn back to the concept inside OnceCallback:

```cpp
template<typename F, typename T>
concept not_the_same_t = !std::is_same_v<std::decay_t<F>, T>;
```

One-sentence summary: once `F` has decayed, as long as it is not `T`, the constraint passes. There are three parts inside, and we'll take them apart one by one.

First, `std::decay_t<F>`. It does three things to a type: strips references (`int&` → `int`), strips top-level const/volatile (`const int` → `int`), and decays array and function types (`int[5]` → `int*`, `int(int)` → `int(*)(int)`). In the OnceCallback scenario, the critical one is stripping the reference. When we write `OnceCallback cb2 = std::move(cb1)`, `Functor` is deduced as `OnceCallback` (not `OnceCallback&&` — forwarding-reference deduction rules deduce rvalues as non-references); but if we wrote `OnceCallback cb2 = cb1` (the copy is deleted; this is just for illustration), `Functor` would be deduced as `OnceCallback&`. The job of `std::decay_t` is to take whatever reference shape `Functor` deduces to, reduce it all to bare `OnceCallback`, and then compare it against `T = OnceCallback`.

Next, `std::is_same_v<A, B>`. It returns `true` when `A` and `B` are exactly the same type. And "exactly the same" is strict: `int` and `const int` don't count, and neither do `int&` and `int`. That's why we had to apply `std::decay_t` first to unify the form on both sides — otherwise one side carries a reference and the other doesn't, and the comparison is pure noise.

Finally, the negation `!` is the finishing touch. The whole concept's value is `!std::is_same_v<std::decay_t<F>, T>` — if `F`'s decayed type equals `T`, the negation makes it `false`, the constraint fails, and the template is kicked out of the candidate set; if it doesn't equal `T`, the negation gives `true`, the constraint passes, and the template takes part in overload resolution as usual. That's the whole logic.

Hang the constraint back on and see the effect:

```cpp
template<typename Functor>
    requires not_the_same_t<Functor, OnceCallback>
explicit OnceCallback(Functor&& f) : status_(Status::kValid), func_(std::move(f)) {}
```

When what's passed in is `OnceCallback` itself (the move-construction scenario, for example), `not_the_same_t<OnceCallback, OnceCallback>` evaluates to `!true = false`, the constraint isn't satisfied, the template is left out in the cold, and the compiler has no choice but to pick the move constructor. When what's passed in is some other type — a lambda, a function pointer — the constraint is satisfied, the template takes the job normally, and gets selected as the constructor. That clean.

---

## This is not unique to OnceCallback

This is not a need exclusive to OnceCallback. `std::move_only_function`'s own implementation hangs an almost identical constraint on itself; the standard library just spells it with the standard concept `std::constructible_from` paired with `!std::is_same_v`. To put it plainly: every move-only type-erasure wrapper needs this defense. As long as your class has both "a template constructor that accepts any type" and "a compiler-generated move constructor," the two are bound to fight, and you must separate them with a constraint.

```text
模式总结：
模板构造函数 + requires 排除自身类型 = 保护移动语义的正确匹配
```

A parting word: when you later roll your own move-only wrappers like `unique_function` or `any_invocable`, remember this pattern. It's a universally applicable defensive move, and it saves you from debugging for hours only to discover the template intercepted your move semantics.

---

## Pitfall warnings

**Pitfall 1: forgetting `std::decay_t`.** Take the lazy route and write only `!std::is_same_v<F, T>` without the `std::decay_t`, and the trap is already planted — `F`'s deduced type may or may not carry a reference, entirely depending on how you call it. Look at these two scenarios:

```cpp
OnceCallback cb1([](int x) { return x; });

// 场景 A：std::move(cb1) 是右值
// Functor 推导为 OnceCallback（不带引用）
// is_same_v<OnceCallback, OnceCallback> == true → 约束失败 ✓ 正确

// 场景 B：const OnceCallback& ref = cb1;
// 如果有人写了 OnceCallback cb2(ref);
// Functor 推导为 const OnceCallback&
// is_same_v<const OnceCallback&, OnceCallback> == false → 约束通过 ✗ 错误！
```

In scenario B, if `decay_t` is missing, `const OnceCallback&` and `OnceCallback` are simply not the same type; the constraint passes anyway, and the template constructor gets selected — but semantically, what we want is a compile error (the copy is deleted), or at the very least not template construction. Once `decay_t` is added, `const OnceCallback&` decays into `OnceCallback`, the two sides line up, and the constraint correctly fails. We've stepped in this one ourselves and debugged for ages before realizing `decay_t` was missing.

**Pitfall 2: `static_assert(false)` "misfires" inside a template.** Before C++23, writing `static_assert(false, "...")` inside a template triggers the assertion failure on every instantiation — even if nobody ever calls the template. The old standard required `static_assert(false)` to be evaluated the moment the template definition is seen. Chromium's workaround is `static_assert(!sizeof(*this), "...")`: `!sizeof` is always `false`, but it depends on the type of `*this`, making it a dependent expression that isn't evaluated at definition time — it only blows up at instantiation. C++23 relaxed this rule, but if you're still compiling as C++20, keep this in mind.

---

In the next piece we'll look at `std::move_only_function` — it's OnceCallback's core storage type, and the key puzzle piece for replacing Chromium's hand-written BindState with standard library facilities.

## References

- [cppreference: Constraints and concepts](https://en.cppreference.com/w/cpp/language/constraints)
- [cppreference: std::decay](https://en.cppreference.com/w/cpp/types/decay)
- [Stack Overflow: Generic constructor template called instead of copy/move constructor](https://stackoverflow.com/questions/70267685/generic-constructor-template-called-instead-of-copy-move-constructor)
