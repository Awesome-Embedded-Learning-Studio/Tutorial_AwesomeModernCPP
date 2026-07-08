---
chapter: 1
cpp_standard:
- 23
description: "Building the OnceCallback class skeleton from scratch in five steps: template partial specialization, data members, constructor constraints, run() consumption semantics, and the query interface"
difficulty: beginner
order: 2
platform: host
prerequisites:
- 'OnceCallback hands-on (I): motivation and API design'
- 'OnceCallback prerequisite (I): function types and template partial specialization'
- 'OnceCallback prerequisite (IV): concepts and requires constraints'
- 'OnceCallback prerequisite (V): std::move_only_function'
- 'OnceCallback prerequisite (VI): deducing this'
reading_time_minutes: 9
related:
- 'OnceCallback hands-on (III): implementing bind_once'
- 'OnceCallback hands-on (IV): the cancellation token'
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
- 模板
title: "OnceCallback in Practice (II): Building the Core Skeleton"
---
# OnceCallback in Practice (II): Building the Core Skeleton

In the previous piece we sorted out why OnceCallback exists and what its target API looks like. With the motivation settled, the itch sets in. Staring at an interface only gets you so far. To tell a design that holds up from one that just looks good on paper, you have to write it out line by line.

So let's build it. We start the class skeleton from zero and grow it in five steps, each one layering onto the last. Once the skeleton stands, everything that follows (`bind_once`, the cancellation token, `then()`) is just hanging components off this frame; no structural surgery ahead. We assume you've been through the seven prerequisite pieces. Function types, partial specialization, `requires`, `move_only_function`, deducing this, they all come into play below without re-explanation.

## Step 1: primary template and partial specialization

The "function type + template partial specialization" pattern from Prerequisite (I) now lands directly on OnceCallback.

```cpp
namespace tamcpp::chrome {

// Primary template: declaration only, no definition.
// If someone writes OnceCallback<int> (a non-function type), the compiler errors.
template<typename FuncSignature>
class OnceCallback;

// Partial specialization: matches when FuncSignature is a function type R(Args...)
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    // All the real code lives inside this specialization
public:
    using FuncSig = ReturnType(FuncArgs...);
    // ...
};

} // namespace tamcpp::chrome
```

When you write `OnceCallback<int(int, int)>`, the compiler first feeds `int(int, int)` as a whole into the primary template's `FuncSignature`, then notices the partial specialization can split it back into `ReturnType = int` and `FuncArgs = {int, int}`, so the specialization wins. This "take it whole, let specialization decompose it" move is the universal skeleton for this kind of callback library. `std::function`, Chromium's `RepeatingCallback`, they all follow the same mold. The `FuncSig` alias keeps a copy of the full signature handy, so later when we declare `std::move_only_function<FuncSig>` we just reuse it instead of stitching the signature back together.

---

## Step 2: data members, three core storages

With the type skeleton in place, we fill in the data members. OnceCallback manages its own state, and it takes three things to do it:

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
public:
    using FuncSig = ReturnType(FuncArgs...);

private:
    enum class Status : uint8_t {
        kEmpty,     // never assigned (default-constructed)
        kValid,     // holds a valid callable
        kConsumed   // run() has been called
    } status_ = Status::kEmpty;

    std::move_only_function<FuncSig> func_;          // type-erased callable
    std::shared_ptr<CancelableToken> token_;         // optional cancellation token
};
```

Of the three, `func_` is the heart of type erasure. Lambdas, function pointers, functors, the shapes are all over the place, and `func_` funnels them into one `operator()` with the `FuncSig` signature. That's the "one interface catches every callable" we wanted.

The member worth dwelling on is the three-state `status_`. You might ask, why not just null-check `func_`? Because `std::move_only_function::operator bool()` only separates "empty" from "non-empty", and OnceCallback's contract is finer than that. "Never assigned" and "already consumed by `run()`" are different things. The second is an explicit contract violation (a callback runs exactly once), and it has to read differently from "empty off the factory line." Worse, the post-move state of `move_only_function` is "valid but unspecified" by the standard, so leaning on it for null checks is shaky ground. So we give the state its own enum and don't expect the underlying container to carry our semantics. Prerequisite (V) covered this trap; this is where it lands.

`token_` is the optional cancellation token. It starts as a null pointer (cancellation disabled) and only attaches through an explicit `set_token()`. We park it here as a placeholder; the cancellation mechanism gets its own piece later.

---

## Step 3: constructors and the requires constraint

Data members in place, now we give the class its constructors. There's an old trap with template constructors: they elbow into overload resolution and steal the move constructor's job. A `requires` clause has to head them off. Prerequisite (IV) explained why; here we watch it land.

```cpp
// not_the_same_t concept: F, after decay, is not T
template<typename F, typename T>
concept not_the_same_t = !std::is_same_v<std::decay_t<F>, T>;

