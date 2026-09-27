---
chapter: 1
cpp_standard:
- 23
description: "Six categories of systematically designed test cases verifying every core behavior of OnceCallback, plus a performance comparison against the original Chromium implementation and standard-library options"
difficulty: beginner
order: 6
platform: host
prerequisites:
- 'OnceCallback hands-on (II): building the core skeleton'
- 'OnceCallback hands-on (III): implementing bind_once'
- 'OnceCallback hands-on (IV): designing the cancellation token'
- 'OnceCallback hands-on (V): chaining with then'
reading_time_minutes: 8
related:
- 'OnceCallback prerequisite (V): std::move_only_function (C++23)'
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
title: 'OnceCallback hands-on (VI): tests and performance comparison'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/01-6-once-callback-testing-and-perf.md
  source_hash: e00cd9b22a6ded35cf41b1f001be03d31d638cce098a63521c5c1f91c97cfbac
  translated_at: '2026-09-26T00:39:05+00:00'
  engine: anthropic
  token_count: 3200
---
# OnceCallback hands-on (VI): tests and performance comparison

The core skeleton, `bind_once`, the cancellation token, the `then()` chaining — all four pieces are in place, the code compiles, and it runs correctly. But we didn't dare breathe easy after writing it, because a whole suite of tests still stands between "it runs" and "it's right in every corner case". This piece fills those tests in, and while we're at it, we lay out and measure the one thing we care about most: this thing we cobbled together on top of `std::move_only_function` — set against Chromium's two-thousand-plus lines of hand-written original, exactly how much fatter is it, how much slower, and what did we get in exchange for all that extra weight.

## Setting up the test framework

We use Catch2 v3 as the test framework, with dependencies pulled automatically through CPM (CMake Package Manager).

```cmake
# test/CMakeLists.txt
CPMAddPackage("gh:catchorg/Catch2@3.7.1")

add_executable(test_once_callback test_once_callback.cpp)
target_link_libraries(test_once_callback PRIVATE once_callback Catch2::Catch2WithMain)
target_compile_options(test_once_callback PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_once_callback COMMAND test_once_callback)
```

Our original move from `assert()` to Catch2 came down to two things: `REQUIRE` spits out the failed expression together with the file name and line number, and subsequent checks inside the same `TEST_CASE` keep running afterward (unlike `assert`, which detonates once and stops everything); and `REQUIRE_THROWS_AS` keeps a dedicated eye on exception types. That exception-watching one is especially useful for our cancellation machinery — you'll see it later on.

Running the tests is the same old routine: from `build/`, `cmake --build . && ctest`.

---

## Six categories of test cases

We split the tests into six categories, each one staring down a single design invariant. Why sort by invariants rather than by features? Because once you finish a feature list, it's easy to feel touched by your own thoroughness — "everything that needed testing is tested" — while the corner cases quietly leak out the back. An invariant is a hard constraint of the form "this must hold no matter what"; orbit around it, and the boundaries surface on their own.

### Category A: basic invocation and return values

```cpp
TEST_CASE("non-void return", "[once_callback]") {
    OnceCallback<int(int, int)> cb([](int a, int b) { return a + b; });
    int result = std::move(cb).run(3, 4);
    REQUIRE(result == 7);
}

TEST_CASE("void return", "[once_callback]") {
    bool called = false;
    OnceCallback<void()> cb([&called] { called = true; });
    std::move(cb).run();
    REQUIRE(called);
}
```

The two plainest cases: a non-void callback has to carry its return value out, and a void callback has to run to completion. The void one walks the other branch of `if constexpr (std::is_void_v<ReturnType>)` — we kept it there on purpose, because that is exactly the spot where a template's two paths most easily end up with only one of them tested.

### Category B: move semantics

```cpp
TEST_CASE("move-only capture", "[once_callback]") {
    auto ptr = std::make_unique<int>(42);
    OnceCallback<int()> cb([p = std::move(ptr)] { return *p; });
    int result = std::move(cb).run();
    REQUIRE(result == 42);
}

TEST_CASE("move semantics: source becomes null", "[once_callback]") {
    OnceCallback<int()> cb([] { return 1; });
    OnceCallback<int()> cb2 = std::move(cb);
    REQUIRE(cb.is_null());

    int result = std::move(cb2).run();
    REQUIRE(result == 1);
}
```

