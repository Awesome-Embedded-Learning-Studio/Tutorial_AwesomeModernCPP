---
chapter: 0
cpp_standard:
- 23
description: "A deep dive into C++23's std::move_only_function — the core storage type of OnceCallback — from the motivations behind its evolution from std::function to its SBO behavior, and on to why OnceCallback needs its own independent three-state management"
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'OnceCallback prerequisite cheat sheet: a review of C++11/14/17 core features'
- 'OnceCallback prerequisite (I): function types and template partial specialization'
reading_time_minutes: 9
related:
- 'OnceCallback hands-on (II): building the core skeleton'
- 'OnceCallback hands-on (VI): tests and performance comparison'
tags:
- host
- cpp-modern
- intermediate
- 函数对象
- 智能指针
title: 'OnceCallback prerequisite (V): std::move_only_function (C++23)'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/pre-05-once-callback-move-only-function.md
  source_hash: 85d36a05082254c0f75818c5d1b8fe581226134fc9e1b842649f32a0fe3f4fce
  translated_at: '2026-09-26T00:51:26+00:00'
  engine: anthropic
  token_count: 4200
---
# OnceCallback prerequisite (V): std::move_only_function (C++23)

The `func_` member of OnceCallback has the type `std::move_only_function<FuncSig>`. This thing does the dirty work — type erasure: it takes the whole zoo of callable objects out there — lambdas, function pointers, functors — and folds every one of them into a single calling entry with a fixed signature. In this piece we're going to crack it open and get a clear look: where exactly it differs from the old `std::function`, whether its SBO (small buffer optimization) pulls its weight, and one pit the author personally fell into — why OnceCallback still has to keep an extra `Status` enum of its own instead of taking the lazy route and leaning on its emptiness check.

## From std::function to std::move_only_function

### Where std::function gets stuck

`std::function` is the general-purpose callable container C++11 gave us; through type erasure it boils a whole pot of callable objects down to one interface. But it carries a fatal hard constraint: whatever gets stored inside must be copyable.

The root cause is that it is itself copyable. Copy a `std::function`, and it has to copy the object held inside along with it. But what if the thing you want to stuff in is a lambda that captured a `std::unique_ptr`? A unique_ptr owns its target exclusively and flat-out refuses to be copied. The result is that this line of code slaps a compile error right in your face:

```cpp
#include <functional>
#include <memory>

auto ptr = std::make_unique<int>(42);

// Compile error! unique_ptr is not copyable, and std::function requires copyability
std::function<int()> f = [p = std::move(ptr)]() { return *p; };
```

This runs OnceCallback straight into a wall — move-only is OnceCallback's whole selling point, so it absolutely has to support callbacks that captured a `unique_ptr`.

### How std::move_only_function solves it

C++23's `std::move_only_function` (still living in `<functional>`) came at exactly this pain point: it chopped off the copy operations and kept only moves, so the stored object no longer has to be copyable either.

```cpp
#include <functional>
#include <memory>

auto ptr = std::make_unique<int>(42);

// OK! move_only_function does not require copyability
std::move_only_function<int()> f = [p = std::move(ptr)]() { return *p; };

int result = f();  // result == 42
```

The interface difference between the two fits in one sentence: `std::function` copies and moves, so the stored object must be copyable; `std::move_only_function` only moves and never copies, so the stored object just needs to be movable.

---

## Construction, moving, calling, and emptiness checks

Construction works from the same mold as `std::function`: a `std::move_only_function<R(Args...)>` opens its arms to any callable whose signature matches — lambdas, function pointers, functors, even another `std::move_only_function`. A default-constructed one comes out empty and compares equal to `nullptr` when you test it. Calling is the familiar `f(args...)` syntax; call an empty one and it throws `std::bad_function_call` on the spot — if it deserves to crash, let it crash.

```cpp
// Construct from a lambda
std::move_only_function<int(int, int)> f1 = [](int a, int b) { return a + b; };

// Construct from a function pointer
int add(int a, int b) { return a + b; }
std::move_only_function<int(int, int)> f2 = &add;

// Construct from a functor
struct Multiplier {
    int operator()(int a, int b) { return a * b; }
};
std::move_only_function<int(int, int)> f3 = Multiplier{};

// Default construction: creates an empty move_only_function
std::move_only_function<int()> f4;  // f4 == nullptr
```

What genuinely deserves a pause is moving. The semantics are plain: the source's callable relocates wholesale into the target. But after the move, what state is the source left in? The standard's answer: valid but unspecified. It does not guarantee that the source becomes empty.

```cpp
std::move_only_function<int()> f = []() { return 42; };
auto g = std::move(f);
// f's state is unspecified — it may or may not be empty
// Do not rely on f's behavior after the move
```

The author casually ran this on GCC 16, and sure enough, `bool(f)` after the move came back `false`. But please remember: that is the implementation being nice to you, not a promise the standard underwrites — switch to another implementation, and it handing you a `true` tomorrow is entirely possible. This loose end matters a lot: when we get to why OnceCallback still has to keep its own `Status` enum, half of the root cause sits right here.

To check for emptiness, go through `operator bool()` or compare directly against `nullptr` — the two are equivalent. To empty one out on purpose, assign `nullptr` into it, and the callable it was clutching gets destroyed along the way:

