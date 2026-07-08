---
chapter: 1
cpp_standard:
- 23
description: "From the core skeleton to a complete component: a four-step walkthrough of the once_callback implementation, focused on the template techniques and the ownership design."
difficulty: advanced
order: 2
platform: host
prerequisites:
- once_callback design guide (I): motivation and API design
reading_time_minutes: 24
related:
- bind_once / bind_repeating and argument binding
- Callback cancellation and composition patterns
tags:
- host
- cpp-modern
- advanced
- 回调机制
- 函数对象
title: "once_callback Design Guide (II): Step-by-Step Implementation"
---
# once_callback Design Guide (II): Step-by-Step Implementation

In the previous post we nailed down the target API and the internal architecture of `OnceCallback`. Now it is time to write code. One disclaimer first: this post will not paste the full header in front of you. That file runs to several hundred lines, and staring at it whole is a good way to lose focus. Instead we walk through the skeleton and the template tricks that actually demand thought, so the "why this shape" question gets answered properly. The complete, compilable code is left for the exercises and for the testing post that comes third.

The implementation stacks up in four steps. First we get the `run()` semantics right, since everything hangs off them. Then we add `bind_once()` for argument binding, hang a cancellation check off the front, and finally wire up `then()` for chaining. At each step we keep two questions in view: what does this piece look like, and where is the hard template trick hiding.

---

## Step 1: The core skeleton, starting from template partial specialization

### Why `OnceCallback<R(Args...)>` looks like that

If you have glanced at the standard library, you will have noticed that `std::function` and `std::move_only_function` share a shape: the template parameter is not "return type plus a parameter list written separately", it is one whole function signature. Our `OnceCallback` follows the same convention, for the same reason. Signature-style template parameters read cleanly.

The mechanism underneath is template partial specialization. We start with a bare primary template, declared but not defined:

```cpp
template<typename FuncSignature>
class OnceCallback;  // primary template: no implementation
```

Then we open one partial specialization that catches the case where the signature is exactly a function type:

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    // all the real code lives in this specialization
};
```

When you write `OnceCallback<int(int, int)>`, the compiler first feeds `int(int, int)` as one whole type into the primary template's `FuncSignature`, then notices the partial specialization can split that whole into `ReturnType = int` and `FuncArgs... = {int, int}`, so the specialization wins. The payoff is obvious: users specify the callback type with a natural function-signature syntax, instead of passing return type and parameter list as two separate template arguments.

There is a small trap here that trips people. The `R(Args...)` spelling looks like a function declaration, but in a template parameter position it is actually a function type. `int(int, int)` is a legal C++ type in its own right, describing "a function that eats two ints and returns one int". The partial specialization hitchhikes on that, unpacking the signature through pattern matching.

### Internal storage: what the class skeleton looks like

The previous post fixed a three-state architecture. Now we stand the skeleton up. Ignore the method bodies for a moment and just look at the data members and the interface signatures:

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    // Core storage: holds the actual callable object.
    // Feed it a lambda, a function pointer, or a functor — it takes them all.
    std::move_only_function<FuncSig> func_;

    // Three-state flag: kEmpty → kValid → kConsumed
    Status status_ = Status::kEmpty;

    // Cancellation token (optional)
    std::shared_ptr<CancelableToken> token_;

public:
    // Construction: accepts any callable (with a requires constraint, explained below)
    template<typename Functor>
        requires not_the_same_t<Functor, OnceCallback>
    explicit OnceCallback(Functor&& f);

    // Move-only: copy deleted
    OnceCallback(const OnceCallback&) = delete;
    OnceCallback& operator=(const OnceCallback&) = delete;
    OnceCallback(OnceCallback&& other) noexcept;
    OnceCallback& operator=(OnceCallback&& other) noexcept;

    // Core: runs the callback and consumes *this (via deducing this, explained below)
    template<typename Self>
    auto run(this Self&& self, FuncArgs&&... args) -> ReturnType;

    // Query interface
    [[nodiscard]] bool is_cancelled() const noexcept;
    [[nodiscard]] bool maybe_valid() const noexcept;
    [[nodiscard]] bool is_null() const noexcept;
    explicit operator bool() const noexcept;

    // Install a cancellation token
    void set_token(std::shared_ptr<CancelableToken> token);

    // Chained composition
    template<typename Next> auto then(Next&& next) &&;

private:
    ReturnType impl_run(FuncArgs... args);  // the real execution logic
};
```

