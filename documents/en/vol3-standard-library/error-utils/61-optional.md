---
chapter: 7
cpp_standard:
- 17
- 20
- 23
description: A deep dive into std::optional—why raw pointers, sentinel values, and
  pair<bool, T> are the wrong way to represent emptiness; construction and access
  (has_value/value/value_or, and operator* being UB when empty); the value-semantics
  lifecycle of emplace/reset; and how C++23 monadic operations (and_then/or_else/transform)
  compress a three-layer if chain of "possibly empty" lookups into a single chain
difficulty: intermediate
order: 61
platform: host
prerequisites:
- 'variant: Type-Safe Unions and visit'
- 'Deep Dive into vector: Three Pointers, Reallocation, and Iterator Invalidation'
reading_time_minutes: 16
related:
- 'expected: Value or Error, C++23''s New Error Handling Paradigm'
tags:
- host
- cpp-modern
- intermediate
- 类型安全
- optional
title: 'optional: Making "Maybe Nothing" a Type'
translation:
  source: documents/vol3-standard-library/error-utils/61-optional.md
  source_hash: dde681d0aab852fd783542c8ab351904c2352cc882e479531c64c7c0ed3d5969
  translated_at: '2026-09-26T00:54:03+00:00'
  engine: anthropic
  token_count: 11000
---
# optional: Making "Maybe Nothing" a Type

Anyone who has written a lookup function knows this kind of return value all too well: return `-1` when nothing is found, the index when something is. Search an array for `10` and get `-1`, and you know it wasn't found. But what if the value you're looking for is itself allowed to be `-1`? Now is that `-1` "found, and the value is -1", or "not found"? Judging from the return value alone, the type system can't help you at all; all you have left is comments and conventions. And this "convention-based" style is a ticking time bomb the moment someone who doesn't know the convention takes over the code.

`std::optional<T>` (in the standard library since C++17, defined in `<optional>`) solves exactly this pain point. It promotes "possibly no value" from comments and verbal agreements to a **fact in the type system**: an `optional<int>` either holds an `int` or is empty, and that "present or not" is part of the value itself—you are forced to face it before taking the value out. In this article we'll run through optional's design motivation, construction and access, the undefined behavior of dereferencing an empty optional (the easiest trap to step into), and the monadic chaining operations new in C++23, so we can see clearly why it's worth using and how to use it correctly.

## First, a Question: Where the Existing "Empty" Representations Fall Short

You might think: I already have plenty of ways to represent "nothing", why bother with a dedicated `optional`? Let's lay out the three most common homegrown approaches and look at what's wrong with each.

**First: sentinel values.** Return `-1`, `nullptr`, or an empty string when nothing is found. The problem was already pointed out above—a sentinel is a **value from the legitimate value range** that you've commandeered. The moment that value itself becomes meaningful in the business domain (index `-1`, empty string as legal input), the convention contradicts itself. And it lives entirely in human memory; the compiler won't check it for you.

**Second: returning a raw pointer `T*`.** Return a pointer to the result if found, `nullptr` if not. This looks clean, but it has two headaches. One is **ownership ambiguity**: the caller receives a `T*` and has no idea who owns the pointee, whether it may be deleted, or when it goes stale—does it point at an element inside a container (deleting it leaves a dangling pointer), or at a heap object you're supposed to `delete` yourself? You can't tell from the signature alone. The other is that it **clashes with value semantics**: a "box holding a value" is clearly a value type (copying, moving, and lifetime should all behave like an ordinary variable), yet using a pointer turns it into reference semantics.

**Third: `pair<bool, T>` or `struct { bool ok; T value; }`.** Looks reasonable—a flag bit says whether there's a value. But the trap lies in "what is `value` on failure?". Look at this actual test:

```cpp
// Standard: C++17
struct Result { bool ok; int value; };
Result find_pair(int needle, const int* a, int n) {
    for (int i = 0; i < n; ++i) if (a[i] == needle) return {true, i};
    return {false, 0};   // value=0 on failure is filler, not a real result
}
```