The move-only capture case is our non-negotiable bar: it directly proves that what sits underneath really is `std::move_only_function` and not a corner-cutting `std::function` — with the latter, that line wouldn't even compile. The second case checks that the source object goes null after move construction, which is OnceCallback's "a move guts the source" contract.

There's a distinction here that took us a while to straighten out at first: moving is just relocating, not consuming. The only thing that actually runs the callback off is `run()`. After `OnceCallback cb2 = std::move(cb1)`, the callback is alive and well — it merely changed address — and only at `cb2.run()` is it consumed. Confuse those two things and you'll be thoroughly lost when writing the cancellation token later on.

### Category C: the call-once constraint

This category has no runtime test, because the constraint is stopped at compile time — deducing this working with `static_assert` makes `cb.run()` (no move) fail to compile outright, and only `std::move(cb).run()` gets through. The fact that it compiles is itself the verification passing. We first thought about adding a `TEST_CASE` for this one, then decided to drop it: this invariant is, in essence, "wrong code fails to compile", and forcing a test through the `static_assert(!std::is_invocable_v<...>)` machinery would be the roundabout way — better to let the compiler serve as the referee.

### Category D: argument binding

```cpp
TEST_CASE("bind_once basic", "[bind_once]") {
    auto bound = bind_once<int(int)>([](int a, int b) { return a * b; }, 5);
    int result = std::move(bound).run(8);
    REQUIRE(result == 40);
}

TEST_CASE("bind_once with member function", "[bind_once]") {
    struct Calc {
        int multiply(int a, int b) { return a * b; }
    };
    Calc calc;
    auto bound = bind_once<int(int)>(&Calc::multiply, &calc, 5);
    int result = std::move(bound).run(8);
    REQUIRE(result == 40);
}
```

Both kinds of binding get a pass: partial argument binding on an ordinary lambda, plus member-function binding. In the member-function case we deliberately left the bare-pointer spelling `&calc` in the test — it's there to hammer one point home: lifetime responsibility rests entirely on the caller's shoulders. The traps in that area were taken apart in an earlier piece, so we won't rehash them here.

### Category E: the cancellation mechanism

```cpp
TEST_CASE("is_cancelled respects cancel token", "[once_callback]") {
    auto token = std::make_shared<CancelableToken>();
    OnceCallback<void()> cb([] {});
    cb.set_token(token);

    REQUIRE_FALSE(cb.is_cancelled());
    token->invalidate();
    REQUIRE(cb.is_cancelled());
}

TEST_CASE("cancelled void callback does not execute", "[once_callback]") {
    auto token = std::make_shared<CancelableToken>();
    bool called = false;
    OnceCallback<void()> cb([&called] { called = true; });
    cb.set_token(token);
    token->invalidate();

    std::move(cb).run();
    REQUIRE_FALSE(called);
}

TEST_CASE("cancelled non-void callback throws", "[once_callback]") {
    auto token = std::make_shared<CancelableToken>();
    OnceCallback<int()> cb([] { return 1; });
    cb.set_token(token);
    token->invalidate();

    REQUIRE_THROWS_AS(std::move(cb).run(), std::bad_function_call);
}
```

We spent the most time on the cancellation machinery, because it has three branches that each need their own test: a token still alive — no cancellation; a token invalidated with a void callback — silent non-execution; a token invalidated with a non-void callback — throws `std::bad_function_call`. Why must the third one throw? Because the caller is waiting on a return value, and you can't just swallow it silently and hand back a default — that would bury the bug down in runtime territory. The three cases pin exactly those three branches down.

### Category F: then composition

```cpp
TEST_CASE("then chains two callbacks", "[then]") {
    auto cb = OnceCallback<int(int)>([](int x) { return x * 2; })
                  .then([](int x) { return x + 10; });
    int result = std::move(cb).run(5);
    REQUIRE(result == 20);  // 5 * 2 + 10
}

TEST_CASE("then multi-level pipeline", "[then]") {
    auto pipeline = OnceCallback<int(int)>([](int x) { return x * 2; })
                        .then([](int x) { return x + 10; })
                        .then([](int x) { return std::to_string(x); });
    std::string result = std::move(pipeline).run(5);
    REQUIRE(result == "20");
}

TEST_CASE("then with void first callback", "[then]") {
    int value = 0;
    auto cb = OnceCallback<void(int)>([&value](int x) { value = x; })
                  .then([&value] { return value * 3; });
    int result = std::move(cb).run(7);
    REQUIRE(result == 21);
}
```

