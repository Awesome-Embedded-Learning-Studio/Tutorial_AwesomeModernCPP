---
chapter: 1
cpp_standard:
- 23
description: 'Build the OnceCallback class skeleton from scratch in five steps: template partial specialization, data members, constructor constraints, run() consumption semantics, and the query interface'
difficulty: beginner
order: 2
platform: host
prerequisites:
- 'OnceCallback hands-on (I): motivation and API design'
- 'OnceCallback prerequisite (I): function types and template partial specialization'
- 'OnceCallback prerequisite (IV): Concepts and requires constraints'
- 'OnceCallback prerequisite (V): std::move_only_function (C++23)'
- 'OnceCallback prerequisite (VI): Deducing this (C++23)'
reading_time_minutes: 9
related:
- 'OnceCallback hands-on (III): implementing bind_once'
- 'OnceCallback hands-on (IV): designing the cancellation token'
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
- 模板
title: 'OnceCallback hands-on (II): building the core skeleton'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/01-2-once-callback-core-skeleton.md
  source_hash: 22fe59868b64d68c749c1c74ea447877b248b142d9fe05418100bb53e1981224
  translated_at: '2026-09-26T00:28:08+00:00'
  engine: anthropic
  token_count: 5800
---
# OnceCallback hands-on (II): building the core skeleton

In the previous piece we sorted out why OnceCallback is needed and what the target API should look like. With the motivation covered, our fingers start itching—an interface alone isn't satisfying. You have to build the thing line by line yourself before you know which parts of the design are real gold and which merely look good on paper.

This piece is where we get our hands dirty. We raise the class skeleton from zero in five steps, each step adding exactly one layer on top of the previous one. Once the skeleton stands, everything that follows—`bind_once`, the cancellation token, `then()`—is just components hung onto this frame; no structural surgery ahead. We assume you have already worked through the seven prerequisite pieces—function types, template partial specialization, `requires`, `move_only_function`, deducing this. From here on we just use them, no backtracking to re-explain.

## Step 1: the primary template and partial specialization

The "function type + template partial specialization" pattern from Prerequisite (I) now lands directly on OnceCallback.

```cpp
namespace tamcpp::chrome {

// Primary template: declaration only, no definition
// If someone writes OnceCallback<int> (a non-function type), the compiler reports an error
template<typename FuncSignature>
class OnceCallback;

// Partial specialization: matches when FuncSignature is a function type of the form R(Args...)
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    // All the real code lives in this partial specialization
public:
    using FuncSig = ReturnType(FuncArgs...);
    // ...
};

} // namespace tamcpp::chrome
```

When you write `OnceCallback<int(int, int)>`, the compiler first feeds `int(int, int)` as a whole to the primary template's `FuncSignature`, then looks again and sees the partial specialization can split `int` off as `ReturnType` and `{int, int}` as `FuncArgs`, so the specialization wins. This "take the whole signature first, let a partial specialization decompose it" routine is the universal skeleton for this family of callback libraries—`std::function` and Chromium's `RepeatingCallback` are cut from the same mold. The `FuncSig` type alias conveniently stores a copy of the full signature, so later, when we declare `std::move_only_function<FuncSig>`, we can reach for it directly instead of stitching the signature together one more time.

---

## Step 2: data members—the three core pieces of state

With the type skeleton in place, we fill in the data members. For OnceCallback to keep its own state under control, it takes three things:

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
public:
    using FuncSig = ReturnType(FuncArgs...);

