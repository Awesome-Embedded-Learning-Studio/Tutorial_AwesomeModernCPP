---
chapter: 1
cpp_standard:
- 23
description: "Systematically design six categories of test cases to verify every core behavior of OnceCallback, then measure how our version compares with the original Chromium implementation and a std::function baseline"
difficulty: beginner
order: 6
platform: host
prerequisites:
- 'OnceCallback hands-on (II): the core skeleton'
- 'OnceCallback hands-on (III): bind_once'
- 'OnceCallback hands-on (IV): the cancellation token'
- 'OnceCallback hands-on (V): then chaining'
reading_time_minutes: 8
related:
- 'OnceCallback prerequisite (V): std::move_only_function'
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
title: 'OnceCallback Hands-On (VI): Tests and Performance'
---
# OnceCallback Hands-On (VI): Tests and Performance

The core skeleton, `bind_once`, the cancellation token, the `then()` chaining, four pieces all assembled. It compiles, and it runs. We did not let out a breath at that point, because "it runs" and "it is correct in every corner case" are separated by a full test suite. This piece fills in that suite, and it also opens up the one question we have been most curious about: how much heavier and slower is the thing we built on `std::move_only_function` compared with the two-thousand-plus lines Chromium hand-rolled? Where did that weight come from, and what did it buy?

## Building the test framework

We use Catch2 v3, and pull it through CPM (CMake Package Manager).

```cmake
# test/CMakeLists.txt
CPMAddPackage("gh:catchorg/Catch2@3.7.1")

add_executable(test_once_callback test_once_callback.cpp)
target_link_libraries(test_once_callback PRIVATE once_callback Catch2::Catch2WithMain)
target_compile_options(test_once_callback PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_once_callback COMMAND test_once_callback)
```

We moved off `assert()` to Catch2 mostly for two things. `REQUIRE` prints the failed expression, the file, and the line, and the rest of the checks inside the same `TEST_CASE` still run (unlike `assert`, which halts on the first failure). `REQUIRE_THROWS_AS` pins down the exact exception type, which matters a lot for the cancellation paths further down.

Running the tests is the usual drill: from `build/`, `cmake --build . && ctest`.

---

## Six categories of test cases

We split the tests into six categories, each one guarding a single design invariant. Why group by invariant instead of by feature? A feature list feels reassuring, you tick boxes and think you covered everything, while the corners slip through. An invariant is a hard contract that must hold in every situation, and if you build tests around it, the edges surface on their own.

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

The two most basic cases. A non-void callback must carry its return value out, and a void callback must run to completion. The void case takes the other branch of `if constexpr (std::is_void_v<ReturnType>)`, and we keep it explicitly because that two-path template split is exactly where it is easy to test one and forget the other.

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

The first case, move-only capture, is a hard line for us. It is direct proof that the storage really is `std::move_only_function` and not a `std::function` we reached for out of laziness; with `std::function`, that line would not compile. The second case checks that the source is hollowed out after a move, which is the "move means empty" contract OnceCallback leans on.

One distinction tripped us up at first. Moving is a relocation, not a consumption. Only `run()` actually consumes the callback. After `OnceCallback cb2 = std::move(cb1)`, the callback is alive and well, it just lives at a new address, and it is not consumed until `cb2.run()`. Confuse those two and the cancellation token work later turns into a headache.

### Category C: the single-invocation constraint

This category has no runtime test, because the constraint is enforced at compile time. Deducing this plus `static_assert` rejects `cb.run()` (no move) outright, and only `std::move(cb).run()` gets through. The fact that it compiles is the verification. We considered writing a `TEST_CASE` for it, then dropped the idea; the invariant is essentially "wrong code fails to compile", and forcing it through `static_assert(!std::is_invocable_v<...>)` is more contorted than letting the compiler referee.

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

Both binding flavors go through. A partial bind on a plain lambda, plus a member-function bind. We left the raw `&calc` form in the member test on purpose, as a standing reminder: lifetime responsibility rests entirely on the caller. The trap was unpacked in an earlier piece, so we do not repeat it here.

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

