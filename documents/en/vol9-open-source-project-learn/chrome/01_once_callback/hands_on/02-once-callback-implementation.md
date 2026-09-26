---
chapter: 1
cpp_standard:
- 23
description: "From the core skeleton to a complete component: a four-step walkthrough of once_callback's implementation strategy, focused on the template techniques and the ownership design"
difficulty: advanced
order: 2
platform: host
prerequisites:
- "once_callback Design Guide (I): motivation and API design"
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
title: "once_callback Design Guide (II): step-by-step implementation"
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/hands_on/02-once-callback-implementation.md
  source_hash: 24b9eba26b70b96e5dc938fa587c544cd74d868002ab505cfd4bd9c5093612ad
  translated_at: '2026-09-26T01:13:02+00:00'
  engine: anthropic
  token_count: 4900
---
# once_callback Design Guide (II): step-by-step implementation

In the previous post we settled the target API and the internal architecture of `OnceCallback`. This post is where we write the code. But let me get the disclaimer out of the way first: I am not going to serve the complete header file here — pasted out in full it runs to several hundred lines, and staring at it is a reliable way to lose focus. We will walk through only the skeleton and the template tricks that genuinely take brainpower, and think the "why is it written this way" part through properly. The complete, compilable code is left for the exercises and for the third post on testing.

The implementation breaks into four steps, stacked one on top of another: first get the `run()` semantics — the lifeline — working; then add `bind_once()` argument binding; next hang the cancellation check on it; and finally wire up `then()` chaining. At each step we keep only two questions in view: what does the thing look like, and where is the key template trick hiding.

---

## Step 1: The core skeleton — starting from template partial specialization

### Why `OnceCallback<R(Args...)>` is written this way

If you have skimmed the standard library, you will have noticed that `std::function` and `std::move_only_function` share a shape — the template parameter is not a return type and a parameter list written separately, but one whole function signature stuffed in. Our `OnceCallback` follows the same convention, precisely for the cleanliness of this signature-style template parameter.

The trick doing the work underneath is template partial specialization. We start by throwing out a bare primary template, declared but not defined:

```cpp
template<typename FuncSignature>
class OnceCallback;  // primary template: no implementation provided
```

Then we open a separate partial specialization that catches exactly the case where the signature is a function type:

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    // all the real code lives in this partial specialization
};
```

When you write `OnceCallback<int(int, int)>`, the compiler first feeds `int(int, int)` into the primary template's `FuncSignature` as one whole type, then notices that the partial specialization can split that whole apart — `ReturnType = int`, `FuncArgs... = {int, int}` — so the specialization is selected. The payoff of this spelling is plain: users specify the callback type with natural function-signature syntax, instead of passing return type and parameter list in as two separate template parameters.

There is a small trap here that trips people: the `R(Args...)` spelling looks like a function declaration, but in a template parameter position it is actually a function type. `int(int, int)` is a legal type in C++ in its own right, describing "a function that eats two ints and spits out one int". The partial specialization simply hitches a ride on that, unpacking it through pattern matching.

### Internal storage: what the class skeleton looks like

In the previous post we settled on a three-state architecture. Now we stand the class skeleton up — ignore the method implementations for the moment and just look at the data members and the interface signatures:

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    // Core storage: holds the actual callable object
    // A lambda, a function pointer, or a functor — it holds them all
    std::move_only_function<FuncSig> func_;

    // Three-state flag: kEmpty → kValid → kConsumed
    Status status_ = Status::kEmpty;

    // Cancellation token (optional)
    std::shared_ptr<CancelableToken> token_;

public:
    // Construction: accepts any callable (with a requires constraint, explained later)
    template<typename Functor>
        requires not_the_same_t<Functor, OnceCallback>
    explicit OnceCallback(Functor&& f);

    // Move-only: copies deleted
    OnceCallback(const OnceCallback&) = delete;
    OnceCallback& operator=(const OnceCallback&) = delete;
    OnceCallback(OnceCallback&& other) noexcept;
    OnceCallback& operator=(OnceCallback&& other) noexcept;

    // The core: runs the callback and consumes *this (via deducing this, explained later)
    template<typename Self>
    auto run(this Self&& self, FuncArgs&&... args) -> ReturnType;

    // Query interface
    [[nodiscard]] bool is_cancelled() const noexcept;
    [[nodiscard]] bool maybe_valid() const noexcept;
    [[nodiscard]] bool is_null() const noexcept;
    explicit operator bool() const noexcept;

    // Set the cancellation token
    void set_token(std::shared_ptr<CancelableToken> token);

    // Chaining
    template<typename Next> auto then(Next&& next) &&;

private:
    ReturnType impl_run(FuncArgs... args);  // the actual execution logic
};
```