When nothing is found, `value` gets a `0` as filler—and nothing guarantees that `0` isn't a false result. Worse, the caller can go straight to `.value` and use that filler `0`, forgetting to check `.ok` first, and the compiler won't utter a word. `pair<bool, T>` also carries a hidden cost: when `T` is a non-trivial type (say, `string`), even a failure has to default-construct an empty `T` to fill in—one construction spent for nothing.

`optional` fixes all of these in one stroke: "present or not" is part of the type, not a free-floating `bool`; it is a **value type**, with copy, move, and destruction all following value semantics, no ownership ambiguity; and when empty, no `T` is constructed inside at all, so there's none of that "must fabricate a `T` even on failure" waste. Let's look at a `sizeof` comparison for a first impression:

```cpp
std::cout << "sizeof(int):                   " << sizeof(int) << "\n";
std::cout << "sizeof(optional<int>):         " << sizeof(std::optional<int>) << "\n";
std::cout << "sizeof(pair<bool,int>):        " << sizeof(Result) << "\n";
std::cout << "sizeof(string):                " << sizeof(std::string) << "\n";
std::cout << "sizeof(optional<string>):      " << sizeof(std::optional<std::string>) << "\n";
```

Here's what it prints on GCC 16.1.1:

```text
sizeof(int):                   4
sizeof(optional<int>):         8
sizeof(pair<bool,int>):        8
sizeof(int*):                  8
sizeof(string):                32
sizeof(optional<string>):      40
```

`optional<int>` is 8 bytes—4 bytes hold the `int`, 1 byte is the "present or not" flag, and the remaining 3 bytes are alignment padding. That's the same size as `pair<bool,int>`, but in exchange you get type-level protection that forces you to face emptiness before taking the value, value semantics, and the laziness of not constructing `T` when empty. That's a bargain.

## Construction and Access: Four Ways to Get the Value

optional's API surface is small, but the value-access step has several interfaces that look alike yet behave very differently, and we need to tell them apart one by one. Let's walk through construction and access with one minimal example:

```cpp
// Standard: C++17
#include <optional>
#include <vector>
#include <string>

std::optional<int> find_first_even(const std::vector<int>& v) {
    for (int x : v) if (x % 2 == 0) return x;
    return std::nullopt;   // explicitly return "empty"
}

int main() {
    std::optional<int> empty;           // default construction: empty
    std::optional<int> a = 42;          // construct from a value
    std::optional<int> b{a};            // copy construction

    // the four ways to access
    a.has_value();     // true:  explicitly ask "is there a value"
    (bool)a;           // true:  operator bool, equivalent to has_value()
    a.value();         // 42:    throws std::bad_optional_access when empty
    *a;                // 42:    undefined behavior when empty (covered separately below)
    a.value_or(0);     // 42:    returns the argument's default when empty
}
```

Run the whole thing and look at the real output:

```text
empty.has_value(): 0
empty as bool:     no
a.has_value():     1
a.value():         42
*a:                42
a.value_or(0):     42
empty.value_or(0): 0
find {1,3,5,8,9}: 8
find {1,3,5,7}:   none
```

The difference between the four access methods really boils down to one sentence: **how each behaves when empty decides which one you should use.**

- `has_value()` / `operator bool()`—pure queries, the safest; nothing goes wrong whether empty or not.
- `value()`—**throws `std::bad_optional_access` when empty**. Fits the "I can't be bothered to check for empty at the call site; empty means the program logic is broken, so let it throw and let a higher layer handle it" scenario.
- `value_or(default)`—**returns the default you supply when empty**. Best for the "if it's empty, patch it with a default" fallback logic—one line, no `if` needed.
- `operator*` and `operator->`—**undefined behavior when empty**. Fastest, but only once you've already confirmed it's non-empty.

Since `value()` throws, let's actually test it rather than assert it on thin air:

```cpp
// Standard: C++17
std::optional<int> empty;
try {
    int v = empty.value();
} catch (const std::bad_optional_access& e) {
    std::cout << "caught: " << e.what() << '\n';
}
```

```text
caught: bad optional access
```

The exception object's `what()` returns exactly the string `"bad optional access"`. Note that `bad_optional_access` derives from `std::logic_error`—meaning the standard library classifies it as a "program logic error" (an empty-check that should have been there but wasn't), not a "sporadic runtime error". In other words, leaning on `value()`'s exception as your safety net amounts to admitting "an empty here is a bug"—don't use it as normal control flow.