private:
    enum class Status : uint8_t {
        kEmpty,     // never assigned (default-constructed)
        kValid,     // holds a valid callable
        kConsumed   // already consumed by run()
    } status_ = Status::kEmpty;

    std::move_only_function<FuncSig> func_;          // type-erased callable
    std::shared_ptr<CancelableToken> token_;         // optional cancellation token
};
```

Of the three members, `func_` is the heart of type erasure. Lambdas, function pointers, functors—their shapes vary wildly, and `func_` funnels them all into the same `operator()` with the `FuncSig` signature. That is exactly the "one interface catches every callable" we asked for.

The one that deserves a few more words is the three-state enum `status_`. You might ask: why not just null-check `func_`? Because `std::move_only_function`'s `operator bool()` only separates "empty" from "non-empty", while OnceCallback's semantics demand a finer split—"never assigned" and "already consumed by `run()`" are two different things. The latter is an explicit contract violation (a callback may run exactly once), so it has to be told apart from "empty straight off the factory line". Worse, the state of a `move_only_function` after a move is "valid but unspecified" in the standard, so leaning on it for a null check is unreliable in the first place. So we honestly give the state bit its own dedicated enum and don't count on the underlying container to manage semantics for us. Prerequisite (V) covered this trap; this is where it lands.

`token_` is the optional cancellation token: a null pointer by default (the cancellation mechanism is off), attached explicitly through `set_token()`. In this piece we just park it here as a placeholder; the cancellation mechanism gets a dedicated piece later on.

---

## Step 3: constructors and the requires constraint

With the data members seated, we give the class its constructors. There is an old trap with template constructors here: during overload resolution they steal the move constructor's job, and a `requires` constraint has to head them off. Prerequisite (IV) explained the principle; here we watch it land.

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

    // Default constructor: creates an empty callback
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

The most used one in this bunch is the template constructor. When you write `OnceCallback<int(int)>([](int x) { return x; })`, this is the path taken—`Functor` gets deduced as the lambda's closure type, and `requires not_the_same_t` keeps the case where "what was passed happens to be a `OnceCallback` itself" outside the template, letting the move constructor take over. Without that constraint, the template constructor would greedily hijack copy and move, and compile-time overload resolution falls apart. `std::move(function)` moves the callable into `func_`, and `status_` is set to `kValid` at the same time.

The default constructor is far duller: it produces an empty callback—`status_` is `kEmpty` (the default given by the member initializer), and both `func_` and `token_` sit empty. It exists mostly so OnceCallback can go into containers and accept deferred assignment.

The move constructor carries one deliberate trade-off. `func_` and `token_` transfer through `std::move`, `status_` gets copied over directly—nothing unexpected there. The unexpected part is that we actively set the source object back to `kEmpty`, rather than relying on that "unspecified" state `move_only_function` is left in after a move. The reasoning came up earlier: keep the semantics in our own hands. Whether the underlying container ends up empty or valid after a move is a door the standard deliberately leaves open, and we are not gambling on it. Move assignment runs the same logic, plus a self-assignment check.

---

## Step 4: implementing run() with deducing this

In this step we pin the "runs only once" contract onto call sites with compile-time machinery. Through deducing this, `run()` intercepts lvalue calls at compile time; only rvalues (that is, `std::move(cb).run(...)`) get through, forwarded to the internal `impl_run()`.

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

The mechanism is actually quite plain. When the caller writes `cb.run(args)` (no `std::move`), `Self` gets deduced as `OnceCallback&`—an lvalue reference—and the `static_assert` blows up on the spot, with the error message shoving the correct form `std::move(cb).run(...)` right in the caller's face so they don't have to guess. Write `std::move(cb).run(args)` instead, and `Self` deduces to `OnceCallback` (non-reference); compilation passes and the call forwards into `impl_run`.

`impl_run` is where the real work happens:

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

    // Consume: pull func_ out first, then update the state, then execute
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

What we most want to argue through with you in this implementation is the order of consumption.

`impl_run` does not call `func_` directly. It first moves it out into the local variable `functor`, then nulls the member `func_`, sets `status_` to `kConsumed`, and only then executes `functor`. The order of the three steps is not casual—mark the state first, get the callable detached from the member and onto the stack, then run. That way, even if `functor` throws an exception that propagates outward, `status_` has long been `kConsumed`, and the callback object never gets stuck in a halfway state of "`func_` still there but the status says unconsumed". That is how the exception safety is squeezed out: pull the irreversible "consumed" state ahead of execution.

The cancellation check moved to the very front of execution. If a token is attached and already invalid, the callback is consumed without being run. The void-return path takes `return`; the non-void path throws `std::bad_function_call`—the latter looks aggressive at first glance, but stand on the caller's side and it clicks: they wrote `auto x = std::move(cb).run(...)` expecting a value back, and we cannot produce any meaningful return value. Rather than handing back something undefined and letting them get creative with it, better to throw and lay the problem out on the table. This is the "fail loudly" trade-off, the same school of thought as WeakPtr failing `operator*` through a `CHECK`.

The remaining `if constexpr` is a compile-time branch carved out for void return types. void cannot take the regular "call, then return the result" path, so `if constexpr (std::is_void_v<ReturnType>)` picks the road at compile time: void takes "call but assign nothing", non-void takes "call and return". This is the standard pattern from the cheat-sheet piece, so we won't expand on it here.

---

## Step 5: the query interface

The skeleton is missing one last piece—a set of query interfaces, so the caller can probe what state the callback is actually in before running it.

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

We have to spell out the criterion `is_cancelled()` uses to judge: anything that is not `kValid` counts as "cancelled"—empty callbacks and consumed callbacks fall into one bucket at this layer of semantics; to the caller, both mean "don't count on this running". Then a token check layers on top: token attached and invalid also counts as cancelled. `maybe_valid()` at this stage is just `!is_cancelled()`; the name is kept so it can grow when cross-sequence semantics arrive later. `is_null()` watches exactly one thing—whether the callback was ever assigned—which is a separate matter from cancellation. `operator bool()` folds "non-null" and "not cancelled" into one condition and is the most common liveness check at call sites.

The query methods all wear `[[nodiscard]]` uniformly. Callers invoke these methods precisely to make a decision from the return value, so a call that ignores the result is basically a slip of the hand, and the compiler should holler on our behalf. The `explicit` on `operator bool()` is the old discipline: block implicit conversions so a `cb` doesn't quietly slide into a slot that wanted an `int`.

---

## Verifying the core skeleton

With the skeleton built, our habit is to first push on a few of the plainest scenarios—don't chase edge cases right out of the gate; firm up the fundamentals first:

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

These four cases are the skeleton's minimum bar: a non-void callback must return the correct value, a void callback's side effects must fire, a move-only callback capturing a `unique_ptr` must release its resource after running, and after a move the source object must read empty while the target still works. All green, and the skeleton can carry the components mounted on it later; if any one case fails, no rush to move on—go back and check this step first.

## References

- [Chromium callback.h source](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0847R7 - the Deducing this proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
