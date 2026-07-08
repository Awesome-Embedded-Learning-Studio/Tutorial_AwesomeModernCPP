---
chapter: 1
cpp_standard:
- 23
description: "Line-by-line breakdown of then()'s ownership chain, from pipeline thinking to the void/non-void branches, to the most intricate ownership management in OnceCallback"
difficulty: beginner
order: 5
platform: host
prerequisites:
- OnceCallback in practice (II): the core skeleton
- OnceCallback prerequisites (II): std::invoke and the uniform call protocol
- OnceCallback prerequisites (III): advanced lambda features
reading_time_minutes: 7
related:
- OnceCallback in practice (VI): tests and performance comparison
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
- 模板
title: 'OnceCallback in practice (V): chaining with then'
---
# OnceCallback in practice (V): chaining with then

`then()` stitches two callbacks into one pipeline, feeding the first one's output into the second. It's the same old Unix pipe trick, and you've surely seen it before:

```bash
# Unix pipe: cmd1's output is cmd2's input
echo "hello" | tr 'h' 'H' | wc -c
```

Drop that onto callbacks and it's the same story: callback A's output goes to callback B.

```cpp
auto pipeline = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;          // step one: 3 + 4 = 7
}).then([](int sum) {
    return sum * 2;        // step two: 7 * 2 = 14
});

int result = std::move(pipeline).run(3, 4);  // result == 14
```

We started out thinking this was easy. `then()` just sews two callbacks together, right? But OnceCallback is move-only, so the original callback's full estate has to move into the new one. Miss `func_`, miss `token_`, miss `status_`, and you're sunk. This piece walks through `then()` line by line, with two things under the magnifying glass: how the ownership chain gets joined up section by section, and how the void and non-void return types fork into two branches.

## Ownership: the real problem in `then()`

If you've used Unix pipes, the semantics of `then()` are pretty intuitive:

```bash
# Unix pipe: cmd1's output is cmd2's input
echo "hello" | tr 'h' 'H' | wc -c
```

`then()` does the same thing: callback A's output becomes callback B's input. In code:

```cpp
auto pipeline = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;          // step one: 3 + 4 = 7
}).then([](int sum) {
    return sum * 2;        // step two: 7 * 2 = 14
});

int result = std::move(pipeline).run(3, 4);  // result == 14
```

`then()` chains two independent callbacks into a new one. Calling the new callback walks the whole A → B flow automatically.

---

The chained callback has to keep both the original and the continuation in its own hands. On a plain `std::function` that's no trouble, you just copy. But OnceCallback is move-only, and `func_`, `status_`, `token_` are none of them copyable. `then()` has to consume `*this` and `next`, moving the whole estate of both into a fresh lambda closure.

Drawn out, the ownership chain is a single line:

```mermaid
graph LR
    A["new OnceCallback"] --> B["move_only_function"] --> C["lambda closure"] --> D["original + continuation"]
```

Every section is move semantics passing the baton. No copying, no sharing. That line is the full shape of the move-only constraint inside `then()`.

---

## `then()` line by line

```cpp
template<typename ReturnType, typename... FuncArgs>
template<typename Next>
auto OnceCallback<ReturnType(FuncArgs...)>::then(Next&& next) && {
    using NextType = std::decay_t<Next>;

    if constexpr (std::is_void_v<ReturnType>) {
        using NextRet = std::invoke_result_t<NextType>;
        return OnceCallback<NextRet(FuncArgs...)>(
            [self = std::move(*this),
             cont = std::forward<Next>(next)]
            (FuncArgs... args) mutable -> NextRet {
                std::move(self).run(std::forward<FuncArgs>(args)...);
                return std::invoke(std::move(cont));
            });
    } else {
        using NextRet = std::invoke_result_t<NextType, ReturnType>;
        return OnceCallback<NextRet(FuncArgs...)>(
            [self = std::move(*this),
             cont = std::forward<Next>(next)]
            (FuncArgs... args) mutable -> NextRet {
                auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
                return std::invoke(std::move(cont), std::move(mid));
            });
    }
}
```

### Function signature: rvalue qualifier

```cpp
auto then(Next&& next) &&
```

That trailing `&&` makes it an rvalue-qualified member function, meaning `then()` only accepts `std::move(cb).then(next)` or `.then(next)` on a temporary. If someone slips up and writes `cb.then(next)` on an lvalue, the compiler fires back "no matching overloaded function" right there, and the error is even a clear one. This is a different route from `run()`, which goes through deducing this. `run()` has to give different error messages on lvalue versus rvalue calls, which is more work. `then()` doesn't need that distinction, so one ref-qualifier does the job. Clean.

