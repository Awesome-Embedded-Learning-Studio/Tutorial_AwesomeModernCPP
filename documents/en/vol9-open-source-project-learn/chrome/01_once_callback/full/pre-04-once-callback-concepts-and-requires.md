---
chapter: 0
cpp_standard:
- 20
description: "Start from the real case of a template constructor hijacking the move constructor, then see how Concepts and requires constraints keep OnceCallback's constructors matching correctly."
difficulty: intermediate
order: 4
platform: host
prerequisites:
- OnceCallback prerequisites cheat sheet: a recap of C++11/14/17 core features
- OnceCallback prerequisites (I): function types and template partial specialization
reading_time_minutes: 9
related:
- OnceCallback hands-on (II): building the core skeleton
- OnceCallback prerequisites (V): std::move_only_function
tags:
- host
- cpp-modern
- intermediate
- concepts
- 模板
title: 'OnceCallback prerequisites (IV): Concepts and requires constraints'
---
# OnceCallback prerequisites (IV): Concepts and requires constraints

The OnceCallback constructor carries a constraint that looks almost redundant:

```cpp
template<typename Functor>
    requires not_the_same_t<Functor, OnceCallback>
explicit OnceCallback(Functor&& function);
```

When we first read this line, the reaction was: isn't that overkill? Just leave it as `template<typename Functor>` and move on. Who exactly does the `requires not_the_same_t` guard against?

After actually stepping on the trap, we learned it guards against a fairly nasty case in C++ overload resolution: **a template constructor can hijack the move constructor**. Concepts and the `requires` clause are the defensive weapons C++20 hands us. This piece digs the trap open from the top and walks through the concepts syntax along the way.

## The problem: a template constructor going "offside"

Let's reconstruct the trap first.

Say we write a simple wrapper that accepts any callable:

```cpp
template<typename FuncSignature>
class Callback;

template<typename R, typename... Args>
class Callback<R(Args...)> {
public:
    // Template constructor: accepts any callable
    template<typename Functor>
    explicit Callback(Functor&& f) {
        // initialize internal storage with f...
    }

    // Implicitly generated move constructor
    // Callback(Callback&& other) noexcept;
};
```

We casually write `Callback cb2 = std::move(cb1);`, meaning is obvious: go through move construction. But the compiler actually sees two paths: the implicitly generated move constructor `Callback(Callback&&)`, and the template constructor instantiated as `Callback(Callback&&)` (with `Functor = Callback`).

Your intuition says the move constructor wins easily, since it's "designed for this exact type." But C++ overload resolution does not follow intuition. The forwarding reference `Functor&&` on the template constructor is greedy; it can perfectly match anything, including a `Callback&&` itself. The move constructor's parameter type, on the other hand, is a fixed `Callback&&`. When it comes to "which one matches more precisely," the template-instantiated version can actually look like the tighter fit.

C++ does have a fallback rule: when a template and a non-template version match equally well, the **non-template wins**. So in most cases the move constructor does come out ahead. But this is not as clean as it sounds. Once forwarding references and perfect matches enter the picture, behavior starts drifting across compilers and versions. Worse, even when the move constructor wins, the template constructor is still sitting in the candidate list, and some SFINAE contexts spit out baffling compile errors.

### A minimal reproduction

```cpp
struct Wrapper {
    // Template constructor: accepts any type
    template<typename T>
    Wrapper(T&& x) {
        std::cout << "template constructor\n";
    }

    // Move constructor (implicitly generated or explicitly declared)
    Wrapper(Wrapper&& other) noexcept {
        std::cout << "move constructor\n";
    }
};

Wrapper a;
Wrapper b = std::move(a);  // You expect "move constructor"
                            // In some cases you may get "template constructor"
```

The fix is to hang a constraint on the template constructor so it stops trying to match the wrapper's own type. That is where the `requires` clause comes in.

---

## What Concepts actually are

C++20 introduced Concepts. The official definition is a mouthful, "a mechanism for naming constraints." We think that phrasing just tangles people up. The word concept does what it says on the tin: it's a concept.

Step back for a second. Before concepts existed, saying "I only accept integer types" meant threading through the `enable_if` machinery: `typename std::enable_if<std::is_integral_v<T>::value, int>::type = 0`, a long, obscure string the reader has to mentally decode before they understand what you mean. A concept lets you **just say what the thing is**: it's called `Integral`, it's the concept of "integer." That's it. If `T` satisfies `Integral`, `T` is an integer; if not, it doesn't get in.

Declaring a concept looks like this:

```cpp
template<typename T>
concept Integral = std::is_integral_v<T>;
```

`Integral` checks whether `T` is an integer type, and `std::is_integral_v<T>` is a compile-time boolean constant. That's the whole point we're making: we just want an integer. With that concept in hand, the next step is to feed it to `requires`.

A `requires` clause hangs off the back of a template declaration and puts a gate in front of the template parameter:

```cpp
template<typename T>
    requires Integral<T>
void foo(T x) {
    // only instantiated when T is an integer type
}

foo(42);    // OK: int is an integer
foo(3.14);  // compile error: double does not satisfy Integral
```

The `<concepts>` header also ships a batch of ready-made standard concepts. A few commonly used ones:

```cpp
#include <concepts>

// std::invocable<F, Args...>: can F be called with Args...?
static_assert(std::invocable<int(*)(int), int>);

// std::same_as<A, B>: are A and B the same type?
static_assert(std::same_as<int, int>);

// std::convertible_to<From, To>: can From implicitly convert to To?
static_assert(std::convertible_to<int, double>);
```

---

## Pulling `not_the_same_t` apart

