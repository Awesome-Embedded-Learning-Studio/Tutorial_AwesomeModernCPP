---
chapter: 7
cpp_standard:
- 17
- 20
- 23
description: A deep dive into `std::optional`—why we shouldn't use raw pointers, sentinel
  values, or `pair<bool, T>` to represent emptiness; construction and access (how
  `has_value`, `value`, `value_or`, and `operator*` result in UB when empty); the
  value semantics lifecycle of `emplace`/`reset`; and how C++23 monadic operations
  (`and_then`, `or_else`, `transform`) flatten a three-layer `if` chain into a single
  chain for "possibly empty" queries.
difficulty: intermediate
order: 61
platform: host
prerequisites:
- variant：类型安全的联合体
- vector 深入：三指针、扩容与迭代器失效
reading_time_minutes: 16
related:
- std::expected<T, E>：类型安全的错误传播
tags:
- host
- cpp-modern
- intermediate
- 类型安全
- optional
title: 'optional: Making "Possibly None" a Type'
translation:
  source: documents/vol3-standard-library/error-utils/61-optional.md
  source_hash: dde681d0aab852fd783542c8ab351904c2352cc882e479531c64c7c0ed3d5969
  translated_at: '2026-06-24T04:10:22.285719+00:00'
  engine: anthropic
  token_count: 4273
---
# optional: Making "Possibly Empty" a Type

Anyone who has written a search function is familiar with this kind of return value: return `-1` if not found, or the index if found. If we search for `10` in an array and get `-1`, we know it wasn't found. But what if the value we are looking for can legitimately be `-1`? Is that `-1` "found, value is -1" or "not found"? Looking at the return value alone, the type system offers no help; we must rely on comments and conventions. This "convention-based" approach is a ticking time bomb if someone unfamiliar with the conventions takes over the code.

`std::optional<T>` (available since C++17 in the `<optional>` header) solves exactly this pain point. It elevates "possibly having no value" from a comment or verbal agreement to a **fact enforced by the type system**: an `optional<int>` either contains an `int` or is empty. This "has value" state is part of the value itself, and you must confront it before accessing the data. In this post, we will cover the design motivation, construction and access, the undefined behavior caused by dereferencing an empty optional (a common pitfall), and the new monadic chain operations added in C++23. We will see clearly why it is worth using and how to use it correctly.

## First, a question: What is wrong with existing "empty" representations?

You might think, "I already have a bunch of ways to represent 'nothing', why do I need a specific `optional`?" Let's look at three common "homegrown" approaches and their flaws.

**First approach: Sentinel values.** Return `-1`, `nullptr`, or an empty string to indicate "not found". We already touched on the problem earlier—a sentinel is a **value within the valid range** that you have commandeered. If that value becomes meaningful in the business logic (e.g., index `-1` or an empty string as valid input), the convention contradicts itself. Furthermore, it relies entirely on human memory; the compiler will not check it for you.

**Second approach: Return a raw pointer `T*`.** Return a pointer to the result if found, or `nullptr` if not found. This looks clean, but it has two major issues. First is **ownership ambiguity**: when the caller receives a `T*`, they don't know who owns the object, whether they can delete it, or when it becomes invalid—does it point to an element inside a container (which becomes dangling if deleted) or to a heap object requiring `delete`? You cannot tell from the signature alone. Second is **incompatibility with value semantics**: a "box holding a value" is clearly a value type (copying, moving, and lifetime should behave like normal variables), but using a pointer forces it into reference semantics.

**Third approach: `pair<bool, T>` or `struct { bool ok; T value; }`.** This looks reasonable—it includes a flag to indicate presence. But the trap lies in "what is `value` upon failure?". Look at this actual test:

```cpp
// Standard: C++17
struct Result { bool ok; int value; };
Result find_pair(int needle, const int* a, int n) {
    for (int i = 0; i < n; ++i) if (a[i] == needle) return {true, i};
    return {false, 0};   // 失败时 value=0 是凑数的, 不是真结果
}
```

