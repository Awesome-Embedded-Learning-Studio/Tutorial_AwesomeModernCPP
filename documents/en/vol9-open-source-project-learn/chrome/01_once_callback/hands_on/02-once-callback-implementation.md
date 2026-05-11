# once_callback Design Guide (Part 2): Step-by-Step Implementation

## Introduction

In the previous article, we completed the motivation analysis and interface design, establishing the target API and internal architecture for `OnceCallback`. In this article, we finally start writing code. But let's set expectations first—the focus here isn't "serving up the complete implementation," but rather walking you through the design rationale and key technical choices at each step. We will look at the critical skeleton of the code, but we won't paste the complete, immediately compilable header file—those details are left as an exercise and for the test verification in Part 3.

The implementation is divided into four steps, each building on the previous one: first, we nail down the core `run()` semantics; then we add argument binding, followed by cancellation checks, and finally `then()` chained composition. At each step, we only focus on "what does this component look like" and "what are the key template techniques," rather than doing a line-by-line code walkthrough.

> **Learning Objectives**
>
> - Understand the template partial specialization pattern and internal storage design of `OnceCallback<R(Args...)>`
> - Master the application of advanced template techniques—such as deducing this, requires constraints, and lambda capture pack expansion—in practical components
> - Understand the argument binding mechanism of `bind_once()` and the ownership chain design of `then()`

---

## Step 1: Core Skeleton — Starting with Template Partial Specialization

### Why the `OnceCallback<R(Args...)>` Syntax

You may have noticed that the way we declare `OnceCallback` is somewhat unusual—it's not `OnceCallback<R, Args...>`, but `OnceCallback<R(Args...)>`. This syntax is known as a "signature-style template parameter," and both `std::function` and `std::move_only_function` use the same approach.

The underlying technique is **template partial specialization**. We first declare a primary template with a declaration but no definition:

```cpp
template<typename FuncSignature>
class OnceCallback;  // 主模板：不提供实现
```

Then we provide a partially specialized version for when `FuncSignature` happens to be a function type:

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    // 所有真正的代码都在这个偏特化里
};
```

When a user writes `OnceCallback<int(int, int)>`, the compiler treats `int(int, int)` as a single whole type and matches it against the primary template's `FuncSignature`. It then discovers that the partial specialization can decompose this whole into a return type `ReturnType = int` and a parameter pack `FuncArgs... = {int, int}`, so it selects the partial specialization. The benefit of this pattern is that users can specify the callback's type using a very natural "function signature" syntax, without needing to pass the return type and parameter list separately.

There is an easily confused point here: `R(Args...)` looks like a function declaration, but in the context of a template parameter, it is a **function type**. `int(int, int)` is a valid C++ type—it describes "a function that takes two int parameters and returns an int." Template partial specialization leverages this type, using pattern matching to tear it apart and extract the return type and parameter pack.

### Internal Storage: What Does the Class Skeleton Look Like

In the previous article, we settled on a three-state architecture. Now let's look at the class skeleton—ignoring method implementations for now, and just looking at data members and interface signatures:

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    // 核心存储：持有实际的可调用对象
    // 不管你传入 lambda、函数指针还是仿函数，它都能装下
    std::move_only_function<FuncSig> func_;

    // 三态标记：kEmpty → kValid → kConsumed
    Status status_ = Status::kEmpty;

    // 取消令牌（可选）
    std::shared_ptr<CancelableToken> token_;

public:
    // 构造：接受任意可调用对象（带 requires 约束，后面解释）
    template<typename Functor>
        requires not_the_same_t<Functor, OnceCallback>
    explicit OnceCallback(Functor&& f);

    // Move-only：删除拷贝
    OnceCallback(const OnceCallback&) = delete;
    OnceCallback& operator=(const OnceCallback&) = delete;
    OnceCallback(OnceCallback&& other) noexcept;
    OnceCallback& operator=(OnceCallback&& other) noexcept;

    // 核心：执行回调并消费 *this（用 deducing this 实现，后面解释）
    template<typename Self>
    auto run(this Self&& self, FuncArgs&&... args) -> ReturnType;

    // 查询接口
    [[nodiscard]] bool is_cancelled() const noexcept;
    [[nodiscard]] bool maybe_valid() const noexcept;
    [[nodiscard]] bool is_null() const noexcept;
    explicit operator bool() const noexcept;

    // 设置取消令牌
    void set_token(std::shared_ptr<CancelableToken> token);

    // 链式组合
    template<typename Next> auto then(Next&& next) &&;

private:
    ReturnType impl_run(FuncArgs... args);  // 真正的执行逻辑
};
```