Every member has a clear job. `func_` does the dirty work of type erasure: whatever you feed it — lambda, function pointer, or functor — it folds everything into one invocation slot with a known signature. `status_` is a three-state enum that separates the phases "never assigned" (kEmpty), "ready to run at any time" (kValid), and "already run" (kConsumed). `token_` is an optional cancellation token that checks the door for you before the callback really runs. Move operations amount to pointer-level transfers, and the source object lands back in kEmpty once it has been moved from.

With the skeleton standing, two spots have the highest density of template trickery — the deducing this in `run()` and the `requires` constraint on the constructor. We will pull those two out and go through them thoroughly; the rest then reads easily.

### deducing this: letting the compiler block wrong calls for us

`run()` is the soul of the whole component, and the method where C++23 features are packed most densely. First, stare at its declaration:

```cpp
template<typename Self>
auto run(this Self&& self, Args... args) -> R;
```

That `this Self&& self` stopped me for a beat the first time I saw it; only later did I work out that this is C++23 deducing this, officially the "explicit object parameter". In a traditional member function `this` is implicit — the compiler quietly slips in the address of the current object, where you can neither see nor touch it. What deducing this does is write `this` explicitly as the function's first parameter, and then use template argument deduction on its type and value category.

```cpp
// Traditional style: this is implicit
void run(FuncArgs... args);          // what the compiler sees is run(OnceCallback* this, FuncArgs... args)

// Deducing this style: this is explicit
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType;  // self is this
```

The trick lives in `Self&&`: it looks like an rvalue reference, but because `Self` is a template parameter it degrades into a forwarding reference. The beauty of a forwarding reference is that it changes face according to the value category of the argument: for an lvalue call like `cb.run(args)`, `Self` is deduced as `OnceCallback&`; written as `std::move(cb).run(args)`, `Self` becomes the prvalue `OnceCallback`; and the const lvalue `std::as_const(cb).run(args)` gives `const OnceCallback&`. Three value categories, and one template catches them all.

#### How we put it to work

Once you know the deduction rules, intercepting lvalue calls is a one-liner:

```cpp
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType {
    static_assert(!std::is_lvalue_reference_v<Self>,
        "OnceCallback::run() must be called on an rvalue. "
        "Use std::move(cb).run(...) instead.");
    return std::forward<Self>(self).impl_run(std::forward<FuncArgs>(args)...);
}
```

`std::is_lvalue_reference_v<Self>` is a compile-time constant that asks exactly one question: is `Self` an lvalue reference. When the caller writes `cb.run(args)`, `Self` is deduced as `OnceCallback&` — an lvalue reference — the negated condition fires the `static_assert`, the compiler errors out on the spot, and the message it prints is precisely our sentence "you must use std::move". Written as `std::move(cb).run(args)`, `Self` is a prvalue type, the `static_assert` lets it through, and `std::forward<Self>(self).impl_run(...)` dispatches the work to the real implementation. The deliberate choice of `std::forward<Self>(self)` over a plain `self.impl_run(...)` is to guarantee that `impl_run` is invoked on an rvalue — the value category must not be lost at this step.

There is a detail I find worth savoring: the `static_assert` condition hangs off the template parameter `Self`, so it is only evaluated at the moment the template is instantiated. In other words, as long as nobody calls `run()`, this assert never fires, regardless of whether the argument would be an lvalue or an rvalue. Only when someone actually writes a call site somewhere and the compiler has to instantiate the template does the concrete type of `Self` get pinned down and the assert get evaluated. This mechanism is called lazy instantiation, and it is everyday fare in template metaprogramming.

#### Compared with Chromium's approach