template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    // ... data members ...

    // Copying is forbidden
    OnceCallback(const OnceCallback&) = delete;
    OnceCallback& operator=(const OnceCallback&) = delete;

public:
    // Template constructor: accepts any callable
    template<typename Functor>
        requires not_the_same_t<Functor, OnceCallback>
    explicit OnceCallback(Functor&& function)
        : status_(Status::kValid), func_(std::move(function)) {}

    // Default constructor: produces an empty callback
    explicit OnceCallback() = default;

    // Move constructor
    OnceCallback(OnceCallback&& other) noexcept
        : status_(other.status_),
          func_(std::move(other.func_)),
          token_(std::move(other.token_)) {
        other.status_ = Status::kEmpty;
    }

    // Move assignment
    OnceCallback& operator=(OnceCallback&& other) noexcept {
        if (this != &other) {
            status_ = other.status_;
            func_ = std::move(other.func_);
            token_ = std::move(other.token_);
            other.status_ = Status::kEmpty;
        }
        return *this;
    }
};
```

The most-used one here is the template constructor. When you write `OnceCallback<int(int)>([](int x) { return x; })`, this is what runs. `Functor` gets deduced as the lambda's closure type, and `requires not_the_same_t` blocks the case where the incoming thing is itself a `OnceCallback`, steering it to the move constructor instead. Without that clause the template constructor would greedily hijack copy and move, and overload resolution falls apart at compile time. `std::move(function)` moves the callable into `func_`, and `status_` flips to `kValid` at the same time.

The default constructor is dull by comparison. It produces an empty callback: `status_` is `kEmpty` (the member initializer's default), `func_` and `token_` both null. It exists mostly so OnceCallback can sit in containers and accept deferred assignment.

The move constructor has one deliberate choice worth pointing out. `func_` and `token_` transfer through `std::move`, `status_` gets copied over, nothing surprising there. What's surprising is that we actively reset the source to `kEmpty` instead of trusting `move_only_function`'s post-move state. Same reasoning as before: keep the semantics in our own hands. Whether the underlying container comes out empty or valid after a move is a question the standard leaves open, and we won't bet on it. Move assignment follows the same logic, plus a self-assignment check.

---

## Step 4: implementing run() with deducing this

This step nails the "runs exactly once" contract to the call site at compile time. Using deducing this, `run()` rejects lvalue calls during compilation and only releases rvalues (that is, `std::move(cb).run(...)`) through to the internal `impl_run()`.

```cpp
// Declaration (inside the class body)
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType;