Now let's look back at the concept on OnceCallback:

```cpp
template<typename F, typename T>
concept not_the_same_t = !std::is_same_v<std::decay_t<F>, T>;
```

In one sentence: once `F` decays, as long as it is not `T`, the constraint passes. There are three parts inside; let's take them one at a time.

First, `std::decay_t<F>`. It does three things to a type: strips references (`int&` becomes `int`), strips top-level const/volatile (`const int` becomes `int`), and decays array and function types (`int[5]` becomes `int*`, `int(int)` becomes `int(*)(int)`). In the OnceCallback scenario the critical one is stripping references. When we write `OnceCallback cb2 = std::move(cb1)`, `Functor` is deduced as `OnceCallback` (not `OnceCallback&&`; forwarding-reference deduction rules deduce rvalues as non-reference). But if someone wrote `OnceCallback cb2 = cb1` (copy is deleted, this is just for illustration), `Functor` would be deduced as `OnceCallback&`. The job of `std::decay_t` is to take whatever reference shape `Functor` deduces to and reduce it to a bare `OnceCallback`, then compare against `T = OnceCallback`.

Next, `std::is_same_v<A, B>`. It returns `true` only when `A` and `B` are exactly the same. Note "exactly the same" is strict: `int` and `const int` do not count, neither do `int&` and `int`. That's why we needed `std::decay_t` first, to unify the form on both sides; otherwise one side carries a reference and the other does not, and the comparison is pure noise.

The negation `!` at the end is the kicker. The whole concept's value is `!std::is_same_v<std::decay_t<F>, T>`: if `F`'s decayed type equals `T`, negation flips it to `false`, the constraint fails, and the template is kicked out of the candidate list; if it's not equal to `T`, negation gives `true`, the constraint passes, and the template participates in overload resolution normally. That's the entire logic.

Hang the constraint back onto the constructor and watch it work:

```cpp
template<typename Functor>
    requires not_the_same_t<Functor, OnceCallback>
explicit OnceCallback(Functor&& f) : status_(Status::kValid), func_(std::move(f)) {}
```

When what gets passed in is `OnceCallback` itself (the move-construction case), `not_the_same_t<OnceCallback, OnceCallback>` evaluates to `!true = false`, the constraint is not satisfied, the template is sidelined, and the compiler can only pick the move constructor. When what gets passed is a lambda, a function pointer, or any other type, the constraint is satisfied, the template takes the call normally, and it is selected as the constructor. Clean.

---

## This is not an OnceCallback exclusive

This is not a need unique to OnceCallback. `std::move_only_function`'s own implementation hangs an almost identical constraint on itself; the standard library just spells it with the standard concept `std::constructible_from` paired with `!std::is_same_v`. Plainly put, any move-only type-erasing wrapper has to eat this defense. As long as your class has both "a template constructor that accepts any type" and "a compiler-generated move constructor", the two will fight, and you have to keep them apart with a constraint.

```text
Pattern summary:
template constructor + requires excluding the class's own type = protects correct matching of move semantics
```

One note to file away: when you eventually roll your own `unique_function`, `any_invocable`, or other move-only wrappers, remember this pattern. It's a reusable defensive measure, and it saves you from debugging for an afternoon only to find move semantics got intercepted by the template.

---

## Pitfall warnings

**Pitfall 1: forgetting `std::decay_t`.** Take the lazy route and write only `!std::is_same_v<F, T>` without `std::decay_t`, and the trap is set. `F`'s deduced type may or may not carry a reference, depending entirely on how you call it. Look at these two scenarios:

```cpp
OnceCallback cb1([](int x) { return x; });

// Scenario A: std::move(cb1) is an rvalue
// Functor deduced as OnceCallback (no reference)
// is_same_v<OnceCallback, OnceCallback> == true → constraint fails ✓ correct

// Scenario B: const OnceCallback& ref = cb1;
// If someone then writes OnceCallback cb2(ref);
// Functor deduced as const OnceCallback&
// is_same_v<const OnceCallback&, OnceCallback> == false → constraint passes ✗ wrong!
```

In scenario B, without `decay_t`, `const OnceCallback&` and `OnceCallback` are simply not the same type, so the constraint passes and the template constructor gets picked. Semantically, what we want is a compile error (copy is deleted), or at least not the template constructor. Add `decay_t`, and `const OnceCallback&` decays into `OnceCallback`, the two sides line up, and the constraint correctly fails. We stepped on this one, debugging for a while before realizing `decay_t` was missing.

**Pitfall 2: `static_assert(false)` "misfires" inside templates.** Before C++23, writing `static_assert(false, "...")` inside a template trips the assertion on every instantiation, even if the template is never called. That's because the older standard required `static_assert(false)` to be evaluated the moment the template definition is seen. Chromium's workaround is `static_assert(!sizeof(*this), "...")`: `!sizeof` is always false, but it depends on the type of `*this`, so it's a dependent expression that does not get evaluated at definition time and only fires on instantiation. C++23 relaxed this rule, but if you're still compiling with C++20, keep this in mind.

---

Next we'll look at `std::move_only_function`, the core storage type of OnceCallback and the key piece for replacing Chromium's hand-written BindState with standard library facilities.

## References

- [cppreference: Constraints and concepts](https://en.cppreference.com/w/cpp/language/constraints)
- [cppreference: std::decay](https://en.cppreference.com/w/cpp/types/decay)
- [Stack Overflow: Generic constructor template called instead of copy/move constructor](https://stackoverflow.com/questions/70267685/generic-constructor-template-called-instead-of-copy-move-constructor)