Chromium does not get to enjoy C++23's benefits; it goes the old two-overload route: `Run() &&` is the version that really executes, while `Run() const&` stuffs in a `static_assert(!sizeof(*this), "...")` to deliberately manufacture a compile error. That `!sizeof` hack exploits a property of C++ — `sizeof` can only be evaluated on a complete type, so once `!sizeof(*this)` is evaluated, it means we are inside the class definition at that moment (`*this` is a complete type), and the value is necessarily `false`. Before C++23, writing `static_assert(false, "...")` directly would fire on all code paths, even if the overload had never been called, so Chromium had to take the roundabout `!sizeof` spelling. C++23 loosened that restriction, but Chromium's codebase has not fully migrated to C++23, so the old spelling stays as it is.

With our deducing this approach, a single function template separates lvalue from rvalue cleanly through the deduction of `Self` — a good stretch cleaner than Chromium's two overloads plus the `!sizeof` hack. That is a bargain earned by standing on the shoulders of the new standard, and fairness demands I say so.

### The constructor's requires constraint

The constructor template carries a constraint that looks redundant at first glance:

```cpp
template<typename Functor>
    requires not_the_same_t<Functor, OnceCallback>
explicit OnceCallback(Functor&& f);
```

Why not just `template<typename Functor>` and be done? No — the problem is the template constructor fighting the move constructor for the same job.

When we write `OnceCallback cb2 = std::move(cb1)`, the compiler has two roads in front of it: take the implicitly declared move constructor `OnceCallback(OnceCallback&&)`, or instantiate the template constructor as `OnceCallback(OnceCallback&&)` (with `Functor = OnceCallback`). Intuition says the move constructor is "more special" and should win. But C++ overload resolution does not follow intuition — in some cases the signature instantiated from the template matches even more "precisely" than the implicitly declared special member function, and the compiler picks the template version without a second thought. That choice is where things go wrong: the template constructor will most likely not faithfully reset the source object's state back to kEmpty.

Our implementation pins this down with a custom concept, `not_the_same_t`: it is essentially `!std::is_same_v<std::decay_t<F>, T>`, meaning "when `F`, after decay, is exactly `T` itself, exclude this template". The job of decay here is to strip references and cv-qualifiers off `F` — `F` might be `OnceCallback&&`, or `const OnceCallback&`; after decay they all turn back into plain `OnceCallback`. With the constraint attached, whenever what comes in is a `OnceCallback` itself the template is out of the game immediately, and only then does the compiler obediently go match the move constructor.

This pattern is extremely common when writing move-only type-erased wrappers — `std::move_only_function`'s own implementation hangs a similar constraint on itself. If you ever build this kind of component yourself, burn this pattern in: a template constructor plus a requires clause excluding the class's own type is the safety net that lets move semantics match correctly.

### How the consume semantics are implemented inside

The backbone logic of `impl_run` reads at a glance: check status, check cancellation, run the callable, flip the status. But a few details only revealed their subtlety after I had stepped in them.

First, the cancellation check has to come before execution. `impl_run` looks at whether the token is still valid — if it has already been cancelled, the callback is consumed but not executed: for a void return type we simply return, for non-void we throw `std::bad_function_call`. Throwing here looks aggressive at first glance, but the justification is solid: the caller is waiting expectantly for a return value, and we cannot conjure up a meaningful one out of thin air; throwing is far more respectable than returning an undefined value.

The second detail is the `if constexpr (std::is_void_v<ReturnType>)` branch. When the return type is void, a spelling like `ReturnType result = func_(args...)` cannot even compile — void is simply not an assignable type. `if constexpr` picks the branch at compile time: the void case goes down the "call but do not assign" path, the non-void case down the "call and assign to result" path. This is the standard maneuver for handling void returns.

The third is the ordering of the null-out-after-consumption step, which I paid no attention to at first and was nearly burned by later. `impl_run` must first move `func_` into a local variable, then set `func_` to `nullptr` and `status_` to kConsumed, and only then execute the callable sitting in that local variable. This order must never be flipped — move the object out, mark the state, then start running. That way, even if the callable throws internally, `status_` is already safely kConsumed, and the callback cannot be left stuck in a neither-here-nor-there dirty state. The null-out step is not just a state change, either — it triggers `std::move_only_function` destroying the callable it holds inside, which in turn releases the resources captured by the lambda (a `unique_ptr`, say).

### Validating the core skeleton

With the skeleton written, four scenarios are enough for a smoke test: fundamental-type return, void return, move-only capture, and move semantics. If all four pass — constructing a callback and getting the right return value, a void callback executing normally, a callback capturing a `unique_ptr` releasing its resource after running, and after a move the source left empty while the target stays valid — the skeleton holds. The full test cases are left for the third post to deal with together.

