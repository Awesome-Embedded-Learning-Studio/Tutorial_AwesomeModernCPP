---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: Pins down NoDestructor's usage boundary — where it belongs (function-local static + non-trivially-destructible T) and where it does not (locals/members, trivially destructible, trivially constructible, rarely-used data), plus the constinit-global and magic-statics trade-offs, with a decision table at the end
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
translation:
  source: documents/vol9-open-source-project-learn/chrome/04_no_destructor/full/04-3-no-destructor-when-to-use.md
  source_hash: 0ad21cce9d10742a16328bb872a233f6f89a65c725d95ba5a4eb3bd80d0c33db
  translated_at: '2026-09-26T03:16:29+00:00'
  engine: anthropic
  token_count: 2600
---
# NoDestructor hands-on (III): when to use it, and when not to

NoDestructor is a sharp knife: in the right place it saves you trouble; in the wrong place it plants a mine — real leaks, wasted memory, and hidden bugs are all things it can do to you. Chromium itself spends an entire "Caveats" section in the source comments (no_destructor.h:15-46) listing the traps you must not step on. We read that section back and forth three times, and its boundary really collapses into one sentence: **the only recommended pattern is a function-local static, with a non-trivially-destructible T**. This piece pulls apart everything on either side of that line.

## Where it belongs: function-local static + non-trivially-destructible T

The canonical usage looks like this, and we suggest you commit it to memory as the template:

```cpp
const T& GetGlobal() {
    static const base::NoDestructor<T> x(args...);   // ✓ function-local static
    return *x;
}
```

Three conditions have to hold at once before NoDestructor gets its turn on stage. First, T has to be non-trivially destructible — the `std::string`, `std::vector`, `std::map` family. Making one of those a raw function-local static generates a global destructor, landing squarely on the target NoDestructor exists to eliminate. We carry a scar from a counterexample here: `std::mutex` looks non-trivial, but in the libstdc++/libc++ implementations it is actually trivially destructible and needs no NoDestructor at all. The first time around, we slipped a wrapper onto it by reflex, and only caught on when review flagged it. Second, it has to sit inside a function-local static, leaning on magic statics for thread-safe first construction — more on that later, in its own section. Third, the object has to be one the whole program lifetime uses; something cobbled together for the moment does not count. Drop any one of the three conditions, and NoDestructor goes from convenient to redundant, or even harmful.

## Where not to touch it

The misuse scenarios are called out one by one in Chromium's comments. We have reordered them by how well hidden the trap is, from the most obvious down to the easiest to miss, and walk through them in turn.

**The most obvious and the most lethal: using it to hold a local variable or a member.** One look at this shape should make you frown on instinct:

```cpp
void f() {
    base::NoDestructor<std::string> s("temp");   // ❌ real leak!
    // after f returns, s never destructs; the string's heap allocation is never freed (until the program exits)
}
```

The entire point of NoDestructor is "do not destruct", and that premise assumes the object was supposed to live until the program ends. Wrap it around a local or a member, and an object that should have died early instead has its destructor pinned shut by NoDestructor and never runs — the memory genuinely leaks. This is not the harmless "reclaimed in one sweep at exit" kind of leak; it is the real "not reclaimed when it should have been" kind. The source comments (no_destructor.h:18-20) put it in hard terms: **Must not be used for locals or fields**.

**One rung down: trivially-destructible T.** Here NoDestructor is pure redundancy; you never needed it at all:

```cpp
static const base::NoDestructor<int> x(42);   // ❌ int is trivially destructible, no NoDestructor needed
```

With T trivially destructible (like `int`, `double`, or a POD struct), its function-local static never produces a global destructor in the first place; a bare static does the job. NoDestructor's `static_assert` will turn you away (the second assertion), but what matters more than the compile error is this: you never recognized in the first place that this thing had no work to do here. Write it clean instead:

```cpp
static const int x = 42;   // ✓ trivially destructible, no global destructor
```

**A sneakier category: T that is both trivially constructible and trivially destructible.** For these, even a function-local static feels heavyweight; what you want is `constinit`:

```cpp
static const base::NoDestructor<uint64_t> seed(GetRand());   // ❌
```

Wrapping NoDestructor around something like `uint64_t`, which could be initialized directly as a `constinit` global constant, is pure scene-stealing — inventing drama for a role with no lines. The positive example given in the source comments (no_destructor.h:33-44) looks like this:

```cpp
const uint64_t GetUnstableSessionSeed() {
    static const uint64_t kSessionSeed = base::RandUint64();   // ✓ trivially destructible, no NoDestructor needed
    return kSessionSeed;
}
// or better: constinit uint64_t g_seed = constexpr_value;
```

**The easiest to miss: rarely-used data — do not cache it behind NoDestructor.** The trap here is that it never errors and never leaks; it just quietly eats memory:

```cpp
const BigTable& GetRareTable() {
    static const base::NoDestructor<BigTable> t(BuildBigTable());   // ⚠ use with care
    return *t;
}
```

If this table gets used once or twice across the whole program lifetime, caching it behind NoDestructor is a memory waste — the compiler reserves space for `BigTable` in the bss segment, and it stays occupied for the entire run, even if you touch it exactly once. The advice in the source comments (no_destructor.h:28-31) is blunt: create rarely used data on demand, do not cache it. Rewrite it like this, so it destructs the moment it is done being used and stops holding memory long term:

```cpp
// create on demand (destructs after use, no long-term memory held)
BigTable GetRareTable() { return BuildBigTable(); }   // return by value, transient use
```

---

## The constinit-global path is a dead end

NoDestructor's canonical usage is the function-local static. Could it be a global instead? The source comments (no_destructor.h:22-26) mention in passing that "a constinit-constructible T can be global, but it must be marked constinit" — we genuinely went and tried it at first, hit the wall, and **confirmed by experiment that it does not compile**:

```cpp
constinit const base::NoDestructor<MyConstexprType> g_data(args...);   // ⚠ compile failure
```

The reason hides in the constructor: NoDestructor internally has to call placement new (`new (storage_) T(...)`), and placement new is not `constexpr` at all, so NoDestructor's constructor is explicitly marked non-constexpr (the real header at no_destructor.h:95 spells it out in black and white: "Not constexpr"). `constinit` demands a constant-expression initializer; that check cannot pass, and the compiler throws you back a `constinit variable does not have a constant initializer`. We also flipped through Chromium's unittests while we were at it, and **there is not a single constinit global NoDestructor in there** — it is function-local statics across the board.

So this section really comes down to two sentences. For a constinit-constructible T, write `constinit T g(...)` directly and do not wrap NoDestructor around it — constant initialization is settled at compile time, and the runtime picture stays clean. A T that is not constinit-constructible but is non-trivially destructible is the only practical stage where a function-local-static `NoDestructor<T>` belongs. That source-comment line about "mark it constinit and make it global" reads more like an ideal or a historical leftover; the current implementation (construction through placement new) simply cannot hold it up.

---

## A magic-statics recap: thread safety comes from it, not from NoDestructor

One thing needs saying outright: NoDestructor itself **does no locking**, and it never says a single word about thread safety. That it survives concurrency unscathed is entirely thanks to C++11 magic statics — the initialization of function-local static variables, where the standard carries the concurrency-safety layer for you (details in [pre-00](./pre-00-static-storage-and-init.md)).

```cpp
const T& GetGlobal() {
    static const base::NoDestructor<T> x(args...);   // magic statics guarantees x is constructed once, thread-safely
    return *x;
}
```

And here is a causal chain we want to underline: the moment you leave the function-local static — say you hang NoDestructor off a member, or make a non-constinit global — the magic-statics protection walks out with it, and multiple threads touching a not-yet-initialized state is a race waiting to pop out. So "wrap it in a function-local static" is doing two jobs at once: it dodges the global constructor, and it hands the thread-safety burden over to magic statics. Those two stack together, and that is what pins NoDestructor's canonical usage down to the single function-local-static shape.

---

## Decision table

Gathering the judgments scattered above into one table, so the next time you are unsure you can match your case directly:

| Case | Use | Reason |
|---|---|---|
| Global/static + non-trivially-destructible T + needed for the whole program | **`static const NoDestructor<T>`** (function-local static) | Skips ctor/dtor + magic-statics thread safety |
| Trivially-destructible T | Bare `static T x = ...;` | No global destructor generated, no NoDestructor needed |
| Trivially-constructible + trivially-destructible T | `constinit T x = ...;` or `constexpr` | Compile-time initialization, no runtime code |
| Local variable / member variable | Plain `T x(...)` or `unique_ptr<T>` | NoDestructor causes a real leak |
| Rarely-used data | On-demand creation function (return by value) | NoDestructor caching wastes memory |
| You need T's destructor side effects (flush to disk / notify) | Function-local static `static T x;` (accept the destructor) | NoDestructor skips the destructor side effects |

At this point NoDestructor's usage boundary is clear. But it leaves us with a counterintuitive tail: it deliberately keeps objects from destructing, and to LeakSanitizer that reads as one big blob of a leak. How are the two supposed to coexist? And how does Chromium's reachability hack smooth the whole thing over? That is what the next piece takes apart.

---

## References

- [Chromium `base/no_destructor.h` — the Caveats section](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
- [cppreference: constinit (C++20)](https://en.cppreference.com/w/cpp/language/constinit)
- [cppreference: magic statics (thread-safe initialization of variables)](https://en.cppreference.com/w/cpp/language/storage_duration#Static_local_variables)
- [NoDestructor prerequisite (0): static storage duration, initialization, and destruction](./pre-00-static-storage-and-init.md)