## The Real Trap: Dereferencing an Empty optional Is Undefined Behavior

`value()` throws when empty—so what about `*empty`? The standard is quite clear: **dereferencing an empty optional is undefined behavior (UB)**. It won't check emptiness for you, and it won't throw—it's just straight-up UB. The insidious part is that it **usually doesn't crash**: you keep using a wrong result, until one day a different compiler flag or platform makes it blow up.

We tested on GCC 16.1.1; first, what happens under a default build:

```cpp
// Standard: C++17
std::optional<int> empty;
std::cout << *empty << '\n';   // dereferencing an empty optional: UB
```

Compile and run directly with `g++ -std=c++23 -O2`:

```text
0
```

No crash—it printed a `0`. But don't let that `0` fool you: it is **not the optional telling you "I'm empty"**; it just happened to read the default zeros in that uninitialized memory. Change the scenario, the optimization level, or the type, and it could just as well be any garbage value, or a straight segfault. That's what makes UB scary: the fact that it "works" today is precisely the most dangerous sign.

::: warning ASan cannot catch this UB
Many people's first instinct is "throw AddressSanitizer at it". But in actual tests, ASan is **powerless** against this UB:

```text
O2 -fsanitize=address: prints 0, no error, exits normally
O2 -fsanitize=undefined: prints 0, no error
```

The reason is that optional internally uses a **legitimately allocated chunk of union memory** to hold the value, and dereferencing an empty optional reads that memory—it's neither use-after-free (the memory is alive) nor out-of-bounds (the size isn't exceeded), so ASan/UBSan never treats it as an error. This access to "memory that was read but never constructed" lives in a gray zone of "active but uninitialized", invisible to runtime sanitizers.

To catch it, you need the assertions that ship with libstdc++. Compile the same program with `-D_GLIBCXX_ASSERTIONS`:

```text
/usr/include/c++/16.1.1/optional:1249: constexpr _Tp& std::optional<_Tp>::operator*() &
  [with _Tp = int]: Assertion 'this->_M_is_engaged()' failed.
exit code 134 (SIGABRT)
```

Hidden inside libstdc++'s `operator*` is a `__glibcxx_assert(this->_M_is_engaged())`; with `_GLIBCXX_ASSERTIONS` enabled it checks emptiness for you at runtime and aborts on the spot when empty. Whether to enable this macro in production builds (it costs a little performance) is a team trade-off, but **during debugging we strongly recommend turning it on**—it blocks a whole class of "looks like it works" UB for you.

That said, relying on assertions is a safety net, not a basis for writing code. The right mindset: **use `operator*` only in contexts where you've already confirmed non-emptiness**—say, right after an `if (opt)` check, or once `opt.has_value()` has come back true. Otherwise use `value()` (let the exception shout for you) or `value_or()` (let the default catch it). Handing the empty-check duty over to UB always comes due sooner or later.
:::

## emplace, reset, and the Value-Semantics Lifecycle

optional is a value type, which means it **manages the lifetime of the `T` inside all by itself**: construct a non-empty optional and the `T` gets constructed; destroy the optional and the `T` is destroyed with it; reassign or clear it, and the old `T` is destroyed first. This automatic management is the core of what makes optional less hassle than raw pointers. Let's make the lifecycle plain to see with a logging type:

```cpp
// Standard: C++17
struct User {
    std::string name;
    int age;
    User(std::string n, int a) : name{std::move(n)}, age{a} {
        std::cout << "  User(" << name << ", " << age << ") 构造\n";
    }
    ~User() { std::cout << "  User(" << name << ") 析构\n"; }
    void greet() const { std::cout << "  hi, 我是 " << name << ", " << age << " 岁\n"; }
};

int main() {
    std::optional<User> opt;          // empty: no User constructed yet
    opt.emplace("alice", 30);         // construct in place, no temporary created
    opt->greet();                     // operator-> member access

    opt.emplace("bob", 25);           // emplace again: destroy the old one first, then construct the new
    opt->greet();

    opt.reset();                      // actively clear: invokes the destructor
    opt = std::nullopt;               // assign nullopt, equivalent to clearing
}
```