---

## Step 2: Argument binding — `bind_once()`

### The problem we are solving

The scenario for `bind_once` fits in one sentence: you have a three-parameter function `f(int, int, int)`, the first two parameters are already known at bind time (say 10 and 20), and only the third has to wait until the moment of the call. What we want is a `OnceCallback<int(int)>` that eats just one parameter, and when it runs, automatically assembles 10, 20, and the argument you pass in and feeds the lot to the original function.

That is argument binding — stuffing the "known arguments" into the callback up front, so the caller only worries about the "unknown arguments". Chromium's `BindOnce` goes to great lengths in this area handling argument lifetimes (`Unretained`, `Owned`, `Passed`, `WeakPtr` — a whole pile of helpers); our simplified version manages only the core binding logic.

### The `bind_once` implementation skeleton

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

The snippet is short, yet it hides several template tricks, each worth pulling out on its own. Let's take them apart one by one.

### Lambda Capture Pack Expansion

The line `...bound = std::forward<BoundArgs>(args)` is the lambda init-capture pack expansion syntax that C++20 unlocked. The entire reason `bind_once` can be written this cleanly is this feature.

Before C++20, the parameter pack of a variadic template could not be expanded directly into a lambda's capture list — you could not write "capture each element of `args...` into the lambda individually". The homespun workaround was to pack all the bound arguments into a `std::tuple` and unpack it inside the lambda with `std::apply` for the call. It works, but the code bloats by a good chunk — an extra tuple, one `std::apply`, plus template helper code for handling the move semantics of the tuple elements.

C++20 finally relented. The effect of `...bound = std::forward<BoundArgs>(args)` is that one capture variable is generated per type in `BoundArgs...`, each initialized with perfect forwarding via `std::forward`. Concretely, suppose `BoundArgs...` is `int, std::string`; the expansion is equivalent to:

```cpp
[b1 = std::forward<int>(arg1), b2 = std::forward<std::string>(arg2)]
```

Each capture variable is independently usable inside the lambda; in our `bind_once`, at the moment the lambda is invoked they are expanded together via `std::move(bound)...` and fed to `std::invoke`. One trap I must flag here: we use `std::move`, not `std::forward` — because the lambda is marked `mutable`, the captured variables are lvalues inside the lambda body, and we need to send them out as rvalues for move semantics to kick in.

### `std::invoke`: uniform invocation

Inside the lambda we use `std::invoke`, not a direct `f(...)`. The reason is that `std::invoke` flattens away the differences between kinds of callables. Calling an ordinary function pointer directly is fine, but a pointer to member function is another story — you cannot write `(&Class::method)(obj, args...)`; you must switch to the dedicated syntax `(obj.*method)(args...)`. `std::invoke` folds all of these variations in: `std::invoke(&Class::method, &obj, args...)` is exactly equivalent to `(obj.*method)(args...)`.

As a result, `bind_once` supports member-function binding for free, without a single extra line of code:

```cpp
struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

Calculator calc;
auto bound = bind_once<int(int)>(&Calculator::multiply, &calc, 5);
int r = std::move(bound).run(8);  // r == 40
```

But a lifetime trap is buried right here, and I absolutely must warn you: `&calc` is a raw pointer, and `bind_once` does not care in the least whether it lives or dies. If `calc` gets destroyed before the callback actually runs, `std::invoke` will follow the dangling pointer and grope over already-freed memory — a textbook use-after-free. Chromium ships a full supporting cast for this — `base::Unretained` explicitly declares "I know what I am doing with this raw pointer's lifetime", `base::Owned` takes ownership over, and `base::WeakPtr` invalidates the callback when the object is destroyed. In our simplified version, that safety burden rests on the caller's shoulders for now.

### Signature deduction: why `Signature` must be spelled out

You have probably noticed that the first template parameter of `bind_once`, `Signature` (something like `int(int)`), has to be written out by the caller. Ideally, the compiler ought to be able to deduce the "remaining signature after dropping the bound arguments" from `F`'s callable signature. In C++, that turns out to be much harder to pull off than it sounds.

