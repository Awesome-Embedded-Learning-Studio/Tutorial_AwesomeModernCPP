---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: "Pinning down where NoDestructor actually belongs: use it for function-local statics with non-trivially-destructible T, and leave it alone for locals/fields, trivially-destructible T, trivially-constructible T, and rarely-used data. Includes why the constinit global path does not compile, and the magic-statics recap. Decision table at the end."
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'NoDestructor hands-on (II): the core implementation'
- 'NoDestructor prerequisite (0): static storage duration, initialization, and destruction'
reading_time_minutes: 9
related:
- 'NoDestructor hands-on (IV): the LSan leak tradeoff and the reachability hack'
tags:
- host
- cpp-modern
- intermediate
- 内存管理
- RAII
title: "NoDestructor hands-on (III): when to use it, and when not to"
---
# NoDestructor hands-on (III): when to use it, and when not to

NoDestructor is a sharp knife. In the right place it saves you trouble; in the wrong place it plants a mine. Real leaks, wasted memory, bugs hidden for months, any of those can come out of a misuse. Chromium itself spends a whole "Caveats" section in the source comments (no_destructor.h:15-46) listing the traps you must not step on. We read that section three times over, and the boundary turns out to collapse to one sentence: **the only recommended pattern is a function-local static, and only when T is non-trivially destructible**. This piece pulls apart what sits inside and outside that line.

## Where it belongs: function-local static + non-trivially-destructible T

The canonical usage looks like this, and we suggest you commit it to memory as the template:

```cpp
const T& GetGlobal() {
    static const base::NoDestructor<T> x(args...);   // ✓ function-local static
    return *x;
}
```

Three conditions have to hold at once before NoDestructor earns a place. First, T has to be non-trivially destructible, the `std::string`, `std::vector`, `std::map` family, where a raw function-local static would generate a global destructor and land squarely on the target NoDestructor exists to eliminate. We have stepped on a counterexample here: `std::mutex` looks non-trivial, but in libstdc++/libc++ it is actually trivially destructible and does not need NoDestructor at all. We wrapped it once on autopilot and only caught it in review. Second, it has to live inside a function-local static, leaning on magic statics for thread-safe first construction; more on that below. Third, the object has to be needed for the whole program lifetime, not just a one-off. Drop any one of the three and NoDestructor goes from helpful to redundant, or actively harmful.

## Where to leave it alone

The misuses are listed point by point in the Chromium comments. We reordered them by how hard they are to spot, from the most obvious down to the easiest to miss, and walk through each.

**The most obvious and the most lethal: using it to hold a local variable or a member.** This shape should make you frown on instinct:

```cpp
void f() {
    base::NoDestructor<std::string> s("temp");   // ❌ real leak!
    // after f returns, s never destructs; the string's heap allocation is never freed (until exit)
}
```

The entire premise of NoDestructor is "do not destruct", and that premise assumes the object was supposed to live until exit anyway. Wrap it around a local or a member and the object was supposed to die early, except NoDestructor pins the destructor shut and it never runs, so the memory really does leak. This is not the harmless "reclaimed at exit" kind of leak. It is the "was supposed to be reclaimed and was not" kind. The source comments (no_destructor.h:18-20) are blunt about it: **Must not be used for locals or fields**.

**One step down: trivially-destructible T.** Here NoDestructor is just dead weight, you don't need it at all:

```cpp
static const base::NoDestructor<int> x(42);   // ❌ int is trivially destructible, no NoDestructor needed
```

If T is trivially destructible (say `int`, `double`, a POD struct), its function-local static never produces a global destructor in the first place, so a raw static is enough. NoDestructor's `static_assert` will turn you away (the second assertion), but more important than the compile error is the realization that this thing had no job here. Write it clean:

```cpp
static const int x = 42;   // ✓ trivially destructible, no global destructor
```

**Sneakier: T that is both trivially constructible and trivially destructible.** Even a function-local static is overkill here; reach for `constinit`:

```cpp
static const base::NoDestructor<uint64_t> seed(GetRand());   // ❌
```

A `uint64_t` that could be a `constinit` global constant initializer has no business inside a NoDestructor; that is just adding ceremony. The positive example in the source comments (no_destructor.h:33-44) is this:

```cpp
const uint64_t GetUnstableSessionSeed() {
    static const uint64_t kSessionSeed = base::RandUint64();   // ✓ trivially destructible, no NoDestructor needed
    return kSessionSeed;
}
// or better: constinit uint64_t g_seed = constexpr_value;
```

**The easiest to miss: don't cache rarely-used data behind NoDestructor.** This one never errors and never leaks, it just quietly eats memory:

