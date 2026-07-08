---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: "Static storage duration, the three phases of static initialization, the Static Initialization Order Fiasco (SIOF), destruction-order problems, magic statics, and constinit. The groundwork for NoDestructor."
difficulty: intermediate
order: 0
platform: host
prerequisites:
- WeakPtr prerequisite (0): weak references and the lifetime puzzle
reading_time_minutes: 11
related:
- 'NoDestructor hands-on (I): motivation and API design'
- 'NoDestructor prerequisite (I): placement new and aligned storage'
tags:
- host
- cpp-modern
- intermediate
- 内存管理
- RAII
title: "NoDestructor prerequisite (0): static storage duration, initialization, and destruction"
---
# NoDestructor prerequisite (0): static storage duration, initialization, and destruction

You write `std::map<int, Config> global_table = load();` at file scope and probably think nothing of it. The map gets constructed at startup, destructed at exit, end of story. Yet Chromium's `//base` style guide flatly bans global constructors and destructors. The first time we hit that rule we stopped cold: such a natural thing to write, why on earth is it forbidden?

The answer only shows up once you go read the standard. Behind that one ban sits a whole chain of old C++ traps: static-storage objects have a weird three-phase initialization, the order of initialization across translation units is completely out of your hands (that's SIOF), and destruction order is just as out of control (shutdown races). This piece drags all of that foundation out into the light and walks through each piece. Once these are clear, why Chromium issues this ban and what the next few `NoDestructor` articles are actually solving falls into place on its own.

---

## Static storage duration

C++ groups objects by **storage duration**, and storage duration decides when an object is born and when it dies. The three we deal with every day: automatic storage duration for locals (created on function entry, destroyed on function exit, the usual stack citizen); dynamic storage duration for `new`'d objects (you manage them by hand, creation and destruction happen on your say-so); and the protagonist of this piece, static storage duration, which covers both globals and `static` variables. These are created at program startup and destroyed at program exit, living exactly as long as the program itself.

`NoDestructor` exists to serve that last category. When you write `static std::string s = "...";` or a global `std::map g_table;`, the `std::string` or `std::map` inside has static storage duration and lives as long as the program. The catch is that static-storage objects play by their own initialization and destruction rules, different from ordinary locals, and every problem we're about to talk about grows out of those rules.

---

## The three phases of static initialization

The standard splits the initialization of a static-storage object into three phases. The first two are the well-behaved ones, they don't make trouble.

Phase one is zero initialization: the memory gets wiped to all zeros. For builtin types, and for classes with zero-initialization semantics, this is where initialization ends. Phase two is constant initialization: if the initializer is a compile-time constant (a `constexpr`, say), the compiler resolves it during compilation. Both of these count as "static initialization" and produce no runtime code. Together they're "the static part": done at compile time, zero cost.

The real trouble is phase three, dynamic initialization. When the initializer has to be computed to be known, `static std::string s = "x";` (which has to call `std::string`'s constructor) or `static int n = rand();` (which has to call `rand()` at runtime), the compiler has to defer the work to runtime. This is "the dynamic part" and it costs real cycles. Every pitfall we cover below has its roots here.

```cpp
// Static initialization (compile time, no cost):
constexpr int kMax = 100;            // constant initialization
static int zero;                      // zero initialization

// Dynamic initialization (runtime, code has to run):
std::string g_name = "chromium";      // calls std::string's constructor
static int g_seed = rand();           // calls rand()
std::map<int,int> g_table;            // calls std::map's constructor
```

For every global or static that goes through dynamic initialization, the compiler emits a tiny "call its constructor at program startup" stub and drops it into the `.init_array` section. That stub has a name: the global constructor, and the runtime walks every entry before `main` even starts.

---

## The Static Initialization Order Fiasco (SIOF)

Inside one translation unit (one .cpp), global constructors run in declaration order. That much you control. The moment you cross translation units, the order becomes unspecified: the compiler is free to line them up however it likes.

That single fact is what surfaces the oldest C++ trap in the book, the Static Initialization Order Fiasco, SIOF for short. A plain example:

```cpp
// a.cpp
extern int b_value;
int a_value = b_value + 1;     // dynamic init, depends on b_value

// b.cpp
int b_value = std::rand();     // dynamic init (rand isn't constexpr, evaluated at runtime)
```

The first time we saw this we didn't think much of it. Run it, and the trap shows itself. If `a.cpp`'s `a_value` gets initialized first, it reads `b_value` before `b_value` has had its turn, and at that point `b_value` only has its zero-initialized 0. So `a_value` comes out as 1, not whatever `b_value` was actually supposed to evaluate to. One nuance is worth memorizing: only dynamic initialization is exposed to SIOF. Constant initialization (something like `int b = 42;`) is finished at compile time and always precedes any dynamic initialization, so it's immune. But once two cross-.cpp globals depend on each other and both are dynamically initialized, the order is out of your hands and the result is undefined behavior. The thing that makes this bug so miserable is that it barely reproduces. Change machines, change compiler flags, and the order shifts. Runs clean on your laptop, flakes out on CI.

The standard-blessed workaround is "construct on first use": tuck that global inside a function as a local static, so its initialization only happens the first time the function is called.

```cpp
int& a_value() {
    static int v = b_value() + 1;   // function-local static, initialized on first call
    return v;
}
int& b_value() {
    static int v = std::rand();     // b_value also becomes a function-local static
    return v;
}
```

Rewritten this way, the first time `a_value()` is called it goes and calls `b_value()` itself, and that call is what triggers `b_value()`'s construction. The order is now decided by the code you wrote, not by the compiler. This is the fundamental reason NoDestructor recommends function-local statics: it sidesteps SIOF at the root.

---

## Destruction order (the shutdown race)

Initialization has its order mess; destruction has one too. Static-storage objects get destructed when the program exits (after `main` returns, during `exit`), in the reverse of initialization order. It sounds elegant. The problem hides inside "reverse": if initialization order was never under your control, reverse order isn't either.

A common shape: some global object's destructor happens to depend on another global that's already been destroyed. A global logger holds a reference to a global string; at shutdown the string destructs first, the logger's destructor then touches the dead reference, instant UAF. Worse landmines live inside destructors themselves. Calling `exit` from inside one skips the destruction of every remaining static object, and is UB under cross-thread or nested calls. Throwing from one during stack unwinding trips `std::terminate`. Every step of the shutdown path can cost you a week.

Collectively this is the shutdown race. Chromium is a browser, so its shutdown path is messy to begin with: multiple processes, multiple threads, task queues possibly still draining. Global-object destruction races are repeat offenders in its bug tracker.

Chromium's answer is blunt: don't let global objects destruct at all. That's the core idea behind `NoDestructor`. The object lives as long as the program, but at program exit nothing destructs it. The cost is that the OS reclaims the memory when the process exits, which the OS was going to do anyway, so it's free. Trading one manual "skip the destructor" for the entire class of destruction-order problems is a deal Chromium is happy to take.

---

## Magic statics: the C++11 thread-safety guarantee

That "function-local static" workaround rests on a premise you might not have scrutinized: if several threads hit the function for the first time at once, the initialization has to be safe, right? Before C++11 this wasn't actually guaranteed. From C++11 on, the standard backs it up directly. It's known as magic statics. Paraphrased, the standard says roughly:

> If control flow passes concurrently through the declaration of an uninitialized function-local static, other threads **wait** for the in-flight initialization to finish.

In code, that means this pattern is safe to use:

```cpp
const std::string& GetDefault() {
    static const std::string s = "default";   // thread-safe: concurrent first calls still initialize exactly once
    return s;
}
```

Any number of threads can pile into `GetDefault()` and `s` is constructed exactly once, with no data race in between. That's a black-and-white guarantee from C++11 (GCC and Clang implement it underneath via `__cxa_guard_acquire`). One thing we want to flag here: NoDestructor can sit there calmly as a singleton precisely because it stands on magic statics. It doesn't add any lock of its own, the language does the work underneath. Get that straight and the NoDestructor implementation later won't trip you up with "why doesn't it lock?"

---

## constinit (C++20): guaranteeing zero-cost initialization

C++20 hands us a new tool: `constinit`. What it does fits in one sentence. It promises both you and the compiler that this variable's initialization will be constant initialization, finished at compile time, and that **no dynamic initialization code will ever be generated** for it.

```cpp
constinit int x = 42;             // OK: constant initialization
constinit int y = compute();      // compile error: compute() isn't a constant expression → rejected
```

We think the keyword is designed cleanly. It isn't advice, it's an assertion: if you can write it, you pass; if the initializer isn't constant, the compiler rejects it on the spot instead of leaving a grenade for runtime. Its value is exactly "force this global to not emit a global constructor." For a constinit-constructible type (a POD with a `constexpr` constructor, say), you can write `constinit T x` and have it both ways: you skip the global constructor and you keep using the bare type, no need to bother NoDestructor at all. That's precisely the "trivial case" in NoDestructor's static_assert recommendations: if T is trivially constructible and trivially destructible, just use constinit directly, and wrapping it in NoDestructor on top would be pointless.

---

## Why Chromium bans global ctors/dtors

Stack those pieces up and Chromium's ban on global constructors and destructors stops looking arbitrary. The most direct reason is startup performance: before `main`, the runtime has to walk the entire `.init_array`, and in a large project with thousands upon thousands of globals queued up, the startup delay is visible. Add SIOF and destruction races on top (the cross-translation-unit old traps), plus the fact that a browser's shutdown path is complex to begin with (multi-process, multi-thread tangled together), and destruction races in particular are vicious. Three reasons combined, and Chromium just cuts the whole thing off.

A ban alone is useless without a way to make people obey it. Chromium reaches for clang's `-Wglobal-constructors` and `-Wexit-time-destructors` warnings and locks them down with `-Werror`. Write a global that would emit a global ctor or dtor, and the build fails on the spot, no negotiation. `NoDestructor` is the official escape hatch Chromium shipped alongside the rule, purpose-built to slip past it.

Use a function-local static to dodge the global constructor (construct on first use, with magic statics guaranteeing thread safety); use `NoDestructor` to dodge the global destructor (don't register one at all). With both "dodges" in hand, you keep the rule and still get a globally visible object.

The pieces are on the bench. Next up is how NoDestructor turns those two dodges into actual code, and the mechanism it leans on is placement new with aligned storage.

## References

- [cppreference: storage duration](https://en.cppreference.com/w/cpp/language/storage_duration)
- [cppreference: static initialization](https://en.cppreference.com/w/cpp/language/initialization)
- [cppreference: constinit (C++20)](https://en.cppreference.com/w/cpp/language/constinit)
- [SIOF, explained (isocpp FAQ)](https://isocpp.org/wiki/faq/ctors#static-init-order)
- [Chromium `base/no_destructor.h` design notes](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