Every member in the skeleton has a clear responsibility. `func_` is responsible for type erasure—unifying various forms of callable objects into a known-signature call interface. `status_` is a three-state enum distinguishing "never assigned" (kEmpty), "ready to call" (kValid), and "already called" (kConsumed). `token_` is an optional cancellation token, used to check whether execution should be canceled before the callback runs. Move operations perform pointer-level transfers, leaving the source object in the kEmpty state.

Next, we focus on the two most ingenious parts of the skeleton: the deducing this technique in `run()` and the `requires` constraint on the constructor. These two areas are the most template-technique-dense parts of the entire component and deserve to be explained thoroughly on their own.

### Deducing this: Letting the Compiler Intercept Incorrect Calls for Us

`run()` is the soul of the entire component, and the method with the densest concentration of C++23 features. Let's look at its declaration first:

```cpp
template<typename Self>
auto run(this Self&& self, Args... args) -> R;
```

If you've never seen the `this Self&& self` syntax before, don't panic—we'll break it down step by step.

#### What is deducing this

Deducing this is a feature introduced in C++23, officially called "explicit object parameter." In traditional member functions, `this` is an implicit parameter—the compiler automatically passes in the address of the current object, invisible and untouchable. Deducing this allows us to explicitly write `this` as the first parameter of the function, and use a template parameter to deduce its type and value category.

```cpp
// 传统写法：this 是隐式的
void run(FuncArgs... args);          // 编译器看到的是 run(OnceCallback* this, FuncArgs... args)

// deducing this 写法：this 是显式的
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType;  // self 就是 this
```

The key lies in `Self&&`—it looks like an rvalue reference, but it is actually a **forwarding reference**, because `Self` is a template parameter. The special property of a forwarding reference is that it can be deduced as different types based on the value category of the passed argument:

- `cb.run(args)` — `cb` is an lvalue, `Self` is deduced as `OnceCallback&` (lvalue reference)
- `std::move(cb).run(args)` — `std::move(cb)` is an rvalue, `Self` is deduced as `OnceCallback` (prvalue)
- `std::as_const(cb).run(args)` — const lvalue, `Self` is deduced as `const OnceCallback&`

#### How We Leverage It

Knowing the deduction rules of `Self`, intercepting lvalue calls is straightforward:

```cpp
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType {
    static_assert(!std::is_lvalue_reference_v<Self>,
        "OnceCallback::run() must be called on an rvalue. "
        "Use std::move(cb).run(...) instead.");
    return std::forward<Self>(self).impl_run(std::forward<FuncArgs>(args)...);
}
```

`std::is_lvalue_reference_v<Self>` is a compile-time constant that checks whether `Self` is an lvalue reference type. When the caller writes `cb.run(args)`, `Self` is deduced as `OnceCallback&`, which is an lvalue reference, so the condition is `true`. Negated, the `static_assert` fails, and the compiler directly reports an error—the error message being the exact sentence we wrote. When the caller writes `std::move(cb).run(args)`, `Self` is deduced as `OnceCallback`, which is not a reference, so `static_assert` passes and execution enters the `impl_run` to perform the actual logic. Note that we use `std::forward<Self>(self)` here instead of `self.run_impl()`, ensuring that `impl_run` is correctly invoked on the rvalue.

There is a nuanced detail worth pondering: the condition in `static_assert` depends on the template parameter `Self`, so it is only evaluated upon template instantiation. This means that if `run()` is never called, `static_assert` won't trigger—regardless of whether an lvalue or rvalue is passed. Only when the compiler needs to instantiate this template at a specific call site does the concrete type of `Self` get determined, and `static_assert` get evaluated. This is called "lazy instantiation," a very common pattern in template metaprogramming.

#### Comparison with Chromium's Approach

Chromium doesn't have the luxury of C++23. It uses two overloads: `Run() &&` is the actual execution version, while `Run() const&` contains a `static_assert(!sizeof(*this), "...")` to produce a compilation error. The `!sizeof` hack exploits a property of C++: `sizeof` can only be evaluated on a complete type, so when `!sizeof(*this)` is evaluated, it must be inside the class definition (the type of `*this` is complete), and the expression's value is guaranteed to be `false`. Before C++23, writing `static_assert(false, "...")` directly would trigger on all code paths (even if this overload was never called), so Chromium had to resort to the `!sizeof` trick. C++23 relaxed this restriction, but Chromium's codebase hasn't fully migrated to C++23 yet, so it still retains the old approach.