// Implementation (outside the class body, in once_callback_impl.hpp)
template<typename ReturnType, typename... FuncArgs>
template<typename Self>
auto OnceCallback<ReturnType(FuncArgs...)>::run(this Self&& self, FuncArgs&&... args)
    -> ReturnType {
    static_assert(!std::is_lvalue_reference_v<Self>,
        "once_callback::run() must be called on an rvalue. "
        "Use std::move(cb).run(...) instead.");
    return std::forward<Self>(self).impl_run(std::forward<FuncArgs>(args)...);
}
```

The mechanism is straightforward. When the caller writes `cb.run(args)` (no `std::move`), `Self` deduces to `OnceCallback&`, an lvalue reference. The `static_assert` fires on the spot, and the error message hands them the correct form `std::move(cb).run(...)` so they don't have to guess. Write `std::move(cb).run(args)` and `Self` deduces to `OnceCallback` (non-reference), compilation passes, and the call forwards into `impl_run`.

`impl_run` is where the actual work happens:

```cpp
template<typename ReturnType, typename... FuncArgs>
ReturnType OnceCallback<ReturnType(FuncArgs...)>::impl_run(FuncArgs... args) {
    assert(status_ == Status::kValid);

    // Cancellation check: consume but do not execute
    if (token_ && !token_->is_valid()) {
        status_ = Status::kConsumed;
        func_ = nullptr;
        if constexpr (std::is_void_v<ReturnType>) {
            return;
        } else {
            throw std::bad_function_call{};
        }
    }

    // Consume: pull func_ out first, then update state, then execute
    auto functor = std::move(func_);
    func_ = nullptr;
    status_ = Status::kConsumed;

    if constexpr (std::is_void_v<ReturnType>) {
        functor(std::forward<FuncArgs>(args)...);
    } else {
        return functor(std::forward<FuncArgs>(args)...);
    }
}
```

The thing most worth unpacking in this implementation is the consumption order.

`impl_run` doesn't call `func_` directly. It first moves it out into a local `functor`, then nulls the member `func_`, sets `status_` to `kConsumed`, and only then executes `functor`. The ordering is not arbitrary. State gets marked first, the callable detaches from the member and lands on the stack, then it runs. So even if `functor` throws an exception on the way out, `status_` is already `kConsumed`, and the callback object never sits in a halfway state where `func_` still exists but the status says unconsumed. That's how the exception safety is carved out: pull the irreversible "consumed" state ahead of execution.

The cancellation check moved to the very front of execution. If a token is attached and it's invalid, the callback is consumed without running. The void path returns, the non-void path throws `std::bad_function_call`. The throw looks aggressive at first glance, but stand on the caller's side and it clicks: they wrote `auto x = std::move(cb).run(...)` expecting a value back, and you can't produce any meaningful return value. Handing back something undefined and letting them use it in surprising ways is worse than throwing and putting the problem on the table. It's a "fail loud" tradeoff, the same family of thinking as WeakPtr using `CHECK` to fail `operator*`.

The remaining `if constexpr` is a compile-time branch carved out for void return types. void can't take the usual "call and return the result" path, so `if constexpr (std::is_void_v<ReturnType>)` picks the road at compile time: void takes "call but don't assign", non-void takes "call and return". This is the standard pattern from the cheat-sheet piece, so we won't expand on it here.

---

## Step 5: the query interface

The skeleton is missing one last piece: a set of query methods so the caller can probe what state the callback is actually in before running it.

```cpp
[[nodiscard]] bool is_cancelled() const noexcept {
    if (status_ != Status::kValid) return true;
    if (token_ && !token_->is_valid()) return true;
    return false;
}

[[nodiscard]] bool maybe_valid() const noexcept {
    return !is_cancelled();
}

[[nodiscard]] bool is_null() const noexcept {
    return status_ == Status::kEmpty;
}

explicit operator bool() const noexcept {
    return !is_null() && !is_cancelled();
}

void set_token(std::shared_ptr<CancelableToken> token) {
    token_ = std::move(token);
}
```

A word on how `is_cancelled()` reads the state. Anything that isn't `kValid` counts as "cancelled". Empty callbacks and consumed callbacks collapse into one bucket at this layer; from the caller's view both mean "don't count on this running." Then a token check layers on top: token attached and expired also counts as cancelled. `maybe_valid()` is currently just `!is_cancelled()`, kept under that name to leave room for cross-sequence semantics later. `is_null()` looks at one thing only, whether the callback was ever assigned, which is a separate question from cancellation. `operator bool()` folds "non-null" and "not cancelled" together, and it's the most common liveness check at call sites.

Every query method carries `[[nodiscard]]`. Callers invoke these for their return value to make a decision on, so dropping the result is almost always a slip, and the compiler should holler. The `explicit` on `operator bool()` is the old discipline: block implicit conversions so a `cb` doesn't slip into a slot that wanted an `int`.

---

## Verifying the core skeleton

With the skeleton standing, the habit is to push on a few of the plainest cases first. Don't chase edges right away; lock the base down first.

```cpp
#include "once_callback/once_callback.hpp"
#include <cassert>
#include <memory>

int main() {
    using namespace tamcpp::chrome;

    // 1. Non-void return
    OnceCallback<int(int, int)> add([](int a, int b) { return a + b; });
    assert(std::move(add).run(3, 4) == 7);

    // 2. Void return
    bool called = false;
    OnceCallback<void()> side_effect([&called] { called = true; });
    std::move(side_effect).run();
    assert(called);

    // 3. Move-only capture
    auto ptr = std::make_unique<int>(42);
    OnceCallback<int()> capture_move([p = std::move(ptr)] { return *p; });
    assert(std::move(capture_move).run() == 42);

    // 4. Move semantics
    OnceCallback<int()> movable([] { return 1; });
    OnceCallback<int()> moved_to = std::move(movable);
    assert(movable.is_null());            // source goes empty
    assert(std::move(moved_to).run() == 1);  // target is valid

    return 0;
}
```

These four cases are the skeleton's floor: a non-void callback returns the right value, a void callback's side effect fires, a move-only callback capturing a `unique_ptr` releases its resource after running, and after a move the source reads empty while the target works. All green, and the skeleton can hold up the components coming next. If any one fails, don't rush forward; turn back and check this step.

## References

- [Chromium callback.h source](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0847R7, the Deducing this proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