Every member has a clear job. `func_` does the dirty work of type erasure: whatever you feed it, lambda, function pointer, or functor, it gets folded into one call port with a known signature. `status_` is a three-state enum separating "never assigned" (kEmpty), "ready to run" (kValid), and "already ran" (kConsumed). `token_` is an optional cancellation token that gates the callback before it actually runs. Move operations do a pointer-level transfer and leave the source back in kEmpty.

Once the skeleton is up, two spots carry the most template density: `run()` with its deducing this, and the constructor's `requires` constraint. Pulling those two apart and explaining them fully makes the rest easy reading.

### deducing this: let the compiler block the wrong call for us

`run()` is the soul of the component, and it is also the method most densely packed with C++23 features. Start with its declaration:

```cpp
template<typename Self>
auto run(this Self&& self, Args... args) -> R;
```

That `this Self&& self` made me blink the first time I saw it. It is C++23's deducing this, officially called an explicit object parameter. In a traditional member function `this` is implicit: the compiler quietly threads the current object's address in, and you cannot see or touch it. Deducing this turns `this` into an explicit first parameter and uses template argument deduction to pin down its type and value category.

```cpp
// Traditional form: this is implicit
void run(FuncArgs... args);          // compiler sees run(OnceCallback* this, FuncArgs... args)

// Deducing this form: this is explicit
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType;  // self is this
```

The trick lives in `Self&&`. It looks like an rvalue reference, but because `Self` is a template parameter it actually degrades into a forwarding reference. A forwarding reference shape-shifts based on the value category of the actual argument: an lvalue call like `cb.run(args)` deduces `Self` as `OnceCallback&`; `std::move(cb).run(args)` makes `Self` a prvalue `OnceCallback`; a const lvalue `std::as_const(cb).run(args)` gives `const OnceCallback&`. Three value categories, one template that catches them all.

#### How we put it to work

Once the deduction rules are clear, blocking lvalue calls is a one-liner:

```cpp
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType {
    static_assert(!std::is_lvalue_reference_v<Self>,
        "OnceCallback::run() must be called on an rvalue. "
        "Use std::move(cb).run(...) instead.");
    return std::forward<Self>(self).impl_run(std::forward<FuncArgs>(args)...);
}
```

`std::is_lvalue_reference_v<Self>` is a compile-time constant that asks whether `Self` is an lvalue reference. When the caller writes `cb.run(args)`, `Self` deduces to `OnceCallback&`, which is an lvalue reference, the condition negates and trips the `static_assert`, and the compiler errors on the spot with the "use std::move" message we wrote. When the caller writes `std::move(cb).run(args)`, `Self` is a prvalue type, the `static_assert` lets it through, and `std::forward<Self>(self).impl_run(...)` hands the work to the real implementation. We use `std::forward<Self>(self)` rather than `self.impl_run(...)` deliberately, so that `impl_run` is invoked on an rvalue and the value category is not lost at this step.

One detail I find worth chewing on: the `static_assert` condition hangs off the template parameter `Self`, so it is only evaluated at the moment of instantiation. In other words, if nobody calls `run()`, this assert never fires, no matter whether the would-be caller is an lvalue or an rvalue. Only when someone writes an actual call site and the compiler has to instantiate the template does `Self` resolve to a concrete type and the assert get evaluated. This mechanism is called lazy instantiation, and it is bread and butter in template metaprogramming.

#### Compared with Chromium's approach

Chromium does not get to enjoy C++23. It walks the old road of two overloads: `Run() &&` is the version that actually executes, and `Run() const&` stuffs a `static_assert(!sizeof(*this), "...")` in to force a compile error. That `!sizeof` hack leans on a property of C++: `sizeof` can only be evaluated on a complete type, so once `!sizeof(*this)` gets evaluated it proves we are inside the class definition (where `*this` is complete), and the value has to be `false`. Before C++23, writing `static_assert(false, "...")` directly would fire on every code path, even the ones where this overload is never called, which is why Chromium had to reach for the `!sizeof` detour. C++23 loosened that restriction, but Chromium's codebase has not migrated wholesale to C++23 yet, so the old form lives on.