```text
1. emplace 就地构造:
  User(alice, 30) 构造
  hi, 我是 alice, 30 岁
2. emplace 再次: 先析构旧的再构造新的
  User(alice) 析构
  User(bob, 25) 构造
  hi, 我是 bob, 25 岁
3. reset 主动清空:
  User(bob) 析构
4. 赋值 nullopt: 同样会析构当前值
```

A few details deserve attention. `emplace(args...)` means "construct in place"—it calls `T`'s constructor with `args` directly on the optional's internal storage, instead of first creating a temporary `T` and then moving/copying it in. That's more efficient for non-trivial types (like this `User`), and it reads more clearly than `opt = User{...}`. `operator->` lets you access members as if through a pointer (`opt->greet()`), with the same precondition—non-empty: using `operator->` on an empty optional is UB, just like dereferencing. `reset()` and `= nullopt` are two equivalent ways to clear; both destroy the currently held value and leave the optional empty.

This "optional manages the lifecycle" semantics contrasts sharply with returning raw pointers. With a function that returns a pointer, once the caller holds it, the lifetime is **up in the air**—pointing into a container's internals, to the heap, or to static storage, each behaving completely differently and none of it visible in the signature. Returning an `optional<T>` (a value) instead means the value lives inside the optional object itself: when the optional is destroyed, the value is gone with it. The boundary is crystal clear, with no ownership ambiguity whatsoever.

## The C++23 Headliner: Monadic Operations

Everything up to here has been C++17 material. C++23 adds three monadic interfaces to optional—`and_then`, `or_else`, `transform`. These are the genuinely new material this article wants to cover, and optional's most anticipated capability.