When the value is not found, we provide a `0` as a placeholder—but no one can guarantee that this `0` isn't a false positive. Even worse, the caller can directly access `.value` to use this placeholder `0`, potentially forgetting to check `.ok` first, and the compiler won't utter a word. `pair<bool, T>` also incurs a hidden cost: when `T` is a non-trivial type (like `string`), it must be default-constructed to fill the slot even on failure, resulting in wasted construction.

`optional` solves all these problems at once: "whether there is a value" is part of the type, not a detached `bool`; it is a **value type**, following value semantics for copy, move, and destruction without ownership ambiguity; when empty, `T` is not constructed at all, eliminating the waste of "fabricating a `T` on failure." Let's look at a `sizeof` comparison to get an intuitive impression:

```cpp
std::cout << "sizeof(int):                   " << sizeof(int) << "\n";
std::cout << "sizeof(optional<int>):         " << sizeof(std::optional<int>) << "\n";
std::cout << "sizeof(pair<bool,int>):        " << sizeof(Result) << "\n";
std::cout << "sizeof(string):                " << sizeof(std::string) << "\n";
std::cout << "sizeof(optional<string>):      " << sizeof(std::optional<std::string>) << "\n";
```

Here is the output from GCC 16.1.1:

```text
sizeof(int):                   4
sizeof(optional<int>):         8
sizeof(pair<bool,int>):        8
sizeof(int*):                  8
sizeof(string):                32
sizeof(optional<string>):      40
```

`optional<int>` is eight bytes—four bytes for the `int`, one byte for the "has value" flag, and three bytes for alignment padding. The overhead is the same as `pair<bool,int>`, but in exchange, you get type protection that forces you to handle emptiness before accessing the value, value semantics, and the laziness of not constructing `T` when empty. It's a good trade-off.

## Construction and Access: Four Ways to Get the Value

The `optional` API isn't huge, but retrieving the value involves several interfaces that look similar but behave very differently, so we need to distinguish them clearly. Let's walk through construction and access using a simple example:

```cpp
// Standard: C++17
#include <optional>
#include <vector>
#include <string>

std::optional<int> find_first_even(const std::vector<int>& v) {
    for (int x : v) if (x % 2 == 0) return x;
    return std::nullopt;   // 显式返回"空"
}

int main() {
    std::optional<int> empty;           // 默认构造: 空
    std::optional<int> a = 42;          // 从值构造
    std::optional<int> b{a};            // 拷贝构造

    // 访问的四种方式
    a.has_value();     // true:  显式问"有没有"
    (bool)a;           // true:  operator bool, 等价于 has_value()
    a.value();         // 42:    空时抛 std::bad_optional_access
    *a;                // 42:    空时是未定义行为(下面单独讲)
    a.value_or(0);     // 42:    空时返回参数里的默认值
}
```

Let's run through the whole process and check the actual output:

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

The difference between these four access methods boils down to one sentence: **How they handle the empty state determines which one you should use.**

- `has_value()` / `operator bool()` — Pure query, the safest option. Whether empty or not, nothing bad happens.
- `value()` — **Throws a `std::bad_optional_access` exception if empty**. Suitable for scenarios where "I'm too lazy to check for emptiness at the call site; if it's empty, the program logic is wrong, so throw it up for the upper layer to handle."
- `value_or(default)` — **Returns your provided default value if empty**. Best for fallback logic where "if it's empty, use the default value." It handles this in one line without needing `if`.
- `operator*` and `operator->` — **Undefined behavior if empty**. The fastest, but only if you have confirmed it is not empty.

Let's verify the behavior of `value()` throwing an exception, rather than just making empty claims:

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

The exception object's `what()` returns the string `"bad optional access"`. Note that `bad_optional_access` is derived from `std::logic_error`—meaning the standard library classifies it as a "program logic error" (failure to check for emptiness when one should have), rather than a "runtime sporadic error". In other words, relying on `value()` to handle exceptions is equivalent to admitting that "an empty value here is a bug"; do not use it for normal control flow.