Our deducing this solution requires only a single function template, naturally distinguishing between lvalues and rvalues through the deduction of `Self`. It is much cleaner than Chromium's two overloads plus the `!sizeof` hack.

### The requires Constraint on the Constructor

There is a seemingly redundant constraint on the constructor template:

```cpp
template<typename Functor>
    requires not_the_same_t<Functor, OnceCallback>
explicit OnceCallback(Functor&& f);
```

Why not just leave it as `template<typename Functor>` and call it a day? The problem lies in the competition between the template constructor and the move constructor.

When we write `OnceCallback cb2 = std::move(cb1)`, the compiler has two paths before it: call the implicitly declared move constructor `OnceCallback(OnceCallback&&)`, or instantiate the template constructor as `OnceCallback(OnceCallback&&)` (letting `Functor = OnceCallback`). Intuitively, we might feel that the move constructor is a "more specific" match and should be preferred. But C++ overload resolution rules don't work that way—in some cases, a function signature instantiated from a template is a "more exact" match than an implicitly declared special member function, and the compiler will choose the template version without hesitation. This can lead to unexpected behavior, such as the template constructor potentially not correctly setting the source object's state to kEmpty.

Our implementation uses a custom concept `not_the_same_t` to solve this problem: `!std::is_same_v<std::decay_t<F>, T>` means "exclude this template when the decayed type of `F` is exactly `T` itself." Decay plays a role here by stripping references and cv-qualifiers from `F`—because `F` could be `OnceCallback&&` or `const OnceCallback&`, both of which decay to `OnceCallback`. With this constraint, when `OnceCallback` itself is passed in, the template is excluded, and the compiler correctly matches the move constructor.

This technique is very common when implementing move-only type-erased wrappers—`std::move_only_function`'s own implementation has a similar constraint. If you write similar components in the future, remember this pattern: **template constructor + requires excluding the type itself = protecting the correct matching of move semantics**.

### Internal Implementation Ideas for the Consume Semantics

The implementation logic of `impl_run` is very intuitive—check the state, handle cancellation, invoke the callable object, and update the state. There are a few details worth mentioning.

The first is that the cancellation check happens before execution. `impl_run` first checks whether the token is valid—if it has been canceled, it directly consumes the callback without executing it. For void returns, it simply returns; for non-void returns, it throws an `std::bad_function_call`. This exception-throwing behavior might seem aggressive, but the reasoning is sound: the caller expects a return value, but we cannot provide a meaningful one, so throwing an exception is safer than returning an undefined value.

The second is the `if constexpr (std::is_void_v<ReturnType>)` branch. When the return type is `void`, we cannot write `ReturnType result = func_(args...)`—void is not a type that can be assigned to. `if constexpr` selects the branch at compile time: the void case takes the "invoke but don't assign" path, while the non-void case takes the "invoke and assign to result" path. This is the standard pattern for `if constexpr` to handle void return types.

The third is nullifying after consumption. `impl_run` first moves `func_` out as a local variable, then sets `func_` to `nullptr` and `status_` to kConsumed, and finally executes the callable object in the local variable. This order is critical—first extract the callable object and mark the state, then execute. This way, even if the callable object throws an exception internally, `status_` is already kConsumed, and the callback won't be left in an inconsistent state. The nullification step isn't just about marking state—it triggers `std::move_only_function` to destruct the internally held callable object, releasing resources captured by the lambda (such as `unique_ptr`).

### Verifying the Core Skeleton

Once the skeleton is written, quickly verifying a few scenarios is sufficient: basic type return, void return, move-only capture, and move semantics. If all four scenarios pass—constructing a callback yields the correct return value, void callbacks execute normally, resources captured by `unique_ptr` are released after the callback is used, the source object becomes empty after a move, and the target object is valid—the skeleton is sound. We will organize the complete test cases uniformly in Part 3.

---

## Step 2: Argument Binding — `bind_once()`

### What Problem Are We Solving

The scenario for `bind_once` is very intuitive: you have a three-argument function `f(int, int, int)`, but the first two arguments can be determined at binding time (for example, 10 and 20), and only the third argument needs to be passed in at call time. You want to get a `OnceCallback<int(int)>` that only takes one argument, which automatically combines 10, 20, and the argument you pass in, feeding them all to the original function.