Why do we need them? Consider a real scenario: given a username, we want to "look up the user id → look up the email → extract the email domain". Each of the three steps can come up empty (user doesn't exist, user left no email, malformed email with no domain to extract). Written with C++17 optionals, it looks like this:

```cpp
// Standard: C++17
std::string classic(const std::string& name) {
    auto uid = get_user_id(name);
    if (!uid) return "(no user)";          // first emptiness check
    auto email = get_email(*uid);
    if (!email) return "(no email)";       // second emptiness check
    auto dom = domain_of(*email);
    if (!dom) return "(no domain)";        // third emptiness check
    return *dom;
}
```

Three nested layers of `if`, each doing "check empty + take the value", with the logic shredded to pieces. "A sequence of steps that can each fail" is extremely common in business code, and the traditional way to write it is piling `if` on `if`—long, and easy to miss a check. `and_then` exists to eliminate those `if`s—it takes a function and, **when the optional is non-empty, feeds the value to that function; when empty, it just passes the emptiness straight through**. So the code above becomes a single chain:

```cpp
// Standard: C++23
std::string monadic(const std::string& name) {
    return get_user_id(name)
        .and_then(get_email)          // optional<int>    -> optional<string>
        .and_then(domain_of)          // optional<string> -> optional<string>
        .value_or("(missing)");       // fallback at the end of the chain
}
```

If any step in the chain returns empty, the entire rest of the chain automatically short-circuits to empty, and `value_or` supplies a default at the end. Let's first confirm it runs on GCC 16.1.1, then compare against the classic version's results:

```text
name   classic         monadic
alice      'example.com'   'example.com'
bob      '(no email)'   '(missing)'
carol      '(no user)'   '(missing)'
```

`alice` sails all the way through and gets the domain `example.com`; `bob` comes up empty at the "look up email" step (no email on file)—the classic version returns `(no email)`, the monadic version short-circuits to `(missing)`; `carol`'s username doesn't exist in the first place, likewise short-circuiting. The two versions mean the same thing, but the monadic one's **control flow is linear, read left to right**, uninterrupted by `if`s.

Keep the distinctions between the three interfaces firmly in mind—they look so alike that mixing them up earns you a pile of concepts errors:

- **`and_then(f)`**—`f` receives the **value type `T`** and returns a **new `optional<U>`**. Semantically it "may turn something into nothing" (`f` itself decides whether to return empty or not), which suits chaining "lookups where every step can fail". This is the workhorse of the monadic chain.
- **`transform(f)`**—`f` receives the **value type `T`** and returns a **plain value `U` (not an optional)**. It only does the "something to something" pure mapping and **never introduces new emptiness** (as long as the optional was non-empty, the result is non-empty). Suits "transform the value once, no failure involved" situations.
- **`or_else(f)`**—the reverse of the other two: **when the optional is non-empty, `f` is not called and the optional is returned as-is; when empty, `f()` is invoked** (note that `f` **takes no parameters**), and `f` must return an **optional of the same type, `optional<T>`**, as the fallback. Suits "if empty, supply a default / log something".

Let's pin the semantics down with one example each for `transform` and `or_else`. First, `transform`: uppercase-ify a username that may not exist—an operation that cannot itself fail—so `transform` rather than `and_then`:

```cpp
// Standard: C++23
std::string to_upper(std::string s) { /* convert to uppercase */ return s; }

std::optional<std::string> name{"alice"};
std::optional<std::string> empty;

auto big = name.transform(to_upper);         // has value -> map -> still has value: ALICE
auto big_empty = empty.transform(to_upper);  // empty -> pass through -> still empty
```

```text
name.transform(upper): ALICE
empty.transform(upper): (none)
```

Note that `empty.transform(to_upper)` is empty—`transform` does nothing to an empty optional, it just passes the emptiness along without ever calling `to_upper`. Now `or_else`: fall back to a default when empty, logging along the way:

```cpp
// Standard: C++23
auto fallback = empty.or_else([] {
    std::cout << "  [or_else] 没值, 回退到 GUEST\n";
    return std::optional<std::string>{"GUEST"};
});
// when empty: logs and returns an optional holding "GUEST"
// when non-empty: not called, returned as-is
```

```text
  [or_else] 没值, 回退到 GUEST
empty.or_else(GUEST): GUEST
name.or_else(GUEST): alice  (or_else 没被调用)
```

`name` is non-empty, so `or_else` is never called at all and `alice` is returned untouched. That's its "keep it if present, fall back if not" semantics.

::: warning Signature differences among the three interfaces—don't mix them up
These three interfaces bite most easily on **parameters and return values**—mix them up and you get a pile of concepts errors:

- For `and_then` and `transform`, the function receives the **value `T`** (or a reference), not an `optional<T>`—don't write `[](std::optional<int> o){...}`.
- For `and_then` and `or_else`, the function returns an **optional**; for `transform`, it returns a **plain value**.
- `or_else`'s function **takes no parameters** (when empty there's no value to pass in the first place), and the optional it returns must be the **same type** `optional<T>` as the original—no type changes allowed.

One-sentence mnemonic: `and_then`/`or_else` manipulate the optional itself (they may change "present or not"), while `transform` only applies one pure transformation to the contained value ("present or not" stays put).
:::

The value of this monadic interface set shows best in "a sequence of steps that can each fail". More importantly, it shares **the same design** with C++23's `std::expected<T, E>`—`expected` is "optional + error information", and its `and_then`/`or_else`/`transform` signatures are nearly identical; the only difference is that "empty" is replaced by "an unexpected value carrying the failure reason". Learn optional's monadic chain and you already know half of expected's. We'll unpack the comparison between the two in the expected article.

## Move Semantics and C++20 constexpr

optional's support for move semantics is complete: "moving out" the value inside an optional, or moving one optional into another, both work the way you'd expect. But one detail needs a close look—**after you haul the value away with `std::move(*opt)`, the optional itself has no idea; it still reports `has_value()` as true**. Let's test with a type that prints move logs:

```cpp
// Standard: C++17
auto o = make_box();              // optional<Box> holding tag="payload"
Box taken = std::move(*o);        // move the value out
// o still has has_value()=true, but the Box inside is now in a moved-from state
```

```text
--- 从 optional 移出值 ---
  Box(payload) ctor
  Box(payload) MOVE
  o has payload
  Box(payload) MOVE          <-- std::move(*o) 触发移动构造
  taken.tag = payload
  o still has_value=1 (optional 不知道值被搬空了, 仍 engaged)
  o->tag = (moved-from)  (moved-from 状态, 别用)
```