```cpp
const BigTable& GetRareTable() {
    static const base::NoDestructor<BigTable> t(BuildBigTable());   // ⚠ careful
    return *t;
}
```

If this table only gets touched once or twice across the whole run, caching it behind NoDestructor is a waste: the compiler reserves space for `BigTable` in bss, and it sits there for the entire run even if you use it a single time. The source comments (no_destructor.h:28-31) say it plainly: create rarely used data on demand, do not cache it. Rewrite it so it destructs once used, and stops holding memory long term:

```cpp
// create on demand (destructs after use, no long-lived memory)
BigTable GetRareTable() { return BuildBigTable(); }   // return by value, transient use
```

---

## The constinit global path: it does not work

The canonical NoDestructor pattern is the function-local static. Can we make it a global instead? The source comments (no_destructor.h:22-26) drop one line about "a T that is constinit-constructible can be global, but must be marked constinit." We went and tried it, hit a wall, and **confirmed it does not compile**:

```cpp
constinit const base::NoDestructor<MyConstexprType> g_data(args...);   // ⚠ compile failure
```

The reason is buried in the constructor: NoDestructor internally calls placement new (`new (storage_) T(...)`), and placement new is not `constexpr`. So NoDestructor's constructor is explicitly marked non-constexpr (the real header at no_destructor.h:95 spells it out: "Not constexpr"). `constinit` demands a constant-expression initializer, that check fails, and the compiler hands you back `constinit variable does not have a constant initializer`. We also flipped through the Chromium unittests; **there is not a single constinit global NoDestructor in there**, it is function-local statics the whole way.

So the two things actually worth remembering from this section: if T is constinit-constructible, just write `constinit T g(...)`, do not wrap it in NoDestructor. Constant initialization is done at compile time and the runtime stays clean. If T is not constinit-constructible but is non-trivially destructible, that is the only practical stage where function-local static `NoDestructor<T>` earns its spot. The source comment line about "mark it constinit and go global" reads more like an ideal or a historical leftover; the current implementation (construction through placement new) does not support it.

---

## A magic-statics recap: thread safety comes from that, not from NoDestructor

One thing needs saying out loud: NoDestructor itself **does not lock anything**, and it has nothing to say about thread safety. It survives concurrency entirely thanks to C++11 magic statics, the initialization of function-local static variables, where the standard carries the thread-safety load for you (details in [pre-00](./pre-00-static-storage-and-init.md)).

```cpp
const T& GetGlobal() {
    static const base::NoDestructor<T> x(args...);   // magic statics guarantees x is constructed once, thread-safe
    return *x;
}
```

There is a causal chain worth tracing here: the moment you step outside the function-local static, say by hanging NoDestructor off a member or making a non-constinit global, the magic-statics protection walks off with it, and multiple threads touching an uninitialized state is a race waiting to happen. So "wrap it in a function-local static" is doing two jobs at once. It dodges the global constructor, and it hands the thread-safety burden to magic statics. Stack those two together and the canonical NoDestructor usage gets pinned down to the single function-local-static shape.

---

## Decision table

Pulling the scattered judgments above into one table, so the next time you're unsure you can just match the case:

| Case | Use | Reason |
|---|---|---|
| Global/static + non-trivially-destructible T + needed program-wide | **`static const NoDestructor<T>`** (function-local static) | Skips ctor/dtor + magic-statics thread safety |
| Trivially-destructible T | Raw `static T x = ...;` | No global destructor, no NoDestructor needed |
| Trivially-constructible + trivially-destructible T | `constinit T x = ...;` or `constexpr` | Compile-time init, no runtime code |
| Local variable / member | Plain `T x(...)` or `unique_ptr<T>` | NoDestructor causes a real leak |
| Rarely used data | On-demand function (return by value) | NoDestructor caching wastes memory |
| You need T's destructor side effects (flush/notify) | Function-local `static T x;` (accept the destructor) | NoDestructor skips the destructor side effects |

That clears the usage boundary for NoDestructor. But it leaves behind a counterintuitive tail: it deliberately keeps objects from destructing, and to LeakSanitizer that reads as one big leak. How do the two coexist, and how does Chromium's reachability hack paper over it? That is what the next piece unpacks.

---

## References

- [Chromium `base/no_destructor.h`, Caveats section](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
- [cppreference: constinit (C++20)](https://en.cppreference.com/w/cpp/language/constinit)
- [cppreference: magic statics (thread-safe initialization of variables)](https://en.cppreference.com/w/cpp/language/storage_duration#Static_local_variables)
- [NoDestructor prerequisite (0): static storage duration, initialization, and destruction](./pre-00-static-storage-and-init.md)