## The Real Trap: Dereferencing an Empty Optional is Undefined Behavior

`value()` throws an exception if empty, but what about `*empty`? The standard is very clear: **dereferencing an empty optional is undefined behavior (UB)**. It won't check for emptiness for you, nor will it throw an exception—it is straight-up UB. The insidious thing about this is that it **usually doesn't crash**; you just carry on using a wrong result, until it explodes one day when you change compiler options or platforms.

We tested this with GCC 16.1.1. First, let's see what happens with the default compilation:

```cpp
// Standard: C++17
std::optional<int> empty;
std::cout << *empty << '\n';   // 空的解引用: UB
```

Compile and run directly using `g++ -std=c++23 -O2`:

```text
0
```

It didn't crash, and it printed a `0`. But don't be fooled by this `0`—it **is not the `optional` telling you "I am empty"**; it just happened to read the default zero value from that uninitialized memory. In a different scenario, with a different optimization level, or a different type, it could easily be any garbage value, or cause a segmentation fault directly. This is the terrifying nature of UB: the fact that it "runs" today is precisely the most dangerous signal.

::: warning ASan can't catch this UB
Many people's first reaction is "turn on AddressSanitizer to catch it." But in practice, ASan is **powerless** against this UB:

```text
O2 -fsanitize=address: 打印 0, 不报错, 正常退出
O2 -fsanitize=undefined: 打印 0, 不报错
```