After the move, `taken` holds `payload`, while the object inside `o` is in a moved-from state (the tag reads `(moved-from)`). The key point: `o.has_value()` is still `true`—optional's "present or not" flag never moved; it doesn't know you hollowed the value out. So **after moving out, don't access that value through `*o` again** (a moved-from object only guarantees destruction and reassignment). If you really do want the optional empty, say so explicitly: `o.reset()` or `o = std::nullopt`.

Finally, a C++20 capability: **the overwhelming majority of optional's operations are `constexpr`**, including construction, `emplace`, `reset`, `value_or`, and `operator*`. That means optional can be evaluated at **compile time** and stuffed into a `static_assert`:

```cpp
// Standard: C++20
constexpr int compute() {
    std::optional<int> o;
    o.emplace(7);
    int v = *o;
    o.reset();
    return v + 35;          // 42
}

int main() {
    static_assert(compute() == 42);                // settled at compile time
    constexpr std::optional<int> empty;
    static_assert(empty.value_or(99) == 99);       // value_or is constexpr too
}
```

```text
constexpr compute() = 42
empty.value_or(99) = 99
C++20 constexpr optional: OK
```

Want to run it and watch optional get evaluated at compile time? Open this live example:

<OnlineCompilerDemo
  title="C++20 constexpr optional: compile-time evaluation"
  source-path="code/examples/vol3/61_optional_constexpr.cpp"
  description="emplace/reset/value_or on optional are all constexpr: compute() and empty.value_or(99) can go straight into a static_assert for compile-time verification, and running it prints the same results"
  allow-run
/>

This is extremely useful in template metaprogramming, compile-time table lookups, and `consteval` functions—whenever you need a "possibly empty" box, optional works at compile time too from C++20 on, no more hand-rolling your own union.

## A Dose of Performance Intuition: How Much optional Costs on a Hot Path

Plenty of people worry about optional's performance. Intuitively, "one extra flag bit and one extra branch" sounds like it would slow things down. So on a hot path of 500 million loop iterations, let's compare `optional<int>` + `value_or` against returning a plain `int` directly (with `-1` as the sentinel):

```cpp
// Standard: C++17
std::optional<int> lookup_opt(int i) { return i & 1 ? std::optional<int>{i} : std::nullopt; }
int                lookup_raw(int i) { return i & 1 ? i : -1; }

for (long i = 0; i < 500'000'000L; ++i) acc += lookup_opt(i).value_or(0);
```

Measured (`g++ -std=c++23 -O2`, representative values from multiple runs):

```text
optional<int>.value_or(0): 132 ms
raw int:                   234 ms
```

```text
optional<int>.value_or(0): 192 ms
raw int:                   403 ms
```

The absolute numbers wobble quite a bit between runs (machine load dominates), but one robust conclusion holds: **`optional<int>.value_or` is in the same ballpark as returning a raw int on this hot path—often even faster**—and it never turns in "an order of magnitude slower". The reason is that under `-O2`, optional's small footprint (just an `int` plus a flag bit), the inlining of `value_or`, and modern CPUs' branch prediction render this bit of overhead nearly invisible. **The takeaway: don't avoid optional for performance reasons**—the type safety it buys is worth far more than overhead you can't even measure. Of course, if your value type is itself large (say, a 1 KB struct), optional stores an extra flag byte plus alignment padding and copy costs enter the picture; whether to pass an optional around then depends on the concrete scenario.

## A Few Pitfalls People Actually Hit

Let's collect the spots where this journey tends to flip over—every item below was verified by the tests above:

::: warning operator* / operator-> on an empty optional is UB
`*empty` and `empty->member` are **undefined behavior**, not an exception. Under a default build they most likely "look fine" (printing some `0`), which buries the trap at its deepest. ASan/UBSan can't catch it; you need `-D_GLIBCXX_ASSERTIONS` to get a runtime abort. The rule: **use `*` and `->` only in contexts where you've already checked for emptiness**; otherwise use `value()` (throws) or `value_or()` (fallback).