A function pointer `R(*)(Args...)` is the easy case: a template partial specialization lifts out the parameter list, and one more compile-time "type-list slice" chopping off the first N types finishes the job. A functor with a fixed signature is also manageable — `decltype(&T::operator())` can dig the signature out. But a generic lambda (`[](auto x) { ... }`) is where it all falls over — its `operator()` is itself a template, so no single fixed signature exists at all, and the compiler cannot ask, at the type level, "what parameters does this lambda eat".

Chromium wrote a whole suite of type-manipulation utilities for this (`MakeUnboundRunType`, `DropTypeListItem`, and the like) — hundreds of lines of template metaprogramming, end to end, to cope with every corner case. For our teaching purposes, making the caller write one more template parameter `int(int)` is simply the pragmatic choice — entire stretches of hairy metaprogramming are saved, and the code stays crisp.

---

## Step 3: Cancellation checks — `is_cancelled()` and `maybe_valid()`

### What the cancellation token is for

When a callback is created, it can have a "cancellation token" attached. Behind the token stands the life and death of some external object — once that object is gone, the token is invalidated with it, and every callback associated through that token enters the "cancelled" state.

Just think of it as a pass: the callback is issued one at birth, stamped "valid". Some day the external object says "passes are void" (someone calls `invalidate()`), and from then on, every callback holding that pass, when it takes a look before executing, finds "this pass has already been stamped void" and quietly skips itself without running. In Chromium, this pass is the control block inside `WeakPtr` — as soon as the object the `WeakPtr` points at is destroyed, the flag bit in the control block flips, and the callbacks bound to that `WeakPtr` invalidate themselves automatically.

### The design of `CancelableToken`

Our simplified token has three core actions: create (issue a valid one), invalidate (stamp it void), and check (ask whether it is still valid). Internally, a `shared_ptr` manages a `Flag` struct holding an `atomic<bool>`:

```cpp
class CancelableToken {
    struct Flag {
        std::atomic<bool> valid{true};  // atomic variable, thread-safe
    };
    // All token copies share the same Flag
    std::shared_ptr<Flag> flag_;

public:
    CancelableToken() : flag_(std::make_shared<Flag>()) {}
    void invalidate() { flag_->valid.store(false, std::memory_order_release); }
    bool is_valid() const {
        return flag_->valid.load(std::memory_order_acquire);
    }
};
```

Why `shared_ptr` instead of a raw pointer? So that the token can be copied and moved around while all copies keep sharing the same `Flag`. The `atomic<bool>` buys safety under multithreading — one thread is in the middle of querying `is_valid()` while another thread has already called `invalidate()` — and the `memory_order_acquire/release` pair aligns the two sides exactly: the former's read is guaranteed to see the latter's write.

### Plugging it into `OnceCallback`

The way the token gets into `OnceCallback` is plain: a data member holds an optional `shared_ptr<CancelableToken>`, set in via `set_token()`, and two places consult it — one is the `is_cancelled()` query, the other is `impl_run()` right before the real run.

The logic of `is_cancelled()` in one sentence: return true whenever the status is anything other than kValid (both the empty callback and the consumed callback count as "cancelled"), and also return true if there is a token and the token has been invalidated. Over on the `impl_run` side, it glances at the token before actually executing the callable; if it has been cancelled, the callback is consumed but not executed — the void case returns directly, the case needing a return value throws `std::bad_function_call`.

For now, `maybe_valid()` is just `!is_cancelled()` in a shell. In Chromium's full implementation, the difference between the two lies in the strength of the thread-safety guarantee — `is_cancelled()` may only be called on the sequence the callback was bound to (the line of execution that created the callback) and returns a definitive answer; `maybe_valid()` can be called from any thread, but the answer may already be stale. Our simplified version does not split that hair for now, but both method names stay, because they will be needed later in `RepeatingCallback` or cross-thread scenarios.

---

## Step 4: Chaining — `then()`

### What `then()` actually does

`then()` threads two callbacks into one pipeline. The semantics in one sentence: when the pipeline is invoked, the first callback runs with the original arguments, and whatever it spits out is handed to the second callback to keep running. For example, callback A computes `3 + 4 = 7`, callback B computes `7 * 2 = 14`; chain them with `then()` and what you get is a new callback that, when run, walks the whole A → B flow automatically.

It sounds simple, but `then()` is the one of the four features whose ownership design takes the most thought.

### Ownership is the crux

