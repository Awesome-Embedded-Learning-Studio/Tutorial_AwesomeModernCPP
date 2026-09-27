---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: Cover static storage duration, the three kinds of static initialization, the static initialization order fiasco (SIOF), destruction-order problems, magic statics, and constinit — laying the groundwork for NoDestructor
difficulty: intermediate
order: 0
platform: host
prerequisites:
- 'WeakPtr prerequisite (0): weak references and the lifetime puzzle'
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
translation:
  source: documents/vol9-open-source-project-learn/chrome/04_no_destructor/full/pre-00-static-storage-and-init.md
  source_hash: d71d1ed1c432fa9afd0c44d974a5b74e7147e91b52d17178ef9443035753331c
  translated_at: '2026-09-26T03:16:26+00:00'
  engine: anthropic
  token_count: 6400
---
# NoDestructor prerequisite (0): static storage duration, initialization, and destruction

You toss off a line like `std::map<int, Config> global_table = load();` in your program and think nothing of it — the map constructs at startup, destructs at exit, as natural as breathing. But Chromium's `//base` style guide flat-out bans global constructors and destructors. The first time we saw that rule we did a double take too: such a natural way to write it — on what grounds is it forbidden?

Once the surprise wore off we went digging through the standard, and it turns out this one ban is sitting on a whole pile of old C++ traps: static-storage-duration objects have a curious three-stage initialization, the initialization order across translation units is entirely out of your hands (that's SIOF), and the destruction order is just as unruly (the shutdown race). This piece drags those foundations out into the daylight and works through every one of them. Once you truly understand all this, why Chromium imposes the ban — and what problem the upcoming `NoDestructor` articles are actually solving — falls into place on its own.

---

## Static storage duration

C++ classifies objects by **storage duration**, and storage duration decides when an object is born and when it dies. Day to day we mostly deal with three kinds. Local variables are created on function entry and destroyed on exit — that's automatic storage duration, the regulars on the stack. Objects born from `new` are yours to manage by hand, created and destroyed at your say-so — that's dynamic storage duration. And then there's the protagonist of this piece: static storage duration. Global variables and `static` variables all belong to this class — created when the program starts, destroyed when it exits, living exactly as long as the program itself.

`NoDestructor` exists to serve exactly this class. Write `static std::string s = "...";` or a global `std::map g_table;`, and that `std::string`, that `std::map`, are both static storage duration, living as long as the program. The catch is that static-storage-duration objects follow their own set of initialization and destruction rules, different from ordinary local variables — and every bit of the trouble we're about to wade through grows out of that rulebook.

---

## Three-stage static initialization

For objects of static storage duration, the standard splits initialization into three stages. Take the first two first — they're the well-behaved kids who cause no trouble.

Stage one is zero initialization: the object's memory is simply zeroed out wholesale. For built-in types and classes with zero-initialization semantics, initialization is finished once this step completes. Stage two is constant initialization: if the initializer is a compile-time constant — say you wrote a `constexpr` — the compiler finishes the job during compilation. This stage also counts as "static initialization" and produces no runtime code at all. Together these two form the "static" part: done at compile time, zero overhead.

The real trouble lives in stage three — dynamic initialization. When the initializer has to be computed before the result is known — `static std::string s = "x";` needs the `std::string` constructor called, `static int n = rand();` needs `rand()` called at runtime — the compiler has no choice but to defer the work to runtime. This is the "dynamic" part, and it carries runtime cost. Every trap we walk through below has its roots in this stage.

```cpp
// Static initialization (compile time, no cost):
constexpr int kMax = 100;            // constant initialization
static int zero;                      // zero initialization

// Dynamic initialization (runtime, code has to execute):
std::string g_name = "chromium";      // must call the std::string constructor
static int g_seed = rand();           // must call rand()
std::map<int,int> g_table;            // must call the std::map constructor
```

For every global or static variable that goes through dynamic initialization, the compiler must additionally emit a snippet of "call its constructor at program startup" code and tuck it into the `.init_array` section. This snippet has a name — the global constructor — and before `main` the runtime walks the whole list and runs it once.

---

## The static initialization order fiasco (SIOF)

Within a single translation unit (a single .cpp), global constructors run in declaration order — that much you control. But the moment you cross translation units, the order becomes unspecified, and the compiler arranges things however it pleases.

And with that we've dug up the mustiest trap in all of C++ — the Static Initialization Order Fiasco, SIOF for short. Here's the most bare-bones example:

```cpp
// a.cpp
extern int b_value;
int a_value = b_value + 1;     // dynamic initialization, depends on b_value

// b.cpp
int b_value = std::rand();     // dynamic initialization (rand is not constexpr, evaluated at runtime)
```

When we first saw this example it looked innocent; running it was what revealed the trick. If `a.cpp`'s `a_value` gets initialized first, it reads `b_value` before `b_value` has had its turn at dynamic initialization — all that's there is the zero-initialized 0 — so `a_value` works out to 1, not the value it should have gotten after `b_value` was truly evaluated. One nuance to lock in: only dynamic initialization can collide with SIOF — constant initialization (something like `int b = 42;`) is finished at compile time and always precedes dynamic initialization, so it never meets SIOF. But the moment two globals across .cpp files depend on each other and both are dynamically initialized, the order is completely out of control, and the result is undefined behavior. What makes this bug so agonizing is how hard it is to reproduce — switch machines, switch compiler flags, and the order changes; runs perfectly locally, flakes out on CI.

The classic counter to SIOF (from the isocpp FAQ) is called "construct on first use": tuck that global variable inside a function as a local static, and don't lift a finger to initialize it until the function is called for the very first time.

```cpp
int& a_value() {
    static int v = b_value() + 1;   // function-local static, initialized on first call
    return v;
}
int& b_value() {
    static int v = std::rand();     // b_value becomes a function-local static too
    return v;
}
```

With this change, the first time `a_value()` is called, it actively goes and calls `b_value()` — and that call is what triggers `b_value()`'s construction. The order is now decided by the code you wrote, no longer by the compiler's say-so. This is the fundamental reason NoDestructor's recommended pattern is the function-local static: it routes around SIOF at the root.

---

## The destruction-order problem (the shutdown race)

Initialization has its order mess, and destruction has one of its own. Static-storage-duration objects are destroyed when the program exits (after `main` returns, or at `exit`), in the reverse of their initialization order. It sounds elegant — until you notice the problem hiding behind the word "reverse": you never controlled the initialization order in the first place, so the reverse goes out of control right along with it.

Take the most common scenario: one global object's destructor happens to depend on another object that has already been destroyed. Say a global logger holds a reference to a global string; at shutdown the string destructs first, and when the logger's turn comes and it touches that reference — instant use-after-free. There are sneakier mines buried inside destructors themselves: calling `exit` there skips the destruction of the remaining static objects, and in cross-thread or nested-call scenarios it's UB; throwing an exception there triggers `std::terminate` if stack unwinding is already in progress. Every one of these on the shutdown path is enough to cost you a week of debugging.

This whole family of ailments goes by the collective name shutdown race. Chromium is a browser, and its shutdown path is messy to begin with: multiple processes, multiple threads, task queues possibly still draining. Races between global objects' destructors are repeat customers in its bug tracker.

Chromium's solution is blunt to the point of brutality: just don't let global objects destruct. That is the core idea of `NoDestructor`. It lets the object live as long as the program, but when the program ends, nobody destructs it; the cost is that the operating system reclaims the memory wholesale when the process exits — something the OS was going to do anyway, so it comes free. One manually waived destructor in exchange for an entire class of destruction-order trouble — Chromium reckons that trade is worth it.

---

## magic statics: the C++11 thread-safety guarantee

That "function-local static" remedy rests on a premise you may not have thought through: if several threads call into the function for the first time simultaneously, the initialization had better be safe, right? Before C++11 nothing actually guaranteed this; it was C++11 that put the standard's weight behind it, a guarantee known in the trade as magic statics. The standard's wording, roughly:

> If control flow passes concurrently through the declaration of an uninitialized function-local static variable, other threads will **wait** for the in-progress initialization to complete.

Translated into code, this is a pattern you can use with confidence:

```cpp
const std::string& GetDefault() {
    static const std::string s = "default";   // thread-safe: on concurrent first calls from multiple threads, only one performs the initialization
    return s;
}
```

Any number of threads can call `GetDefault()` together; `s` gets constructed exactly once, with no data race in between. That is a guarantee C++11 gives in black and white (GCC/Clang implement it for you underneath via `__cxa_guard_acquire`). One point we want to flag here: the reason NoDestructor can serve so reliably as a singleton is that its roots are planted in magic statics — it doesn't add any lock for you; it's the language providing the safety net down below. Get that straight, and when you read NoDestructor's implementation later you won't be tripped up by "why doesn't it lock anything".

---

## constinit (C++20): guaranteed zero runtime initialization

C++20 handed us a new tool: `constinit`. What it does fits in one sentence: it promises you and the compiler that this variable's initialization is guaranteed to be constant initialization, finished at compile time — **it will absolutely never generate dynamic initialization code**.

```cpp
constinit int x = 42;             // OK: constant initialization
constinit int y = compute();      // compile error: compute() is not a constant expression → rejected
```

We find this keyword refreshingly decisive in its design — it isn't a suggestion, it's an assertion: if you can write it, you pass; if the initializer isn't a constant, the compiler rejects it on the spot instead of leaving a mine behind to blow up at runtime. Its value is precisely "a hard guarantee that this global variable generates no global constructor." For types that are constinit-constructible (a POD constructed via `constexpr`, say), a plain `constinit T x` gives you the best of both worlds — you dodge the global constructor and keep working with the bare type, with no need to summon NoDestructor at all. That is exactly the "trivial case" in NoDestructor's static_assert recommendation: T trivially constructible plus trivially destructible means constinit walks in and does the job, and wrapping another layer of NoDestructor around it would just be doing work twice.

---

## Why Chromium bans global ctor/dtor

Assemble the pieces above and Chromium's motive for banning global construction and destruction is plain to see. The most straightforward one is startup performance — everything in `.init_array` has to run before `main`, and in a large project thousands upon thousands of global objects queue up to construct, so the startup delay is visible to the naked eye. Stack on top of that SIOF and the destruction race, the two classic cross-translation-unit traps, plus a browser shutdown path that is complicated to begin with (multiple processes and threads tangled together) — the destruction race is especially lethal. Add the three together, and Chromium simply cuts the knot with one stroke.

But a ban alone accomplishes nothing; you need machinery to make people follow the rules. What Chromium wheels out are the two clang warnings `-Wglobal-constructors` and `-Wexit-time-destructors`, locked down with `-Werror` — write one global object that generates a global ctor/dtor and the build fails on the spot, no discussion entertained. `NoDestructor` is the official escape hatch Chromium shipped alongside this rule, purpose-built to slip past it:

Use a function-local static to dodge the global constructor (construct on first use, with magic statics guaranteeing thread safety); then use `NoDestructor` to dodge the global destructor (no destructor registered, period). Put the two "avoids" together and you've kept the rule while still getting a globally visible object.

The parts are on the table; next we get to see exactly how NoDestructor implements those two "avoids" in code. The next part to stock is placement new and aligned storage — the core mechanism holding up NoDestructor's implementation rests on it.

## References

- [cppreference: storage duration](https://en.cppreference.com/w/cpp/language/storage_duration)
- [cppreference: static initialization](https://en.cppreference.com/w/cpp/language/initialization)
- [cppreference: constinit (C++20)](https://en.cppreference.com/w/cpp/language/constinit)
- [The classic SIOF explainer — isocpp FAQ](https://isocpp.org/wiki/faq/ctors#static-init-order)
- [Design notes on Chromium's `base/no_destructor.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