**`value()` or `operator*`?** The trade-off is a single question: is emptiness here "a normal occurrence that I must handle", or "something that shouldn't happen, and happening means a bug"? For the former, use `value_or` or check-then-`*`; for the latter, use `value()` and let the exception expose the bug for you. Don't use `value()` as normal control flow—neither the cost nor the semantics of exceptions fit.
:::

::: warning After moving out, the optional is still engaged
`std::move(*opt)` hauls the value away, but the optional's "present or not" flag doesn't move, and `has_value()` still returns `true`. Accessing `*opt` at that point yields a moved-from object (legal but in an unspecified state), good only for destruction or reassignment. To truly empty the optional, explicitly `reset()` or `= nullopt`.
:::

::: warning optional's emptiness check isn't free, but it's cheap
optional carries one extra flag bit, and every value access implies an emptiness check. In scenarios where you've already done `if (opt)` and then use `*opt` repeatedly inside a loop, you can hoist the check outside the loop and save the repeated tests. But don't sacrifice readability for that micro-optimization—in the vast majority of cases the compiler optimizes it away on its own; get the code right first.
:::

::: warning or_else's function takes no parameters, and the return type must match
`or_else`'s function is `f()` (no parameters), not `f(value)`—when empty there's simply no value to pass. And it must return the **same `optional<T>`**; no sneaking in a type change (for that, use `and_then`). Mix them up and you get a string of concepts errors.
:::

## Summary

`std::optional`'s core value is promoting "possibly no value" from comments and conventions into **a fact of the type system**. Let's collect the key conclusions:

- **Replaces the three homegrown approaches**: safer than sentinel values (no collision with legitimate values), clearer than raw pointers (value semantics, no ownership ambiguity), cleaner than `pair<bool, T>` (no `T` constructed when empty; the flag and the value are bound together).
- **Four ways to get the value**: `has_value()`/`operator bool()` (query), `value()` (throws `bad_optional_access` when empty), `value_or(default)` (fallback when empty), `operator*`/`operator->` (UB when empty—use with care).
- **The biggest pitfall is dereferencing an empty optional**: UB that usually doesn't crash under default builds (it prints some filler value); ASan can't catch it, and only `-D_GLIBCXX_ASSERTIONS` aborts at runtime. The rule: use `*`/`->` only after checking for emptiness.
- **A value-semantics lifecycle**: optional manages `T`'s construction and destruction itself; `emplace` constructs in place, `reset()`/`= nullopt` clears and invokes the destructor; after a move-out the optional is still engaged, and access yields a moved-from object.
- **The C++23 monadic operations are the headliner**: `and_then` (chains steps that can fail; the function returns an optional), `transform` (pure mapping over the value; the function returns a plain value), `or_else` (fallback when empty; the function takes no parameters and returns the same optional type). Working together, they compress "layer upon layer of if-based emptiness checks" into one linear chain—and they share the same design with `expected`.
- **Performance is a non-issue**: on hot paths `optional<int>` is in the same class as returning `int` directly, so don't avoid it for performance; and since C++20 optional is `constexpr`, usable at compile time too.

Next up is `std::expected<T, E>`—it is "optional + failure reason". When you need to know not just "it failed" but "why it failed", that's its cue. Once you're fluent in optional's monadic chains, expected's will come quickly.

## References

- [cppreference: std::optional](https://en.cppreference.com/w/cpp/utility/optional) — interface overview, `bad_optional_access`, notes on C++20 constexpr
- [cppreference: std::optional::and_then, or_else, transform](https://en.cppreference.com/w/cpp/utility/optional/and_then) — signatures and semantics of the C++23 monadic interfaces
- [P0798R8 Monadic operations for std::optional](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0798r8.html) — the proposal that introduced `and_then`/`or_else`/`transform` in C++23, including the design motivation
- [cppreference: std::bad_optional_access](https://en.cppreference.com/w/cpp/utility/optional/bad_optional_access) — the exception type thrown by `value()` on an empty optional
- libstdc++ source `/usr/include/c++/16.1.1/optional` — the `__glibcxx_assert` and `_M_is_engaged` check inside `operator*` (GCC 16.1.1)