This is argument binding—stuffing the "known arguments" into the callback in advance, so the caller only needs to worry about the "unknown arguments." Chromium's `BindOnce` does a lot of heavy lifting in this area to handle the lifetimes of arguments (`Unretained`, `Owned`, `Passed`, `WeakPtr`, etc.), but our simplified version only focuses on the core argument binding logic.

### The Implementation Skeleton of `bind_once`

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

This code isn't long, but it contains several template techniques worth expanding on. Let's break them down one by one.

### Lambda Capture Pack Expansion

The line `...bound = std::forward<BoundArgs>(args)` is the **lambda init-capture pack expansion** syntax introduced in C++20. It is the key to enabling the concise implementation of `bind_once`.

Before C++20, a parameter pack from a variadic template could not be directly expanded into a lambda's capture list—you couldn't write code that "captures each element of `args...` into the lambda separately." The workaround was to use a `std::tuple` to bundle all bound arguments, then use `std::apply` inside the lambda to expand them into separate arguments for the call. This approach worked, but the code bloated significantly—you needed an extra tuple, an `std::apply` call, and template helper code to handle the move semantics of tuple elements.

C++20 finally allowed pack expansion into lambda captures. Specifically, the effect of `...bound = std::forward<BoundArgs>(args)` is to generate a corresponding captured variable for each type in `BoundArgs...`, with each variable perfectly forward-initialized using `std::forward`. As a concrete example, assuming `BoundArgs...` is `int, std::string`, the expansion is equivalent to:

```cpp
[b1 = std::forward<int>(arg1), b2 = std::forward<std::string>(arg2)]
```

Each captured variable can be used independently inside the lambda. In our `bind_once`, they are expanded together via `std::move(bound)...` when the lambda is called, and passed to `std::invoke`. Note that we use `std::move` here instead of `std::forward`—because the lambda belongs to `mutable`, the captured variables are lvalues inside the lambda, and we want to pass them as rvalues to trigger move semantics.

### The Unified Invocation Capability of `std::invoke`

Inside the lambda, we use `std::invoke` instead of directly calling `f(...)` because `std::invoke` can uniformly handle various callable objects. Calling a regular function pointer directly is fine, but member function pointers are different—you can't write `(&Class::method)(obj, args...)`, you must use the special syntax `(obj.*method)(args...)`. `std::invoke` encapsulates all these differences: `std::invoke(&Class::method, &obj, args...)` is equivalent to `(obj.*method)(args...)`.

This means `bind_once` naturally supports member function binding without extra code:

```cpp
struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

Calculator calc;
auto bound = bind_once<int(int)>(&Calculator::multiply, &calc, 5);
int r = std::move(bound).run(8);  // r == 40
```

However, there is a **lifetime trap** to be aware of here: `&calc` is a raw pointer, and `bind_once` does not manage its lifetime. If `calc` is destroyed before the callback is invoked, `std::invoke` will access freed memory through a dangling pointer. Chromium uses `base::Unretained` to explicitly mark "I know this raw pointer's lifetime is safe," uses `base::Owned` to take ownership, and uses `base::WeakPtr` to automatically cancel the callback when the object is destructed. In our simplified version, this safety responsibility is temporarily left to the caller.

### Signature Deduction: Why We Need to Explicitly Specify `Signature`

You may have noticed that the first template parameter `Signature` of `bind_once` (for example, `int(int)`) needs to be explicitly specified by the caller. Ideally, the compiler should be able to automatically deduce the "remaining signature after removing the bound arguments" from the callable signature of `F`. But in C++, this is much more complex than one might imagine.

For a function pointer `R(*)(Args...)`, you can extract the parameter list via template partial specialization, and then use a compile-time "type list slicing" operation to remove the first N types. For functors with a determined signature, you can also extract the signature via `decltype(&T::operator())`. But for a **generic lambda** (`[](auto x) { ... }`), its `operator()` is itself a template—there is no uniquely determined signature, and the compiler simply cannot obtain "what arguments this lambda accepts" information at the type level.

Chromium wrote an entire suite of type manipulation utilities (`MakeUnboundRunType`, `DropTypeListItem`, etc.)—roughly hundreds of lines of template metaprogramming code to handle various edge cases. For our educational purposes, having the caller write one extra template parameter `int(int)` is the more pragmatic choice—it saves a massive amount of complex template metaprogramming and yields better code clarity.

---

## Step 3: Cancellation Checks — `is_cancelled()` and `maybe_valid()`

### The Concept of Cancellation Tokens