Our deducing this version uses a single function template and lets the deduction of `Self` separate lvalue from rvalue cleanly, which is a big step up in readability over Chromium's two overloads plus the `!sizeof` hack. Credit where it is due: that convenience is bought by standing on the new standard's shoulders.

### The constructor's requires constraint

The constructor template carries a constraint that looks redundant at first glance:

```cpp
template<typename Functor>
    requires not_the_same_t<Functor, OnceCallback>
explicit OnceCallback(Functor&& f);
```

Why not just `template<typename Functor>` and be done with it? Because the templated constructor would otherwise steal work from the move constructor.

When we write `OnceCallback cb2 = std::move(cb1)`, the compiler has two paths in front of it: take the implicitly declared move constructor `OnceCallback(OnceCallback&&)`, or instantiate the template constructor as `OnceCallback(OnceCallback&&)` by setting `Functor = OnceCallback`. Intuitively the move constructor feels "more specific" and should win. But C++ overload resolution does not follow intuition. In some cases the signature instantiated from the template matches "more exactly" than the implicitly declared special member function, and the compiler picks the template version without a second thought. That pick is where the trouble starts: the template constructor most likely does not dutifully reset the source object to kEmpty.

Our implementation squashes this with a custom concept `not_the_same_t`. It is essentially `!std::is_same_v<std::decay_t<F>, T>`, meaning "exclude this template when `F`, after decay, is exactly `T`". Decay's job here is to strip the references and cv-qualifiers off `F`: `F` might be `OnceCallback&&` or `const OnceCallback&`, and decay turns both back into `OnceCallback`. With the constraint attached, the moment the incoming type is `OnceCallback` itself the template drops out, and only then does the compiler fall back to the move constructor.

This pattern is everywhere when you write move-only type-erased wrappers. The implementation of `std::move_only_function` itself carries a similar constraint. If you ever build a component like this, burn the pattern in: template constructor plus a requires that excludes the self type is what backstops correct move-semantics matching.

### The internal shape of consumption semantics

The main line of `impl_run` is easy to read at a glance: check state, check cancellation, run the callable, flip the state. But a few details only register after you have stepped on them.

First, the cancellation check has to come before execution. `impl_run` looks at the token first. If it has already been cancelled, the callback is consumed without running: for a void return type we just return, for a non-void return we throw `std::bad_function_call`. Throwing here feels aggressive at first, but the reasoning is solid. The caller is waiting on a return value, and we have no way to conjure a meaningful one out of thin air. Throwing is more honest than handing back an undefined value.

Second is the `if constexpr (std::is_void_v<ReturnType>)` branch. When the return type is void, the spelling `ReturnType result = func_(args...)` will not even compile, because void is not an assignable type. `if constexpr` picks the branch at compile time: the void case takes the "call but do not assign" path, the non-void case takes "call and assign to result". This is the standard recipe for handling void returns with `if constexpr`.

Third is the ordering of "consume then null", which I did not pay attention to at first and nearly got bitten by. `impl_run` has to move `func_` into a local variable first, then set `func_` to `nullptr` and `status_` to kConsumed, and only then execute the callable held in that local. That order is not negotiable. Pull the object out, mark the state, then run. That way, even if the callable throws internally, `status_` is already sitting at kConsumed and the callback cannot end up in a half-broken dirty state. The nulling step does more than flip a state, by the way. It triggers `std::move_only_function` to destruct the callable it holds, which in turn releases whatever resources the captured lambda owned (a `unique_ptr`, say).

### Verifying the core skeleton

Once the skeleton is written, four scenarios are enough to smoke-test it: basic return type, void return, move-only capture, and move semantics. If those four pass, meaning: constructing a callback gives the right return value, a void callback executes cleanly, a callback capturing a `unique_ptr` releases its resource after running, and after a move the source is empty while the target is valid, the skeleton holds. The full test suite is collected in the third post.

---

## Step 2: Argument binding with `bind_once()`

### The problem we are solving

The scenario for `bind_once` fits in one sentence. You have a three-parameter function `f(int, int, int)`, the first two arguments are known at bind time (say 10 and 20), and only the third has to wait until call time. What you want is a `OnceCallback<int(int)>` that takes a single argument, and when it runs it lines 10 and 20 up with the argument you passed in and hands the lot to the original function.