All three compositions get walked through: the two-level non-void pipeline is the most common usage; the multi-level pipeline deliberately crosses a type boundary (int walking into string) to verify that `then`'s type deduction doesn't drop the ball when the return type changes; and the void-prefix callback case is one we added later — when `then` hangs off a void callback, the next stage gets no "return value" from the previous step and has to relay through external state. That boundary slipped past us in the first version.

---

## Performance comparison: versus the original Chromium implementation

With the tests all green, we arrive at the part we've been most curious about: our hand-assembled OnceCallback set beside Chromium's original — the one with hand-written reference counting and function-pointer tables — and how far apart do they actually land. Let's say it up front: the gap is real, and we have no intention of sugarcoating it.

### Object size

```cpp
std::cout << "sizeof(std::function<void()>):        "
          << sizeof(std::function<void()>) << " bytes\n";
std::cout << "sizeof(std::move_only_function<void()>): "
          << sizeof(std::move_only_function<void()>) << " bytes\n";
// Chromium OnceCallback<void()> ≈ 8 bytes

std::cout << "sizeof(OnceCallback<void()>): "
          << sizeof(OnceCallback<void()>) << " bytes\n";
// ours: move_only_function (32) + status (1) + token ptr (16) + padding
// estimated 56-64 bytes
```

On GCC the typical numbers look like this: `std::function` is about 32 bytes, `std::move_only_function` is also around 32 bytes, and our `OnceCallback`, with the status and the token pointer stacked on top, stretches to 56 to 64 bytes. And Chromium's? 8 bytes. The price of a single pointer.

A sevenfold gap like that made us blink at first, but it straightens out once you follow the storage strategy through. Chromium stuffs the bound arguments, the function pointers, the reference count — all of that state — into a heap-allocated `BindState`, and the callback object proper holds nothing but one pointer. We went down `std::move_only_function`'s SBO route: small lambdas sit inline in the object itself, saving one heap allocation, at the cost of an object that is plumper all around.

### Allocation behavior

This section is in fact the only place where our approach comes out ahead. `std::move_only_function`'s SBO threshold usually lands at two or three pointers' worth (16 to 24 bytes); a lambda capturing a few arguments fits inside just about every time, so no heap allocation is triggered. Only a big lambda capturing a whole pile goes to the heap for memory at construction.

Chromium is the other way around: it heap-allocates unconditionally (`new BindState`), but allocates exactly once, and from then on moving a OnceCallback is copying a single 8-byte pointer — cheap to the point of embarrassment. On our side, small objects don't allocate, but once a move happens, an inline buffer of 32 bytes and up has to be copied. One side saves on allocation, the other on moving — each takes one end.

### Indirect-call overhead

When it comes down to the actual call, the two sides tie — both pay one indirect function call. `std::move_only_function::operator()` and Chromium's `polymorphic_invoke_` go through the same style of dispatch. Under `-O2`, neither side can eliminate that indirect call: a function pointer crossing translation units is something the compiler dares not inline.

### Trade-off summary

| Metric | Our approach | Chromium's approach |
|---------|-----------|--------------|
| Callback object size | 56-64 bytes | 8 bytes |
| Heap allocation for small lambdas | None (SBO) | Always |
| Move cost | Copies 32+ bytes | Copies 1 pointer |
| Implementation code size | ~200 lines | ~2000+ lines |

We've gone back over those four rows several times, and the most valuable one is the last: the code size differs by an order of magnitude. We don't hand-write reference counting, we don't maintain a function-pointer table, and we don't hang `TRIVIAL_ABI` annotations off the class to haggle with the compiler — `std::move_only_function` carries all of that. The price of that exchange: an object seven times fatter and a move several times dearer. And the zero-heap-allocation line for small lambdas turns out to be an unexpected bonus in scenarios where the posting frequency isn't high.

Whether that ledger balances depends on what you're doing with it. For teaching, for prototypes, and for most ordinary business callbacks, we think it's worth it; but if you're wedging this into a Chromium-style hot path — one process with tens of thousands of callbacks hanging off it, callbacks that `[[clang::trivial_abi]]` pushes into registers for argument passing — then go copy their two thousand lines, honestly. This version's positioning has been clear from the start: explain the machinery thoroughly, lay the trade-offs out on the table, and as for which end weighs heavier — that's your call.

## References

- [Chromium base/functional/ source directory](https://source.chromium.org/chromium/chromium/src/+/main:base/functional/)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [Catch2 documentation](https://github.com/catchorg/Catch2/tree/devel/docs)