### `std::decay_t<Next>`: strip the reference with decay

```cpp
using NextType = std::decay_t<Next>;
```

When `Next` comes in it might be `SomeLambda&&`, it might be `SomeLambda&`, and dragging a reference along makes the later type deduction awkward. `std::decay_t` peels the reference off and leaves the bare lambda type. After that, `std::invoke_result_t` queries the return against this `NextType`.

### The two branches of `if constexpr`

What actually forks `then()` is whether the original callback's return type is void. Once that knife comes down, the two sides look quite different.

When the original callback returns a value, the non-void branch, that value has to keep flowing into the continuation:

```cpp
using NextRet = std::invoke_result_t<NextType, ReturnType>;
```

`std::invoke_result_t<NextType, ReturnType>` asks, at compile time: if we hand a value of type `ReturnType` to a callable of type `NextType`, what type does it spit back? That's the new pipeline's outward return type. The work inside the lambda body is straightforward too. Run the original callback to get the intermediate result `mid`, then pass it straight to the continuation:

```cpp
auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
return std::invoke(std::move(cont), std::move(mid));
```

The void branch wears a different face. The original callback returns nothing, so naturally the continuation takes no argument:

```cpp
using NextRet = std::invoke_result_t<NextType>;
```

Here `std::invoke_result_t<NextType>` deduces "call `NextType` with an empty parameter list, what do you get." Inside the lambda it's two steps: run the original callback, throw the result away, then pull out the continuation and run it, also with no argument:

```cpp
std::move(self).run(std::forward<FuncArgs>(args)...);
return std::invoke(std::move(cont));
```

### Lambda capture: the heart of ownership

```cpp
[self = std::move(*this), cont = std::forward<Next>(next)]
```

`self = std::move(*this)` is the crux of the whole ownership chain. It moves the current OnceCallback's entire estate, `func_`, `status_`, `token_`, not one left behind, into the lambda's closure. After the move the current object is an emptied-out shell; `func_` and `token_` are no longer its. `cont = std::forward<Next>(next)` brings the continuation in too, and `std::forward` keeps `next`'s original value category: rvalue moves, lvalue copies.

This lambda finally gets handed to a fresh `OnceCallback<NextRet(FuncArgs...)>` constructor and stuffed into its `std::move_only_function`. Type erasure is what lets it be folded into the same shell no matter what the lambda actually looks like on the outside.

---

## Multi-stage pipelines

`then()` can keep chaining section by section into a multi-stage pipeline:

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
// 5 * 2 = 10, 10 + 10 = 20, to_string(20) = "20"
```

Every call to `then()` mints a new OnceCallback, with a closure inside that captures the previous step's callback. The moment the outermost `run()` fires, execution unrolls like a set of nested dolls: the outermost gets `run()` → its lambda runs → inside the lambda, `std::move(self).run()` hits the next layer up → and the next → all the way down to the bottom.

There's a cost. Each extra level of `then()` adds one `std::move_only_function` indirection. For two or three stages you can ignore it entirely. Push past ten and the nesting gets deep enough that you'd probably want a flattened pipeline structure, but that's well past the edge of what we're doing here, so we'll leave it for another day.

## A few spots that trip people up

### `mutable` is not optional

Inside the lambda we call `std::move(self).run()`, and that call genuinely mutates `self`'s state, flipping status from kValid to kConsumed. Without `mutable`, `self` is a const reference inside the lambda. The compiler will catch you tinkering with a const object every single time and refuse to compile.

### The state of `self = std::move(*this)`

After the move, the original OnceCallback's `func_` and `token_` have walked out the door and landed in "moved-from" territory. `status_` isn't explicitly reset to kEmpty, so its old value just hangs there. But with `func_` emptied, the shell is effectively dead, and anyone touching it is in undefined-behavior land. The saving grace is that `&&` qualifier on `then()` guards the door. The caller has no way to keep using the original object after `then()`.

### Why `std::invoke` instead of a direct call

`cont` is usually a lambda, so `cont(mid)` would run fine. But the day someone passes in a member function pointer as the continuation, direct-call syntax dies on the spot, and `std::invoke` doesn't. Going through `std::invoke` uniformly means that whatever weapon the other side brings, our setup catches it.

## References

- [Chromium callback.h source](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [cppreference: if constexpr](https://en.cppreference.com/w/cpp/language/if)