A callback can be associated with a "cancellation token" at creation time. The token represents the lifetime of some external object—when that object is destroyed, the token becomes invalid, and all callbacks associated via that token transition to a "canceled" state.

You can think of it as a "pass": when creating the callback, a pass is issued to it that says "valid." At some point, the external object says "this pass is revoked" (by calling `invalidate()`), and after that, all callbacks holding this pass will find "the pass is already invalid" when checked before execution, and skip execution. In Chromium, this pass is the control block inside `WeakPtr`—after the object pointed to by `WeakPtr` is destroyed, the flag in the control block is cleared, and all callbacks bound to this `WeakPtr` are automatically canceled.

### The Design Rationale for `CancelableToken`

Our simplified cancellation token only needs three core operations: create (generate a valid token), invalidate (mark as revoked), and check (query whether it is still valid). Internally, we use a `shared_ptr` to manage a `Flag` struct containing a `atomic<bool>`:

```cpp
class CancelableToken {
    struct Flag {
        std::atomic<bool> valid{true};  // 原子变量，多线程安全
    };
    // 所有 token 副本共享同一个 Flag
    std::shared_ptr<Flag> flag_;

public:
    CancelableToken() : flag_(std::make_shared<Flag>()) {}
    void invalidate() { flag_->valid.store(false, std::memory_order_release); }
    bool is_valid() const {
        return flag_->valid.load(std::memory_order_acquire);
    }
};
```

The reason for using `shared_ptr` instead of a raw pointer is to allow the token to be copied and moved, while all copies share the same `Flag`. `atomic<bool>` guarantees the safety of multi-threaded access—one thread might be executing `is_valid()` while another is calling `invalidate()`, and `memory_order_acquire/release` semantics guarantee that the former's read will definitely see the latter's write.

### Integrating into `OnceCallback`

Integrating the cancellation token into `OnceCallback` is very straightforward: add an optional `shared_ptr<CancelableToken>` to the data members, set it via the `set_token()` method, and then check it in two places—when `is_cancelled()` is queried, and before `impl_run()` executes.

The logic of `is_cancelled()` is: if the state is not kValid, return true (both empty and consumed callbacks count as "canceled"), and if there is a token and the token is invalid, also return true. Inside `impl_run`, before actually executing the callable object, we first check the token's state—if it is canceled, we consume the callback without executing it, directly returning (for the void case) or throwing an `std::bad_function_call` (for cases requiring a return value).

`maybe_valid()` is temporarily just a simple wrapper around `!is_cancelled()`. In Chromium's full implementation, the difference between the two lies in the strength of thread safety guarantees—`is_cancelled()` can only be called on the sequence where the callback is bound (i.e., the thread that created the callback), guaranteeing a deterministic result; `maybe_valid()` can be called from any thread, but the result might be stale. Our simplified version doesn't distinguish this semantics for now, but we retain both method names for future extension in `RepeatingCallback` or cross-thread scenarios.

---

## Step 4: Chained Composition — `then()`

### The Semantics of `then()`

`then()` allows us to chain two callbacks together into a pipeline. The semantics are very intuitive: when the pipeline is called, it first executes the first callback with the original arguments, then passes the return value to the second callback. For example, callback A computes `3 + 4 = 7`, and callback B computes `7 * 2 = 14`. After chaining them with `then()`, you get a new callback that automatically walks through the entire A → B flow when invoked.

It sounds simple, but `then()` is the most ingeniously designed of the four features in terms of ownership.

### Ownership is Key

The new chained callback needs to hold the **ownership** of both the original callback and the subsequent callback—otherwise, the original callback might be consumed prematurely on the outside, breaking the pipeline. Since `OnceCallback` is move-only, this means `then()` must consume `*this` (the original callback) and `next` (the subsequent callback), transferring both of their ownerships into a new lambda closure. The entire ownership chain looks like this:

```text
新回调 → move_only_function → lambda 闭包 → [原回调 + 后续回调]
```

The skeleton of the implementation approach looks roughly like this:

```cpp
template<typename Next>
auto then(Next&& next) &&       // 末尾的 && 使其成为右值限定成员函数
    -> OnceCallback</* 返回类型和签名待推导 */>
{
    return OnceCallback</* ... */>(
        [self = std::move(*this),             // 把整个原回调移进 lambda
         cont = std::forward<Next>(next)]     // 把后续回调也移进来
        (FuncArgs... args) mutable -> decltype(auto) {
            if constexpr (std::is_void_v<ReturnType>) {
                std::move(self).run(std::forward<FuncArgs>(args)...);
                return std::invoke(std::move(cont));     // void → 无参数传递
            } else {
                auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
                return std::invoke(std::move(cont), std::move(mid));  // 传递中间结果
            }
        }
    );
}
```