The reason is that `optional` uses a **legally allocated union memory block** internally to hold the value. Dereferencing an empty `optional` reads from this memory—which is neither use-after-free (the memory is still alive) nor an out-of-bounds access (the size hasn't been exceeded)—so ASan/UBSan simply do not treat it as an error. This access of "read but never constructed" memory falls into the gray area of "live but uninitialized," which is invisible to runtime sanitizers.

To catch it, we rely on the built-in assertions in libstdc++. Compile the same program with `-D_GLIBCXX_ASSERTIONS`:

```text
/usr/include/c++/16.1.1/optional:1249: constexpr _Tp& std::optional<_Tp>::operator*() &
  [with _Tp = int]: Assertion 'this->_M_is_engaged()' failed.
退出码 134 (SIGABRT)
```

Inside `libstdc++`'s `operator*`, there is a hidden `__glibcxx_assert(this->_M_is_engaged())`. With `_GLIBCXX_ASSERTIONS` enabled, it checks for null at runtime and aborts if the value is empty. Whether to enable this macro in production builds (which incurs a minor performance cost) is a team decision, but **we strongly recommend enabling it during debugging**—it catches a large number of "seems to run" undefined behavior (UB) cases for you.

That said, relying on assertions is a safety net, not a basis for writing code. The correct mindset is: **use `operator*` only in contexts where you have confirmed it is not empty**—for example, right after an `if (opt)` check, or when `opt.has_value()` returns true. Otherwise, use `value()` (let exceptions signal the error) or `value_or()` (let a default value serve as the fallback). Entrusting null checks to UB is a debt that will eventually come due.
:::

## `emplace`, `reset`, and the Value Semantic Lifecycle

`optional` is a value type, which means it **manages the lifecycle of the contained `T` itself**: when you construct a non-empty `optional`, `T` is constructed; when the `optional` is destroyed, `T` is destroyed along with it; when you reassign or clear it, the old `T` is destroyed first. This automatic management is the core reason why `optional` is less error-prone than raw pointers. We will use a type with logging to make this lifecycle transparent:

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
    std::optional<User> opt;          // 空, 还没构造 User
    opt.emplace("alice", 30);         // 就地构造, 不产生临时对象
    opt->greet();                     // operator-> 访问成员

    opt.emplace("bob", 25);           // 再次 emplace: 先析构旧的, 再构造新的
    opt->greet();

    opt.reset();                      // 主动清空, 调用析构
    opt = std::nullopt;               // 赋值 nullopt, 等价于清空
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

A few details are worth noting. `emplace(args...)` performs "in-place construction"—it directly invokes `T`'s constructor on the storage inside the optional using `args`, without first generating a temporary `T` and then moving or copying it in. This is more efficient for non-trivial types (like this `User`) and expresses intent more clearly than `opt = User{...}`. `operator->` allows you to access members as if you were using a pointer (`opt->greet()`), but the prerequisite is the same: "non-empty"—using `operator->` on an empty optional is just as much undefined behavior (UB) as dereferencing one. `reset()` and `= nullopt` are two equivalent ways to clear the value; both destruct the currently held value and turn the optional into an empty state.

This semantic where "optional manages the lifecycle" stands in stark contrast to returning raw pointers. With a function returning a pointer, once the caller receives it, the lifecycle is **dangling**—it might point inside a container, to the heap, or to a static region. The behavior varies completely, and the signature reveals nothing. In contrast, returning `optional<T>` (by value) means the value resides inside the optional object itself. When the optional is destructed, the value is gone. The boundaries are clear, leaving no ambiguity regarding ownership.

## The Headline Feature of C++23: Monadic Operations

Everything up to this point has been available since C++17. C++23 adds three monadic interfaces to optional—`and_then`, `or_else`, and `transform`. These are the new features this article really wants to discuss, and they represent the most anticipated capabilities of optional.

Why do we need them? Consider a real-world scenario: given a username, we need to "look up user ID → look up email → extract email domain." Each of these three steps might fail (user doesn't exist, user didn't provide an email, or the email format is invalid so we can't get the domain). Writing this with C++17 optional looks like this:

```cpp
// Standard: C++17
std::string classic(const std::string& name) {
    auto uid = get_user_id(name);
    if (!uid) return "(no user)";          // 第一层判空
    auto email = get_email(*uid);
    if (!email) return "(no email)";       // 第二层判空
    auto dom = domain_of(*email);
    if (!dom) return "(no domain)";        // 第三层判空
    return *dom;
}
```

Three levels of nested `if`, each layer performing a "null check + value retrieval," fragments the logic completely. This "sequence of potentially failing steps" is extremely common in business logic. The traditional approach involves stacking layer upon layer of `if`, resulting in long code that is prone to missing checks. `and_then` is designed to eliminate these `if` statements—it accepts a function, **feeding the value to this function when the optional is non-empty, and propagating the empty state directly when it is empty.** Thus, the code above transforms into a single chain:

```cpp
// Standard: C++23
std::string monadic(const std::string& name) {
    return get_user_id(name)
        .and_then(get_email)          // optional<int>    -> optional<string>
        .and_then(domain_of)          // optional<string> -> optional<string>
        .value_or("(missing)");       // 链尾兜底
}
```

If any step in the chain returns an empty value, the rest of the chain automatically short-circuits to empty, and finally `value_or` provides a default value. Let's first verify that it runs correctly on GCC 16.1.1, and then compare the results with the traditional approach:

```text
name   classic         monadic
alice      'example.com'   'example.com'
bob      '(no email)'   '(missing)'
carol      '(no user)'   '(missing)'
```

`alice` successfully retrieves the domain `example.com` all the way through; `bob` fails at the "check email" step (no email provided), the traditional approach returns `(no email)`, while the monadic approach short-circuits to `(missing)`; `carol` fails at the very first step because the username doesn't exist, also short-circuiting. Both approaches have identical semantics, but the **control flow in the monadic version is linear and reads from left to right**, uninterrupted by `if` statements.

Memorize the differences between these three interfaces carefully; they look very similar, and mixing them up will result in a slew of concepts errors:

- **`and_then(f)`** — `f` takes a **value type `T`** and returns a **new `optional<U>`**. Its semantics are "potentially turning something into nothing" (`f` decides whether to return empty or not), making it suitable for chaining "queries where each step might fail". This is the workhorse of monadic chains.
- **`transform(f)`** — `f` takes a **value type `T`** and returns a **plain value `U` (not optional)**. It performs a pure "something to something" mapping, and **does not introduce new emptiness** (as long as the optional was non-empty, the result is non-empty). Suitable for "transforming a value without involving failure".
- **`or_else(f)`** — The inverse of the previous two: **`f` is not called if the optional is non-empty**, and it returns as-is; if empty, `f()` is called (note that `f` **takes no arguments**), and `f` must return an **`optional<T>` of the same type** as a fallback. Suitable for "providing a default or logging when empty".

Let's run through an example with `transform` and `or_else` respectively to nail down the semantics. First, look at `transform`: performing an "uppercase" mapping on a potentially existing username. This operation itself cannot fail, so we use `transform` instead of `and_then`:

```cpp
// Standard: C++23
std::string to_upper(std::string s) { /* 转大写 */ return s; }

std::optional<std::string> name{"alice"};
std::optional<std::string> empty;

auto big = name.transform(to_upper);         // 有值 -> 映射 -> 仍有值: ALICE
auto big_empty = empty.transform(to_upper);  // 空 -> 透传 -> 仍空
```

```text
name.transform(upper): ALICE
empty.transform(upper): (none)
```

Note that `empty.transform(to_upper)` is empty — `transform` does nothing on an empty `optional`, simply propagating the empty state without calling `to_upper`. Now let's look at `or_else`: when empty, it falls back to a default value and logs a message:

```cpp
// Standard: C++23
auto fallback = empty.or_else([] {
    std::cout << "  [or_else] 没值, 回退到 GUEST\n";
    return std::optional<std::string>{"GUEST"};
});
// empty 时: 打日志, 返回装着 "GUEST" 的 optional
// 非空时: 不调用, 原样返回
```

```text
  [or_else] 没值, 回退到 GUEST
empty.or_else(GUEST): GUEST
name.or_else(GUEST): alice  (or_else 没被调用)
```

Since `name` is not empty, `or_else` is not called at all, and `alice` is returned as is. This reflects its semantics of "keep it if you have it, or fall back if you don't."

::: warning Signature differences between the three interfaces — don't mix them up
These three interfaces最容易在**参数和返回值**上踩坑，混了就是一堆 concepts 报错：

- `and_then` and `transform` functions receive the **value `T`** (or a reference), not `optional<T>` — don't write `[](std::optional<int> o){...}`.
- `and_then` and `or_else` functions return an **`optional`**; `transform` functions return a **plain value**.
- The `or_else` function **takes no arguments** (there is no value to pass when it is empty), and the returned optional must be the **same type** as the original `optional<T>`. You cannot change the type.

To remember it in one sentence: `and_then`/`or_else` manipulate the optional itself (potentially changing whether it "has a value"), while `transform` performs a pure transformation on the contained value (without changing whether it "has a value").
:::

The value of these monadic interfaces is best demonstrated in "a sequence of steps that might fail." More importantly, this shares the **same philosophy** as C++23's `std::expected<T, E>` — `expected` is essentially "optional + error information." Its `and_then`/`or_else`/`transform` signatures are almost identical, with the only difference being that "empty" is replaced by "an unexpected value with an error cause." Once you master optional's monadic chain, you are halfway there with expected. We will expand on the comparison between the two in the article on expected.

## Move Semantics and C++20 constexpr

optional has complete support for move semantics: moving the value out of an optional, and moving one optional to another, both work exactly as you expect. However, there is a detail you need to watch out for — **after you `std::move(*opt)` the value away, the optional itself is unaware of this; it still reports `has_value()` as true**. Let's test this with a type that logs move operations:

```cpp
// Standard: C++17
auto o = make_box();              // optional<Box>, 装着 tag="payload"
Box taken = std::move(*o);        // 把值搬出来
// o 仍然 has_value()=true, 但里面的 Box 已是 moved-from 状态
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

After the move, `taken` now holds the `payload`, while the object inside `o` is in a moved-from state (the tag shows `(moved-from)`). Crucially, `o.has_value()` is still `true`—the "has a value" flag in `optional` remains untouched, so it is unaware that you have emptied the contents. Therefore, **do not access the value via `*o` after moving out** (a moved-from object is only guaranteed to be destructible or re-assignable). If you intend to make the `optional` empty, explicitly call `o.reset()` or assign `o = std::nullopt`.

Finally, let's cover a C++20 feature: **most `optional` operations are `constexpr`**, including construction, `emplace`, `reset`, `value_or`, and `operator*`. This means `optional` can be evaluated at **compile time** and used within `static_assert`:

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
    static_assert(compute() == 42);                // 编译期就定下来
    constexpr std::optional<int> empty;
    static_assert(empty.value_or(99) == 99);       // value_or 也 constexpr
}
```

```text
constexpr compute() = 42
empty.value_or(99) = 99
C++20 constexpr optional: OK
```

Want to see `optional` compile-time evaluation in action? Check out this online demo:

<OnlineCompilerDemo
  title="C++20 constexpr optional: Compile-time Evaluation"
  source-path="code/examples/vol3/61_optional_constexpr.cpp"
  description="emplace, reset, and value_or in optional are all constexpr: compute() and empty.value_or(99) fit into static_assert for compile-time verification, and runtime prints the same result"
  allow-run
/>

This is extremely useful for template metaprogramming, compile-time lookup tables, and `consteval` functions—whenever you need a "box that might be empty," you can use `optional` at compile time starting with C++20, no need to roll your own `union`.

## Performance Intuition: How Much Overhead Does `optional` Add on a Hot Path?

Many developers worry about `optional` performance. Intuitively, "an extra flag and an extra branch" seems like it would slow things down. Let's compare `optional<int>` + `value_or` against returning a raw `int` (using `-1` as a sentinel) in a hot path loop of 500 million iterations:

```cpp
// Standard: C++17
std::optional<int> lookup_opt(int i) { return i & 1 ? std::optional<int>{i} : std::nullopt; }
int                lookup_raw(int i) { return i & 1 ? i : -1; }

for (long i = 0; i < 500'000'000L; ++i) acc += lookup_opt(i).value_or(0);
```

Actual measurements (`g++ -std=c++23 -O2`, taking the median value from multiple runs):

```text
optional<int>.value_or(0): 132 ms
raw int:                   234 ms
```

```text
optional<int>.value_or(0): 192 ms
raw int:                   403 ms
```

The absolute values fluctuate significantly across runs (machine load has a major impact), but one robust conclusion remains: **`optional<int>.value_or` is on the same order of magnitude as directly returning an `int` on this hot path, and is often even faster**. It certainly does not suffer from being "an order of magnitude slower." This is due to the small size of `optional` at `-O2` (just an `int` plus a flag), the inlining of `value_or`, and modern CPU branch prediction, which optimize this overhead to near invisibility. **The conclusion is: do not avoid `optional` for performance reasons**—the type safety benefits it provides far outweigh the negligible, immeasurable cost. Of course, if your value type is large (e.g., a 1KB struct), `optional` will add storage for a flag and alignment padding, and copy overhead becomes a factor. In those cases, whether to pass an `optional` depends on the specific scenario.

## Common Real-World Pitfalls

Let's consolidate the places where it's easy to crash and burn; every point below has been verified through the tests above:

::: warning operator* / operator-> on Empty is UB
`*empty` and `empty->member` result in **undefined behavior**, not an exception. Under default compilation, they will likely "appear to work" (printing a `0`), trapping you in the deepest pit of despair. ASan/UBSan cannot catch this; you need `-D_GLIBCXX_ASSERTIONS` to trigger an abort at runtime. The rule: **Only use `*` and `->` in contexts where you have already checked for emptiness**. Otherwise, use `value()` (throws) or `value_or()` (fallback).

**`value()` or `operator*`?** The trade-off comes down to one question: Is "empty" here a "normal possibility I must handle," or "something that shouldn't happen and indicates a bug"? For the former, use `value_or` or check for emptiness before using `*`. For the latter, use `value()` to let the exception expose the bug. Don't use `value()` for normal control flow—the overhead and semantics are ill-suited for it.
:::

::: warning optional Remains Engaged After Move
`std::move(*opt)` moves the value out, but the optional's "has value" flag remains untouched, so `has_value()` is still `true`. Accessing `*opt` at this point yields a moved-from object (valid but with an unspecified state); you can only destroy it or reassign it. To make the optional truly empty, explicitly call `reset()` or assign `= nullopt`.
:::

::: warning Null Checks Are Not Free, But Cheap
`optional` adds a flag, so accessing the value always implicitly involves a "null check." In scenarios where you have already checked `if (opt)` and repeatedly use `*opt` in a loop, you can hoist the check out of the loop to save redundant checks. However, don't sacrifice readability for this micro-optimization—in the vast majority of cases, the compiler can optimize it away. Write correct code first.
:::

::: warning or_else Functions Take No Args and Must Match Return Type
The function passed to `or_else` is `f()` (no arguments), not `f(value)`—when empty, there is no value to pass. Furthermore, it must return **the exact same `optional<T>`**; you cannot use this to change types (use `and_then` for that). Mixing this up results in a cascade of concepts errors.
:::

## Summary

The core value of `std::optional` is elevating "possibly having no value" from comments and conventions into a **fact of the type system**. Here are the key takeaways:

- **Replaces Three "Hacks"**: Safer than sentinel values (no conflict with legitimate values), clearer than raw pointers (value semantics, no ownership ambiguity), and cleaner than `pair<bool, T>` (doesn't construct `T` when empty, couples flag and value tightly).
- **Four Ways to Access Values**: `has_value()`/`operator bool()` (query), `value()` (throws `bad_optional_access` if empty), `value_or(default)` (fallback if empty), `operator*`/`operator->` (UB if empty, use with extreme caution).
- **The Biggest Pitfall is Dereferencing an Empty Optional**: It is UB. Under default compilation, it likely won't crash (printing some garbage value), ASan misses it, and you need `-D_GLIBCXX_ASSERTIONS` to abort at runtime. The rule is to only use `*`/`->` after an explicit check.
- **Value Semantics Lifecycle**: `optional` manages the construction and destruction of `T`. `emplace` constructs in-place, `reset()`/`= nullopt` clears and destroys. After a move, the optional remains engaged; accessing it yields a moved-from object.
- **C++23 Monadic Operations are the Highlight**: `and_then` (chain operations that might fail, function returns optional), `transform` (pure mapping of the value, function returns plain value), `or_else` (fallback on empty, function takes no args and returns same optional). Together, they flatten nested "if-check" logic into a linear chain. This shares the same philosophy as `expected`.
- **Performance is Not an Issue**: On a hot path, `optional<int>` is on par with returning a raw `int`, so don't avoid it for performance. Since C++20, `optional` is also `constexpr`, making it usable at compile time.

In the next post, we will look at `std::expected<T, E>`—it's "optional plus a reason for error." When you need to know not just "that it failed," but "why it failed," this is its time to shine. Once you are proficient with `optional`'s monadic chains, you will pick up `expected` very quickly.

## References

- [cppreference: std::optional](https://en.cppreference.com/w/cpp/utility/optional) — Interface overview, `bad_optional_access`, C++20 constexpr notes
- [cppreference: std::optional::and_then, or_else, transform](https://en.cppreference.com/w/cpp/utility/optional/and_then) — Signatures and semantics of C++23 monadic interfaces
- [P0798R8 Monadic operations for std::optional](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0798r8.html) — Proposal introducing `and_then`/`or_else`/`transform` in C++23, including design rationale
- [cppreference: std::bad_optional_access](https://en.cppreference.com/w/cpp/utility/optional/bad_optional_access) — Exception type thrown by `value()` when empty
- libstdc++ source `/usr/include/c++/16.1.1/optional` — `__glibcxx_assert` and `_M_is_engaged` checks for `operator*` (GCC 16.1.1)