We spent the most time here, because cancellation has three branches that each need their own test. The token is alive, so no cancellation. The token is dead and the callback is void, so the run is silently skipped. The token is dead and the callback is non-void, so the run throws `std::bad_function_call`. Why must the third one throw? The caller is waiting for a return value, and silently returning a default would push the bug into runtime where it hides. Three cases, three branches pinned down.

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

All three composition shapes get exercised. A two-stage non-void pipeline is the common case. The multi-stage pipeline crosses a type boundary on purpose (int into string), to confirm that `then`'s type deduction holds up when the return type changes. The void-prefix case we added later: when `then` is chained after a void callback, the next stage receives no "return value" from the previous step and has to relay through external state. That edge slipped past the first version of the tests.

---

## Performance: versus the original Chromium

All tests green. Now for the part we were most curious about. How much do we lose by putting our OnceCallback next to the original Chromium version, with its hand-written reference counting and function-pointer tables? The gap is real, and we are not going to dress it up.

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
// estimate 56-64 bytes
```

The typical numbers on GCC: `std::function` is around 32 bytes, `std::move_only_function` is also around 32 bytes, and our `OnceCallback`, after stacking on the status and token pointer, lands at 56 to 64 bytes. Chromium's? 8 bytes. The price of a single pointer.

Seven times larger gave us a pause at first, but it makes sense once you trace the storage strategy. Chromium stuffs the bound arguments, the function pointer, and the reference count into a heap-allocated `BindState`, and the callback object itself holds a single pointer. We go the SBO route of `std::move_only_function`: small lambdas inline directly into the object, which saves a heap allocation, at the cost of a fatter object.

### Allocation behavior

This is the one place where our design comes out ahead. The SBO threshold of `std::move_only_function` usually sits at two or three pointers (16 to 24 bytes), and a lambda that captures a few arguments fits inside without hitting the heap. Only a lambda with a large capture goes to the heap at construction.

Chromium flips this. It always heap-allocates (`new BindState`), but only once, and every move of the OnceCallback after that copies a single 8-byte pointer, which is almost free. Our side avoids allocation for small objects, but once a move happens, it copies a 32-plus-byte inline buffer. One side saves on allocations, the other saves on moves, and each takes one end of the trade.

### Indirect-call overhead

Once you reach the actual call, the two designs tie. Both do one indirect function call. `std::move_only_function::operator()` and Chromium's `polymorphic_invoke_` dispatch the same way. Under `-O2`, neither side can erase that indirect call: a function pointer that crosses translation units is something the compiler will not inline.

### The trade-off, summed up

| Metric | Ours | Chromium |
|------|-----------|--------------|
| Callback object size | 56-64 bytes | 8 bytes |
| Heap alloc for a small lambda | None (SBO) | Always |
| Move cost | Copy 32+ bytes | Copy 1 pointer |
| Implementation size | ~200 lines | ~2000+ lines |

We read those four rows more than once, and the most valuable one is the last. A full order of magnitude less code. We do not hand-write reference counting, we do not maintain a function-pointer table, we do not haggle with the compiler over `TRIVIAL_ABI` annotations; `std::move_only_function` absorbs all of that. What we get back is an object seven times larger and moves several times more expensive. The zero-allocation property for small lambdas is an accidental bonus in scenarios where callbacks are not posted at high frequency.

Whether that trade pays off depends on what you are doing with it. For teaching, for prototypes, for most business-logic callbacks, we would take it. If you are dropping callbacks into Chromium's hot paths, where a single process hangs tens of thousands of callbacks and `[[clang::trivial_abi]]` is pushing them into registers, go copy the two thousand lines. The positioning of our version was always clear: explain the mechanism, lay the trade-off out on the table, and let you weigh which side matters.

## References

- [Chromium base/functional/ source directory](https://source.chromium.org/chromium/chromium/src/+/main:base/functional/)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [Catch2 documentation](https://github.com/catchorg/Catch2/tree/devel/docs)