Note an important difference here from the original Chromium version: we use `std::invoke` for the subsequent callback instead of `.run()`. This is because the `next` parameter accepted by `then()` is a regular callable object (like a lambda), not a `OnceCallback`—the caller doesn't need to explicitly write `std::move(cont).run()`, `std::invoke` can just be called directly. Only `self` (the original callback) needs `std::move(...).run()` to express consume semantics.

### A Few Easy-to-Miss Pitfalls

**First, the `&&` qualifier.** The `&&` at the end of the function declaration makes it an rvalue-qualified member function, which can only be called through an `std::move(cb).then(next)` or a temporary object `.then(next)`. This is another way to express "consume semantics"—unlike `run()` which uses deducing this, `then()` directly uses the traditional ref-qualifier. Why not use deducing this? Because `then()` doesn't need to distinguish between lvalues and rvalues to give different error messages—it simply only accepts rvalues, with no middle ground.

**Second, `self = std::move(*this)`.** This line moves **everything** in the current `OnceCallback` object into the lambda's closure object. After the move, the current object enters a consumed state (because we don't set it to kEmpty, but let it naturally remain in a "moved-from" state). The closure object is then stored in the `move_only_function` of the returned new `OnceCallback`—the type-erasure capability of `move_only_function` guarantees that no matter what the lambda's actual type is, it can be stored uniformly.

**Third, the `mutable` keyword cannot be omitted.** The `operator()` generated by a lambda by default is `const`—meaning the lambda cannot modify captured variables internally. But we need to call `std::move(self).run()` on `self` inside the lambda, an operation that modifies the object's state (changing status from kValid to kConsumed). Therefore, the lambda must be declared as `mutable`, making `operator()` non-const.

**Fourth, `if constexpr (std::is_void_v<ReturnType>)`.** Just like the situation in `impl_run`—when the original callback returns `void`, the semantics of `then()` are "execute the original callback first, then execute the subsequent callback (with no argument passing)." `if constexpr` selects the branch at compile time, generating completely different code paths for the two cases.

### Multi-Level Pipelines

`then()` can be called in a chain, forming multi-level pipelines:

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

Each `then()` call creates a new `once_callback`, internally capturing the callback from the previous step in a nested fashion. The call order from outside to inside unfolds recursively: the outermost callback is `run()` → its lambda executes → inside the lambda, `std::move(self).run()` is called on the previous level → then called on the level above that → all the way down to the base level. Performance-wise, each level of `then()` adds one level of `std::move_only_function` indirection, which is completely acceptable for 2-3 level pipelines. If the pipeline is very deep (over 10 levels), you could consider using a `std::variant` to create a flattened pipeline structure to avoid the overhead of nested closures—but that is beyond our current scope of discussion.

---

## Summary

In this article, we completed a design walkthrough of the four core features of `OnceCallback`. Unlike the interface design in Part 1, the focus here was on understanding "why it's written this way" and "what the key template techniques are." Let's review a few core knowledge points:

- **Template partial specialization** `OnceCallback<R(Args...)>` lets users specify the callback type using natural function signature syntax, with the compiler decomposing the function type into a return type and parameter pack through pattern matching
- **Deducing this** enables `run()` to achieve compile-time lvalue/rvalue interception through a single function template, which is cleaner than Chromium's dual overloads + `!sizeof` hack
- **The `requires` constraint** (via the `not_the_same_t` concept) resolves the matching conflict between the template constructor and the move constructor, and is a standard defensive measure for move-only type-erased wrappers
- **Lambda capture pack expansion** is the key to the concise implementation of `bind_once`; before C++20, a workaround using tuple + apply was required
- **The core challenge of `then()`** is ownership management—it guarantees the integrity of the ownership chain for each callback in the pipeline through rvalue qualification + lambda capture move, and uses `std::invoke` to uniformly invoke the subsequent callback

In the next article, we will use systematic test cases to verify these designs and compare our performance trade-offs with the original Chromium version.

## References

- [Chromium callback.h source code](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [Chromium bind_internal.h source code](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/bind_internal.h)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [P0847R7 - Deducing this proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
- [P0780R2 - Pack Expansion in Lambda Capture](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0780r2.html)