That is argument binding. Stuff the known arguments into the callback ahead of time, and let the caller worry only about the unknown ones. Chromium's `BindOnce` goes to considerable lengths on argument lifetime (`Unretained`, `Owned`, `Passed`, `WeakPtr`, a whole pile of helpers); our simplified version only handles the core binding logic.

### The skeleton of `bind_once`

```cpp
template<typename Signature, typename F, typename... BoundArgs>
auto bind_once(F&& funtor, BoundArgs&&... args) {
    return OnceCallback<Signature>(
        [f = std::forward<F>(funtor),
         ...bound = std::forward<BoundArgs>(args)]
        (auto&&... call_args) mutable -> decltype(auto) {
            return std::invoke(
                std::move(f),
                std::move(bound)...,
                std::forward<decltype(call_args)>(call_args)...
            );
        }
    );
}
```

The code is short, but it hides several template tricks each worth pulling out on its own. We will take them one at a time.

### Lambda capture pack expansion

The line `...bound = std::forward<BoundArgs>(args)` is the lambda init-capture pack expansion syntax, which C++20 let out of the cage. The whole reason `bind_once` can be written this cleanly is that line.

Before C++20, the parameter pack of a variadic template could not be expanded directly into a lambda's capture list. You literally could not spell "capture each element of `args...` into the lambda as its own variable". The workaround was to pack every bound argument into a `std::tuple`, then call `std::apply` inside the lambda to unpack the tuple into separate arguments for the call. It works, but the code balloons: an extra tuple, a `std::apply` call, and a pile of template helper code to handle the move semantics of tuple elements.

C++20 finally relented. The effect of `...bound = std::forward<BoundArgs>(args)` is to generate one capture variable per type in `BoundArgs...`, each initialized with perfect forwarding through `std::forward`. Concrete example: if `BoundArgs...` is `int, std::string`, the expansion is equivalent to:

```cpp
[b1 = std::forward<int>(arg1), b2 = std::forward<std::string>(arg2)]
```

Each capture variable is independently usable inside the lambda. In our `bind_once`, when the lambda is called they get unpacked together through `std::move(bound)...` and fed to `std::invoke`. One trap worth flagging: we use `std::move`, not `std::forward`. The lambda is marked `mutable`, so the captured variables are lvalues inside the lambda body, and to send them out as rvalues we have to move them explicitly.

### The unified call surface of `std::invoke`

Inside the lambda we call `std::invoke` rather than writing `f(...)` directly, because `std::invoke` flattens the differences between callable shapes. Calling an ordinary function pointer directly is fine, but a member function pointer is a different animal: you cannot write `(&Class::method)(obj, args...)`, you have to switch to the special syntax `(obj.*method)(args...)`. `std::invoke` papers over all of that: `std::invoke(&Class::method, &obj, args...)` is equivalent to `(obj.*method)(args...)`.

That gives `bind_once` member-function binding for free, with no extra code:

```cpp
struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

Calculator calc;
auto bound = bind_once<int(int)>(&Calculator::multiply, &calc, 5);
int r = std::move(bound).run(8);  // r == 40
```

There is a lifetime trap buried here that I have to flag. `&calc` is a raw pointer, and `bind_once` does not manage its life at all. If `calc` destructs before the callback actually runs, `std::invoke` will follow the dangling pointer straight into freed memory. Classic use-after-free. Chromium ships a whole set of helpers around this: `base::Unretained` to declare explicitly "I know what I am doing with this raw pointer's lifetime", `base::Owned` to take over ownership, and `base::WeakPtr` to void the callback when the object destructs. In our simplified version that safety burden stays on the caller's shoulders.

### Signature deduction: why `Signature` has to be spelled out

You probably noticed that the first template parameter of `bind_once`, `Signature` (something like `int(int)`), has to be written by the caller. In an ideal world the compiler would look at the callable signature of `F`, lop off the bound parameters, and hand you back "the remaining signature" automatically. In C++ that is much harder than it sounds.

A function pointer `R(*)(Args...)` is the easy case: a template partial specialization lifts out the parameter list, then a compile-time "type-list slice" chops off the first N types. A functor with a fixed signature is workable too, since `decltype(&T::operator())` digs the signature out. But a generic lambda (`[](auto x) { ... }`) kills the whole approach. Its `operator()` is itself a template, so there is no single fixed signature, and the compiler cannot ask "what parameters does this lambda take" as a type-level question at all.