```cpp
std::move_only_function<int()> f;
if (!f) {
    std::cout << "f is empty\n";
}
// equivalent to
if (f == nullptr) {
    std::cout << "f is empty\n";
}

f = []() { return 42; };
if (f) {
    std::cout << "f is not empty\n";
}
```

```cpp
f = nullptr;  // empties f, destroying the callable it previously held
```

---

## SBO: the small buffer optimization

On the inside, `std::move_only_function` does the small buffer optimization (SBO) just like `std::function` does. The scheme isn't complicated: the object reserves a fixed-size buffer for itself — usually a few pointers wide. If the callable is small enough, it gets tucked straight into that buffer and the heap allocation is saved; if it's too big to fit, we settle for second best and go to the heap.

![Internal structure of the SBO small buffer optimization](./pre-05-sbo-structure.drawio)

The SBO threshold is set by each implementation itself, commonly landing in the range of 2 to 3 pointers wide (16 to 24 bytes). Lambdas that capture little — things like `[x = 42]` or `[&ref]` — almost always squeeze into SBO without triggering a heap allocation. But if a lambda captures a whole pile, say a `std::string` plus a few `int`s, and blows past the threshold, construction has to honestly go to the heap.

### A measured sizeof comparison

Talk is cheap — let's measure the real thing. On GCC 16 the run comes out like this:

```cpp
#include <functional>
#include <iostream>

int main() {
    std::cout << "sizeof(std::function<void()>):           "
              << sizeof(std::function<void()>) << "\n";
    std::cout << "sizeof(std::move_only_function<void()>): "
              << sizeof(std::move_only_function<void()>) << "\n";
}
```

```text
sizeof(std::function<void()>):           32
sizeof(std::move_only_function<void()>): 40
```

`std::function<void()>` is 32 bytes; `std::move_only_function<void()>` runs 8 bytes larger, coming in at 40. The two share roughly the same underlying SBO approach, but the move-only treatment — skipping the work a copy path would have to do, carrying things like a move vtable instead — is mainly where those extra bytes go.

---

## Why OnceCallback still needs its own Status enum

Reading this far, you might ask: since `std::move_only_function` can check its own emptiness, why does OnceCallback go to the extra trouble of wrapping another `Status` enum around the outside? The author initially wanted to take the shortcut and use its emptiness check too; only when actually building it did it turn out to not be enough.

The root of it is that there aren't enough states. `operator bool()` can only tell "empty" from "non-empty", but what OnceCallback needs to distinguish is three states:

```cpp
enum class Status : uint8_t {
    kEmpty,     // never assigned (default-constructed)
    kValid,     // holds a valid callable
    kConsumed   // already invoked by run()
};
```

"Never assigned anything" (`kEmpty`) and "assigned, run, and already digested" (`kConsumed`) — in the eyes of `operator bool()` both count as empty, yet their meanings are worlds apart. During debugging, `kEmpty` is usually nudging you that you forgot to assign the callback — a real bug; `kConsumed` is the expected state after a callback has been invoked normally — perfectly normal. Smear those two cases together, and a DCHECK can't get a clear sentence out.

And there's a sneakier one: the "valid but unspecified after move" business from the previous section. The standard doesn't guarantee that `operator bool()` gives `false` after a move — some implementation is perfectly free to return `true` while the insides have already been carted off. If OnceCallback really leaned on it for state, the moment a move happens you could get a misjudgment. Our own `Status` sits on solid ground — it's entirely in our hands: on move construction we explicitly mark the source object `kEmpty`, clean and crisp, with no ambiguity whatsoever.

---

## Looking at it next to Chromium's BindState

Chromium didn't touch the standard library's type erasure; it hand-rolled its own `BindState`. Put the two designs side by side and the differences are rather interesting.

Chromium's `BindState<Functor, BoundArgs...>` is a heap object that takes in the callable and all of the bound arguments. The `OnceCallback` itself just holds a smart pointer (`scoped_refptr`) to the `BindState` — 8 bytes total, one pointer wide. All the state gets pushed over to the `BindState` side, and the callback itself is a thin proxy.

Our version swaps that entire `BindState` layer for `std::move_only_function` — type erasure and SBO are handled for you on the inside, and that whole pile of hand-written work — function pointer tables, SBO buffers, move-and-destroy plumbing — is saved. The price is size: from 8 bytes it grows to 40 (the `std::move_only_function` itself), then stacks on the `Status` enum and an optional `CancelableToken` pointer, putting a `OnceCallback` somewhere around 56 to 64 bytes.

| Metric | Chromium BindState | Our std::move_only_function |
|------|-------------------|-------------------------------|
| Callback object size | 8 bytes (one pointer) | 56-64 bytes |
| Heap allocation | Always (new BindState) | Only when the lambda exceeds the SBO threshold |
| Cost of a move | Copying one pointer | Copying 32+ bytes |
| Implementation complexity | Very high (hand-written refcounting + function pointer table) | Low (reuses the standard library) |

For teaching, and for most real-world scenarios, a fifty-to-sixty-byte callback object is nowhere near being a bottleneck. If you truly need to squeeze size to the extreme, take the Chromium road — we'll go through its core ideas in detail in the later hands-on pieces.

The next piece is the final prerequisite stop for OnceCallback: C++23's deducing this (explicit object parameter) — it's what lets the `run()` method tell lvalues from rvalues at compile time and intercept accordingly.

## References

- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0288R9 - the move_only_function proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0288r9.html)
- [cppreference: std::function](https://en.cppreference.com/w/cpp/utility/functional/function)
