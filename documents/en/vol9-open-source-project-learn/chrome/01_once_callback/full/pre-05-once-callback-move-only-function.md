---
chapter: 0
cpp_standard:
- 23
description: "A close look at C++23's std::move_only_function, the storage type behind OnceCallback's func_ member. Covers why std::function is not enough, how SBO behaves, and why OnceCallback still needs its own three-state Status enum instead of leaning on move_only_function's null check."
difficulty: intermediate
order: 5
platform: host
prerequisites:
- OnceCallback prerequisites cheat sheet: a recap of C++11/14/17 core features
- OnceCallback prerequisites (I): function types and template partial specialization
reading_time_minutes: 9
related:
- OnceCallback hands-on (II): scaffolding the core skeleton
- OnceCallback hands-on (VI): tests and performance comparison
tags:
- host
- cpp-modern
- intermediate
- 函数对象
- 智能指针
title: 'OnceCallback Prerequisites (V): std::move_only_function (C++23)'
---
# OnceCallback Prerequisites (V): std::move_only_function (C++23)

The `func_` member of `OnceCallback` is typed `std::move_only_function<FuncSig>`. Its job is the dirty work of type erasure: it takes the motley crew of lambdas, function pointers, and functors and herds them into a single call entry with a fixed signature. In this post we pull it apart and look at what it actually differs from the old `std::function`, how its SBO (Small Buffer Optimization) holds up, and one pitfall we walked into ourselves: why `OnceCallback` has to keep its own `Status` enum and cannot just piggyback on the null check.

## From std::function to std::move_only_function

### Where std::function gets stuck

`std::function` is the general-purpose callable container C++11 handed us. It uses type erasure to fold a pile of callable objects into one interface. But it carries one brutal constraint: whatever it stores must be copyable.

The root cause is that it copies itself. When you copy a `std::function`, it has to copy the object held inside it too. So if you try to stuff in a lambda that captured a `std::unique_ptr`, a move-only type that flat-out refuses to copy, the compiler slaps the error in your face:

```cpp
#include <functional>
#include <memory>

auto ptr = std::make_unique<int>(42);

// Compile error: unique_ptr is not copyable, std::function requires copyable
std::function<int()> f = [p = std::move(ptr)]() { return *p; };
```

For `OnceCallback` this is a wall. Move-only is the whole pitch, and that means it has to accept callbacks capturing a `unique_ptr`.

### How std::move_only_function breaks the deadlock

C++23's `std::move_only_function` (still in `<functional>`) exists to crack exactly this nut. It lops off the copy operations and keeps only the move, so the thing stored no longer has to be copyable.

```cpp
#include <functional>
#include <memory>

auto ptr = std::make_unique<int>(42);

// OK: move_only_function does not require copyable
std::move_only_function<int()> f = [p = std::move(ptr)]() { return *p; };

int result = f();  // result == 42
```

The interface difference is one sentence: `std::function` copies and moves, and what it stores must be copyable; `std::move_only_function` only moves, and what it stores only needs to be movable.

---

## Construction, move, call, null check

Construction works the same way as `std::function`: `std::move_only_function<R(Args...)>` opens its arms to anything matching the signature, a lambda, a function pointer, a functor, even another `std::move_only_function`. A default-constructed one is empty and compares equal to `nullptr`. Calling uses the familiar `f(args...)` syntax; calling an empty object throws `std::bad_function_call`, and when it should crash, let it crash.

```cpp
// From a lambda
std::move_only_function<int(int, int)> f1 = [](int a, int b) { return a + b; };

// From a function pointer
int add(int a, int b) { return a + b; }
std::move_only_function<int(int, int)> f2 = &add;

// From a functor
struct Multiplier {
    int operator()(int a, int b) { return a * b; }
};
std::move_only_function<int(int, int)> f3 = Multiplier{};

// Default construction: create an empty move_only_function
std::move_only_function<int()> f4;  // f4 == nullptr
```

The part worth pausing on is the move. The semantics are straightforward: the callable inside the source relocates wholesale to the target. But what state is the source left in? The standard gives four words: valid but unspecified. It does not promise the source ends up empty.

```cpp
std::move_only_function<int()> f = []() { return 42; };
auto g = std::move(f);
// f's state is unspecified — may be empty, may not be
// Do not rely on f's post-move behavior
```

We ran it on GCC 16 and `bool(f)` after the move did return `false`. But hold onto this: that is the implementation being kind, not a promise the standard backstops for you. Switch to another implementation and a `true` tomorrow is not off the table. This tail matters. It is half the reason, in a moment, that `OnceCallback` cannot lean on the null check and has to keep its own `Status`.