The new chained callback must clutch the ownership of both the original callback and the continuation in its hands — otherwise, if one day the original callback gets consumed early by someone on the outside, the pipeline snaps on the spot. And as it happens, `OnceCallback` is move-only, which means `then()` has no choice but to consume `*this` (the original callback) and `next` (the continuation), moving ownership of both into a new lambda closure. The whole ownership chain looks like this:

```mermaid
graph LR
    A["new callback"] --> B["move_only_function"] --> C["lambda closure"] --> D["original callback + continuation"]
```

The implementation skeleton looks roughly like this:

```cpp
template<typename Next>
auto then(Next&& next) &&       // the trailing && makes this an rvalue-qualified member function
    -> OnceCallback</* return type and signature to be deduced */>
{
    return OnceCallback</* ... */>(
        [self = std::move(*this),             // move the whole original callback into the lambda
         cont = std::forward<Next>(next)]     // move the continuation in as well
        (FuncArgs... args) mutable -> decltype(auto) {
            if constexpr (std::is_void_v<ReturnType>) {
                std::move(self).run(std::forward<FuncArgs>(args)...);
                return std::invoke(std::move(cont));     // void → nothing passed on
            } else {
                auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
                return std::invoke(std::move(cont), std::move(mid));  // pass the intermediate result on
            }
        }
    );
}
```

One difference from Chromium's original deserves a remark: we use `std::invoke` on the continuation, not `.run()`. The reason is that the `next` parameter `then()` receives is an ordinary callable (a lambda, say), not a `OnceCallback` — there is no need to make the caller strain over writing `std::move(cont).run()`; `std::invoke` does the whole job in one stroke. Only the `self` step (the original callback) needs `std::move(...).run()` to express the consume semantics.

### A few pitfalls I have stepped in for you

A few spots around `then()` have taken me down; let's go through them one at a time.

First, the trailing `&&`. It qualifies the member function as the rvalue version, callable only through `std::move(cb).then(next)` or a temporary object's `.then(next)`. This is another route for expressing "consume semantics" — unlike `run()` with deducing this, `then()` goes straight with the traditional ref-qualifier. Why not deducing this here too? Because `then()` has no need to give different error messages for lvalue versus rvalue — it eats rvalues only, there is no middle ground, and a ref-qualifier is already clean enough.

Next, the line `self = std::move(*this)`. It moves the entire estate of the current `OnceCallback` object, in one go, into the lambda's closure object. After the move, the current object is in the consumed state (we do not set it back to kEmpty; we let it naturally keep its "emptied out" look). That closure object is then stuffed into the `move_only_function` of the newly returned `OnceCallback` — type erasure's craft guarantees that whatever the lambda's actual type is, it can be stored uniformly.

Then there is `mutable` — not one letter of this keyword can be spared. The `operator()` a lambda generates by default is `const`, which means the captured variables may not be touched inside the lambda. But we precisely want to call `std::move(self).run()` on `self` inside the lambda, and that step mutates object state (flipping status from kValid over to kConsumed). So the lambda must be marked `mutable`, which makes its `operator()` non-const.

Finally, the old friend `if constexpr (std::is_void_v<ReturnType>)`. It is the same story as in `impl_run`: when the original callback returns `void`, the semantics of `then()` are "run the original callback first, then run the continuation, passing nothing in between". `if constexpr` picks the branch at compile time, and the two cases generate two entirely different code paths.

### Multi-stage pipelines

`then()` keeps chaining, assembling multi-stage pipelines:

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

Every call to `then()` begets a new `OnceCallback` that nests the captured callback of the previous step. The call order unfolds recursively from the outside in: the outermost layer is `run()` → its lambda runs → inside the lambda, `std::move(self).run()` is called on the layer above → which calls the layer above that → all the way down. Performance-wise, each extra `then()` layer adds one more `std::move_only_function` indirect call, which a 2-3 stage pipeline absorbs without complaint. If a pipeline ever really grows past 10 stages deep, you could build a flattened pipeline structure out of `std::variant` to dodge the nested-closure overhead — but that is beyond our present scope.

In the next post we bring in systematic test cases, verify these designs item by item, and, along the way, compare the performance trade-offs between our version and Chromium's original.

## References

- [Chromium callback.h source](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [Chromium bind_internal.h source](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/bind_internal.h)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [P0847R7 - Deducing this proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
- [P0780R2 - Pack Expansion in Lambda Capture](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0780r2.html)