Chromium wrote a whole type-manipulation toolkit for this (`MakeUnboundRunType`, `DropTypeListItem`, and friends), a few hundred lines of template metaprogramming wrestling with one edge case after another. For our teaching purposes, asking the caller to spell out one more template parameter like `int(int)` is the pragmatic call. We skip a fat chunk of metaprogramming and the code stays readable.

---

## Step 3: Cancellation checks with `is_cancelled()` and `maybe_valid()`

### What the cancellation token is for

When a callback is created it can be hooked up to a cancellation token. The token stands for the life and death of some external object: once that object is gone, the token goes invalid with it, and every callback tied to that token enters the "cancelled" state.

Think of it as a pass. When the callback is born it gets issued a pass stamped "valid". The day the external object says "void this pass" (by calling `invalidate()`), every callback holding that pass will check the pass before running, see it has been stamped void, and quietly skip itself. In Chromium that pass is the control block inside `WeakPtr`: the moment the object a `WeakPtr` points at destructs, a flag in the control block flips, and every callback bound to that `WeakPtr` is voided.

### The shape of `CancelableToken`

Our simplified token has three core actions: create (issue a valid one), invalidate (stamp it void), and check (ask whether it is still valid). Internally it holds a `Flag` struct wrapping an `atomic<bool>` behind a `shared_ptr`:

```cpp
class CancelableToken {
    struct Flag {
        std::atomic<bool> valid{true};  // atomic: thread-safe
    };
    // every token copy shares the same Flag
    std::shared_ptr<Flag> flag_;

public:
    CancelableToken() : flag_(std::make_shared<Flag>()) {}
    void invalidate() { flag_->valid.store(false, std::memory_order_release); }
    bool is_valid() const {
        return flag_->valid.load(std::memory_order_acquire);
    }
};
```

Why a `shared_ptr` and not a raw pointer? So the token can be copied and moved around while every copy still shares one and the same `Flag`. The `atomic<bool>` is what keeps multi-threaded access safe: one thread is reading `is_valid()` over here, another thread calls `invalidate()` over there, and the `memory_order_acquire` / `memory_order_release` pair lines the two sides up so the read is guaranteed to see the write.

### Slotting it into `OnceCallback`

The way the token slots into `OnceCallback` is straightforward. A data member holds an optional `shared_ptr<CancelableToken>`, set through `set_token()`, and then it gets checked in two places: once when `is_cancelled()` is queried, and once before `impl_run()` actually runs.

The `is_cancelled()` logic is one line: if the state is anything other than kValid, return true (an empty or consumed callback both count as "cancelled"), and if a token is present and invalid, also return true. On the `impl_run` side, right before the callable is about to run, the token gets a glance. If it has been cancelled, the callback is consumed without running: the void case returns, the needs-a-return-value case throws `std::bad_function_call`.

`maybe_valid()` for now is just `!is_cancelled()` in a wrapper. In Chromium's full implementation the two differ in how strong a thread-safety guarantee they offer. `is_cancelled()` may only be called on the sequence the callback is bound to (the one that created the callback), and returns a deterministic result; `maybe_valid()` can be called from any thread, but the answer may already be stale. Our simplified version does not draw that distinction yet, but both method names are kept so they are ready for `RepeatingCallback` or cross-thread scenarios later.

---

## Step 4: Chained composition with `then()`

### What `then()` actually does

`then()` threads two callbacks into one pipe. The semantics in one sentence: when the pipe is called, the first callback runs with the original arguments, its return value is handed to the second callback, and that one runs. For example, callback A computes `3 + 4 = 7`, callback B computes `7 * 2 = 14`, and `then()` glued together gives you a new callback that walks the whole A then B pipeline when you run it.

Sounds simple. But `then()` is the one out of the four features where the ownership design demands the most thought.

### Ownership is the load-bearing wall

The new chained callback has to hold the ownership of both the original and the follow-up callback. Otherwise the original might get consumed somewhere outside, and the pipe snaps on the spot. The wrinkle is that `OnceCallback` is move-only, which means `then()` has to consume both `*this` (the original) and `next` (the follow-up), moving both into a fresh lambda closure. The whole ownership chain looks like this:

```mermaid
graph LR
    A["new callback"] --> B["move_only_function"] --> C["lambda closure"] --> D["original + follow-up"]
```

The skeleton of the implementation looks roughly like this:

```cpp
template<typename Next>
auto then(Next&& next) &&       // the trailing && makes this an rvalue-qualified member function
    -> OnceCallback</* return type and signature to be deduced */>
{
    return OnceCallback</* ... */>(
        [self = std::move(*this),             // move the entire original callback into the lambda
         cont = std::forward<Next>(next)]     // move the follow-up in too
        (FuncArgs... args) mutable -> decltype(auto) {
            if constexpr (std::is_void_v<ReturnType>) {
                std::move(self).run(std::forward<FuncArgs>(args)...);
                return std::invoke(std::move(cont));     // void → nothing to pass along
            } else {
                auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
                return std::invoke(std::move(cont), std::move(mid));  // hand the intermediate result on
            }
        }
    );
}
```

One difference from the Chromium original deserves a callout. We invoke the follow-up with `std::invoke`, not `.run()`. The reason is that `then()`'s `next` parameter is an ordinary callable (a lambda, say), not a `OnceCallback`, so there is no point making the caller spell out `std::move(cont).run()`. `std::invoke` does the job in one stroke. Only the `self` step (the original callback) needs `std::move(...).run()` to express the consumption semantics.

### A few traps I stepped on for you

`then()` has a few spots where I ate dirt, so let me walk through them.

First, the trailing `&&`. It qualifies the member function as rvalue-only, callable via `std::move(cb).then(next)` or off a temporary `.then(next)`. This is the other route to expressing consumption semantics; unlike `run()`, which uses deducing this, `then()` walks the traditional ref-qualifier path. Why not deducing this here? Because `then()` has no call for different error messages on lvalue versus rvalue. It only eats rvalues, there is no middle ground, and a ref-qualifier is already clean enough.

Second, the line `self = std::move(*this)`. It moves everything the current `OnceCallback` owns, in one lump, into the lambda's closure object. After the move, the current object is left in a consumed state (we do not reset it back to kEmpty, we just let it sit in its moved-out shape). That closure object then gets stuffed into the `move_only_function` of the returned new `OnceCallback`. Type erasure is what guarantees that no matter what type the lambda actually is, it can be stored uniformly.

Third, the `mutable` keyword is not optional. The `operator()` a lambda generates by default is `const`, which means the lambda is not allowed to mutate its captures inside the body. But we very much need to call `std::move(self).run()` on `self` inside the lambda, and that call mutates the object (flipping status from kValid to kConsumed). So the lambda has to be marked `mutable`, which turns `operator()` non-const.

Fourth, our old friend `if constexpr (std::is_void_v<ReturnType>)`. Same reasoning as in `impl_run`. When the original callback returns `void`, the semantics of `then()` are "run the original, then run the follow-up, pass nothing in between". `if constexpr` picks the branch at compile time, and the two cases generate two fully separate code paths.

### Multi-stage pipelines

`then()` can keep chaining, stacking up a multi-stage pipe:

```cpp
using namespace tamcpp::chrome;
auto pipeline = OnceCallback<int(int)>([](int x) {
    return x * 2;
}).then([](int x) {
    return x + 10;
}).then([](int x) {
    return std::to_string(x);
});

std::string result = std::move(pipeline).run(5);
// 5 * 2 = 10, 10 + 10 = 20, "20"
```

Each call to `then()` produces a new `OnceCallback` that nests a capture of the previous step. The call order unfolds recursively from the outside in: the outermost gets `run()`-ed, its lambda runs, the lambda calls `std::move(self).run()` on the layer above, which calls the one above that, all the way down. On performance, each `then()` layer adds one indirect call through `std::move_only_function`, which is perfectly survivable for a 2- or 3-stage pipe. If the depth really does climb past 10 stages, you could build a flattened pipeline with `std::variant` and dodge the nested-closure overhead, but that is well outside our scope here.

The next post brings systematic test cases, walks through each of these design choices one by one, and compares the performance trade-offs we made against the Chromium original.

## References

- [Chromium callback.h source](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [Chromium bind_internal.h source](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/bind_internal.h)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [P0847R7 - Deducing this proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
- [P0780R2 - Pack Expansion in Lambda Capture](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0780r2.html)