For the null check, use `operator bool()` or compare against `nullptr`; the two are equivalent. To clear it on purpose, assign `nullptr`, and the callable it was holding destructs:

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
f = nullptr;  // clear f, destructing the previously held callable
```

---

## SBO: Small Buffer Optimization

Internally, `std::move_only_function` does SBO (Small Buffer Optimization), the same trick `std::function` uses. The recipe is not complicated: the object keeps a fixed-size buffer, usually a few pointers wide. If the callable is small enough, it goes straight into the buffer and the heap allocation is skipped. If it is too big, the heap is the fallback.

![SBO internal structure](./pre-05-sbo-structure.drawio)

The SBO threshold is the implementation's call; it usually lands somewhere between 2 and 3 pointers wide (16 to 24 bytes). A lambda that captures little, like `[x = 42]` or `[&ref]`, almost always slides into SBO without triggering a heap allocation. But a lambda that captures a chunk, say a `std::string` plus a few `int`s, blows past the threshold and has to go to the heap at construction.

### sizeof in practice

Talking about it is cheap; let us measure the real thing. GCC 16 prints this:

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

`std::function<void()>` is 32 bytes; `std::move_only_function<void()>` is 8 bytes more, at 40. The SBO strategy underneath is similar, but the move-only side has its own overhead (dropping the work the copy path would have done, leaving a move vtable, and so on), and that is roughly where the extra bytes go.

---

## Why OnceCallback still needs its own Status enum

Reading this far you might be wondering: since `std::move_only_function` can already check for null, why does `OnceCallback` go to the trouble of wrapping a `Status` enum around it? We tried taking the shortcut too, using its null check directly. It is not enough once you actually sit down to write it.

The root problem is that it does not have enough states. `operator bool()` only separates "empty" from "non-empty", but `OnceCallback` has to tell three states apart:

```cpp
enum class Status : uint8_t {
    kEmpty,     // never assigned (default constructed)
    kValid,     // holds a valid callable
    kConsumed   // run() has already been called
};
```

"Never assigned" (`kEmpty`) and "assigned, run, and already digested" (`kConsumed`) look identical to `operator bool()`, both empty, but the meaning is poles apart. When debugging, `kEmpty` is usually a reminder that you forgot to assign the callback, a genuine bug; `kConsumed` is the expected state after the callback has run normally, nothing wrong at all. Smear the two together and `DCHECK` cannot say anything sensible about it.

Then there is the sneakier one: the "unspecified post-move state" from the last section. The standard does not guarantee that `operator bool()` returns `false` after a move. Some implementation is free to return `true` even though the goods inside have already been hauled out. If `OnceCallback` really relied on it for state, the moment a move happened it could misjudge. Owning its own `Status` is steadier. It is fully in our hands; on move construction we explicitly mark the source object `kEmpty`, clean and unambiguous.

---

## Next to Chromium's BindState

Chromium does not touch the standard library's type erasure. It hand-rolls its own `BindState`. Put the two side by side and the differences are worth a look.

Chromium's `BindState<Functor, BoundArgs...>` is a heap object that takes in the callable and every bound argument. `OnceCallback` itself just holds a smart pointer (`scoped_refptr`) to the `BindState`, 8 bytes total, one pointer wide. All the state lives over on the `BindState` side; the callback is just a thin proxy.

Our version replaces the entire `BindState` layer with `std::move_only_function`. Type erasure and SBO are handled for it internally, and the hand-written work of function pointer tables, SBO buffers, and move/destructor bookkeeping all gets dropped. The price is size: it grows from 8 bytes to 40 bytes (`std::move_only_function` on its own), then piles on the `Status` enum and an optional `CancelableToken` pointer, putting one `OnceCallback` at roughly 56 to 64 bytes.

| Metric | Chromium BindState | Our std::move_only_function |
|------|-------------------|-------------------------------|
| Callback object size | 8 bytes (one pointer) | 56-64 bytes |
| Heap allocation | Always (new BindState) | Only when lambda exceeds SBO threshold |
| Move cost | Copy one pointer | Copy 32+ bytes |
| Implementation complexity | High (manual refcount + function pointer table) | Low (reuse standard library) |

For teaching, and for most real scenarios, a fifty- to sixty-byte callback object is not a bottleneck at all. If you genuinely need to squeeze size to the limit, take the Chromium road; we get into the core idea in a later hands-on piece.

The next post is the last prerequisite for OnceCallback: C++23's deducing this (explicit object parameter). The reason `run()` can pull off compile-time lvalue/rvalue interception has its roots there.

## References

- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0288R9 - move_only_function proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0288r9.html)
- [cppreference: std::function](https://en.cppreference.com/w/cpp/utility/functional/function)
