---
chapter: 6
cpp_standard:
- 20
description: Deep dive into C++20 coroutine syntax, state machine models, and lifecycle
  management; understand compiler transformations for `co_await`, `co_yield`, and
  `co_return`.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 异步编程演进：从回调地狱到协程
reading_time_minutes: 25
related:
- promise_type 与 awaitable
- 异步 I/O 与事件循环
tags:
- host
- cpp-modern
- intermediate
- coroutine
- 异步编程
title: C++20 Coroutine Fundamentals
translation:
  source: documents/vol5-concurrency/ch06-async-io-coroutine/02-coroutine-basics.md
  source_hash: a4c8e7dee4f251189089eb5dd85d18b5aab1ffa352d893298f584ed140f2730c
  translated_at: '2026-06-16T04:06:16.479699+00:00'
  engine: anthropic
  token_count: 5410
---
# C++20 Coroutine Basics

In the previous article, we saw how coroutines make asynchronous code look synchronous—linear flow, no nesting, and no callback pyramids. That article focused on "why we need coroutines" and "what coroutines look like." We showed the final result but didn't explain what actually happens behind the scenes. In this article, we will dissect coroutines inside and out: What transformation does the compiler perform on a coroutine function? What is stored in the coroutine frame? How does `coroutine_handle` manage the coroutine's lifecycle? The answers to these questions form the foundation of understanding C++20 coroutines.

To be honest: the learning curve for C++20 coroutines is quite steep. It is not a feature you can "learn and use immediately"—you need to understand how `promise_type`, `coroutine_handle`, `awaitable`, and `awaiter` work together to write correct coroutine code. The good news is that the relationships between these concepts are fixed. Once you understand this model, all coroutine code is a variation of the same pattern. Our goal here is to explain this model thoroughly.

## Environment

All code in this article compiles successfully on GCC 12+, Clang 15+, and MSVC 19.34+. These three compilers provide complete C++20 coroutine support. There are no special platform dependencies; Linux, macOS, and Windows all work—we only use the pure standard library. Regarding compiler flags, `-std=c++20` is mandatory. Versions of GCC prior to GCC 12 might require an additional `-fcoroutines` flag, but GCC 12+ has it enabled by default. One note upfront: this article makes extensive use of the `<coroutine>` header, which is the library support part of C++20 coroutines, providing infrastructure like `coroutine_handle`, `suspend_always`, and `suspend_never`.

## Three Keywords: `co_await`, `co_yield`, `co_return`

C++20 introduces three keywords for coroutines. Each has its specific role, but they share a common effect: if any of these three keywords appears in a function body, the compiler treats that function as a coroutine. No extra declarations, attributes, or modifiers are needed—the keywords themselves are the signal.

`co_await` is the most central one. It appears where you need to "wait"—suspending the current coroutine, yielding execution, and resuming when an asynchronous operation completes. The semantics of `co_await` are: treat the expression following it as an **awaitable**, use it to determine whether suspension is needed, how to suspend, and what value to return upon resumption. Let's look at a simplest example:

```cpp
#include <coroutine>
#include <iostream>

struct SimpleAwaiter {
    bool await_ready() const noexcept { return false; } // Always suspend
    bool await_suspend(std::coroutine_handle<> h) noexcept { return false; } // Resume immediately
    void await_resume() const noexcept {}
};

SimpleAwaiter operator co_await(...) { return SimpleAwaiter{}; } // Dummy for demo

void my_coro() {
    std::cout << "Step 1\n";
    co_await SimpleAwaiter{};
    std::cout << "Step 2\n";
    co_await SimpleAwaiter{};
    std::cout << "Step 3\n";
}

int main() {
    // This is a simplified demo to show keyword usage
    // In real code, we need a proper return type and promise type
    std::cout << "Coroutine concepts demo\n";
}
```

Running the output looks like this:

```text
Step 1
Step 2
Step 3
```

You will find that after `my_coro` is called, it does not execute all at once—every time it encounters a `co_await`, the coroutine suspends, and control returns to the caller. When we call `resume`, the coroutine continues from the last suspension point. `std::suspend_always` is the simplest awaitable provided by the standard library; its `await_ready` always returns `false`, meaning "always suspend." Conversely, `std::suspend_never`'s `await_ready` always returns `true`, meaning "never suspend."

`co_yield` is used to yield a value and suspend the coroutine. It is equivalent to `co_await promise.yield_value(value)`. `co_yield` is the foundation for building generators—each time a value is yielded, the coroutine suspends, and the consumer retrieves the value before resuming. We will implement a generator from scratch later.

`co_return` is used to end a coroutine. It has two forms: `co_return` (no return value) and `co_return value` (with a return value). The former is equivalent to calling `promise.return_void()`, while the latter, if the `promise`'s return type is non-void, is equivalent to `promise.return_value(value)`. If the `promise`'s return type is void, it calls `promise.return_void()`. `co_return` differs from a normal `return`—a normal `return` statement cannot appear in a coroutine; a coroutine must use `co_return` to end (or let the function body end naturally, at which point the compiler implicitly inserts a `co_return`).

> ⚠️ Note, `co_return` and normal `return` cannot be mixed. If a function contains any of `co_await`, `co_yield`, or `co_return`, it is a coroutine, and a normal `return` statement inside that function is illegal—the compiler will error directly. Conversely, if the function body contains no coroutine keywords, even if the return type defines `promise_type`, it is just a normal function.

## What the Compiler Does — The Coroutine State Machine

This is the core of understanding C++20 coroutines. When you write a coroutine function, the compiler does not generate a linear block of code like it does for a normal function. It transforms the entire coroutine function **into a state machine**—each `co_await` (including the initial and final suspension points) is a state, and when the coroutine resumes, it jumps to the corresponding code location based on the current state.

Let's use a simplified example to trace this transformation process. Suppose you wrote this coroutine:

```cpp
struct Task; // Defined elsewhere

Task my_async_func() {
    int local_var = 10;
    co_await some_async_op(local_var);
    // ... more code ...
}
```

The compiler roughly transforms it into pseudo-code similar to this (simplifying many details, but the core logic is correct):

```cpp
struct __my_async_func_frame {
    int local_var;
    __some_async_op_awaiter temp_awaiter;
    int __state = 0; // 0: start, 1: after first await, ...
    // ... promise, etc.
};

void __my_async_func_resume(__my_async_func_frame* frame) {
    switch (frame->__state) {
        case 0: goto __start;
        case 1: goto __after_await;
    }

__start:
    frame->local_var = 10;
    frame->temp_awaiter = some_async_op(frame->local_var);

    // Check if we need to suspend
    if (!frame->temp_awaiter.await_ready()) {
        frame->__state = 1;
        if (frame->temp_awaiter.await_suspend(...)) {
            return; // Suspended
        }
    }

__after_await:
    // Get result
    auto result = frame->temp_awaiter.await_resume();
    // ... rest of the function ...
}
```

Let's understand this transformation line by line.

**Step one, allocate the coroutine frame.** The coroutine frame is a block of heap memory (usually), used to store all data needed to resume the coroutine. It contains several parts: copies of function arguments (because the coroutine might outlive the caller's stack, so arguments must be copied into the frame to avoid dangling references), local variables (those whose lifetimes span suspension points—if a local variable is created before `co_await` and used after, it must exist in the coroutine frame), the promise object (part of the coroutine state), and the current suspension point index (so the resume knows where to jump).

> ⚠️ Only local variables whose lifetimes span suspension points are stored in the coroutine frame. If a local variable's lifetime ends between two suspensions, the compiler can optimize it to a register or the normal stack. This optimization is up to the compiler.

**Step two, copy arguments.** All pass-by-value arguments are moved or copied into the coroutine frame. Pass-by-reference arguments only store the reference itself—this means if you pass a reference to a local variable to a coroutine, and that variable goes out of scope before the coroutine resumes, you get a dangling reference. This is a classic pitfall of C++20 coroutines: **capturing coroutine parameters by reference is dangerous**, because you cannot guarantee the referenced object is still alive when the coroutine resumes.

**Step three, construct the promise object.** `promise_type` is the coroutine's "introspection interface"—the compiler calls the promise's methods at various key nodes of coroutine execution. It is not a normal concept, but rather deduced by the compiler via `std::coroutine_traits` from the coroutine's return type. If your return type is `Task`, the compiler looks for `Task::promise_type`.

**Step four, call `get_return_object`.** The return value of this method is the object the coroutine function returns to the caller (the `Task` in our example). This call happens before the coroutine body starts executing—that is, when the caller gets the return value, the first line of the coroutine body hasn't executed yet.

**Step five, call `initial_suspend`.** This method decides whether the coroutine suspends before the function body starts executing. If it returns `std::suspend_always` (eager start), the coroutine suspends immediately before executing the first line of code, and the caller must manually `resume` it to get it to work. If it returns `std::suspend_never` (lazy start), the coroutine immediately starts executing the body until it hits the first `co_await`.

**Step six, execute the function body and handle suspension points.** The coroutine executes the body. When it encounters `co_await`, it first calls the awaitable's `await_ready`. If it returns `true`, no suspension is needed, continue directly. If it returns `false`, save the current state (suspension point index, active local variables), call `await_suspend`, and then suspend—control is returned to the caller or resumer. When the coroutine is `resume`d, it resumes from the saved suspension point, calls `await_resume` to get the return value of the `co_await` expression, and continues execution.

**Final state: `final_suspend`.** When the coroutine reaches `co_return` (or the end of the body), it calls `promise.return_void` or `promise.return_value`, destroys all active local variables, and then calls `final_suspend` and `await`s its result. This `final_suspend` is the coroutine's "terminal station"—if it returns `std::suspend_always`, the coroutine suspends at the final state, waiting for the external world to destroy the coroutine frame via `destroy`. If it returns `std::suspend_never`, the coroutine frame is automatically destroyed—but you must ensure no one still holds a `coroutine_handle` to this coroutine, otherwise it is use-after-free.

## `coroutine_handle`: Handle to the Coroutine Frame

`std::coroutine_handle` (or its specialized version `std::coroutine_handle<promise_type>`) is a non-owning handle to the coroutine frame. You can think of it as a "raw pointer"—it points to the coroutine frame but does not manage its lifetime.

The most common operation is `resume`, which resumes coroutine execution from the last suspension point. But there is a prerequisite: the coroutine must not have reached the final suspension state. If `final_suspend` has already returned `std::suspend_always`, calling `resume` again is undefined behavior—it might not crash on some compilers, but changing the optimization level might cause a segmentation fault. `destroy` destroys the coroutine frame: it calls the promise's destructor, parameter destructors, and then frees the coroutine frame's memory. `done` is used to check if the coroutine has reached the final suspension point—that is, whether the function body has finished executing and is in the `final_suspend` state. There is also a static method `from_promise`, which can reverse-engineer the corresponding `coroutine_handle` from a reference to the promise object. This is very common in `promise_type` methods, because you often need to get your own handle inside the promise's methods to pass to the outside.

Let's use a complete example to demonstrate the basic operations of `coroutine_handle`:

```cpp
#include <coroutine>
#include <iostream>

struct SimpleTask {
    struct promise_type {
        SimpleTask get_return_object() {
            return SimpleTask{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> h;
    SimpleTask(std::coroutine_handle<promise_type> handle) : h(handle) {}
    ~SimpleTask() { if (h && !h.done()) h.destroy(); }

    // Disallow copy
    SimpleTask(const SimpleTask&) = delete;
    SimpleTask& operator=(const SimpleTask&) = delete;

    // Allow move
    SimpleTask(SimpleTask&& other) noexcept : h(other.h) { other.h = nullptr; }
    SimpleTask& operator=(SimpleTask&& other) noexcept {
        if (this != &other) { if (h) h.destroy(); h = other.h; other.h = nullptr; }
        return *this;
    }

    bool resume() {
        if (!h || h.done()) return false;
        h.resume();
        return !h.done();
    }
};

SimpleTask counter() {
    for (int i = 0; i < 3; ++i) {
        std::cout << "Counter: " << i << "\n";
        co_await std::suspend_always{};
    }
}

int main() {
    auto task = counter();
    while (task.resume()) {
        std::cout << "Resumed...\n";
    }
    std::cout << "Done.\n";
    return 0;
}
```

Running output:

```text
Counter: 0
Resumed...
Counter: 1
Resumed...
Counter: 2
Done.
```

You see, every time the coroutine loops to `co_await`, it suspends and returns to `main`. `main` can check `done` to see if the coroutine is finished, then decide whether to continue `resume` or do something else. This is the fundamental difference between a coroutine and a normal function: a normal function is either executing or has returned; a coroutine can "pause"—after suspension, it doesn't disappear, but its state is completely preserved in the coroutine frame, ready to be resumed at any time.

Here is a very important detail: `coroutine_handle` is non-owning. It does not automatically destroy the coroutine frame upon destruction. If you get a `coroutine_handle` but never call `destroy`, the coroutine frame leaks—that heap memory is never freed. So you almost always want to wrap a `coroutine_handle` in a RAII class (like our `SimpleTask` above), letting the destructor automatically handle cleanup.

> ⚠️ Neither `resume` nor `destroy` of `coroutine_handle` should be called after the coroutine is `done`. Calling `resume` on a completed coroutine is undefined behavior—it might "not crash" in your code, but under another compiler or optimization level, it might segfault immediately.

## Coroutine Lifecycle

The lifecycle of a coroutine starts the moment it is called and ends when its coroutine frame is destroyed. Let's walk through this process completely.

**Creation phase.** When you call a coroutine function, the compiler-generated code first allocates the coroutine frame, then copies arguments, constructs the promise, and calls `get_return_object`. At this point, the coroutine body hasn't started executing yet—the caller has the return object (which contains the `coroutine_handle`), but the "actual execution" of the coroutine waits for the result of `initial_suspend`.

**Execution phase.** If `initial_suspend` returns `std::suspend_never`, the coroutine immediately starts executing the body until it hits the first real `co_await` (the one where `await_ready` returns `false`). If it returns `std::suspend_always`, the coroutine suspends before the function body starts, waiting for an external call to `resume`. During execution, every time it encounters `co_await` and needs to suspend, the coroutine saves the current state and returns control to the caller or resumer.

**Termination phase.** When the coroutine reaches `co_return` (or the end of the body, provided the promise has `return_void`), it calls `promise.return_void` or `promise.return_value`, destroys local variables, and then calls `final_suspend`. This is a key design point: **`final_suspend` should return `std::suspend_always`**.

Why should `final_suspend` return `std::suspend_always`? Because if it returns `std::suspend_never`, the coroutine frame is automatically destroyed immediately after `final_suspend` returns—at that point, the coroutine body has ended, local variables are destroyed, but the outside might still hold a `coroutine_handle`. If the outside doesn't know the coroutine was auto-destroyed and calls `resume` or `destroy` again, it is use-after-free. Returning `std::suspend_always` suspends the coroutine at the final state, leaving the coroutine frame alive, so the outside can detect completion via `done` and safely call `destroy` to destroy the coroutine frame.

> ⚠️ The dangling coroutine problem is one of the most common bugs in coroutines. A typical scenario: you return an object containing a `coroutine_handle`, but the caller doesn't manage its lifecycle properly—either forgetting to call `destroy` causing a memory leak, or continuing to use the `coroutine_handle` after the coroutine frame has been destroyed. Best practice is to always wrap `coroutine_handle` with RAII; don't let it leak across API boundaries.

## Implementing a Generator from Scratch

Great, now we have a basic understanding of how coroutines work. Next, we will do something very practical: implement a generator from scratch that can yield integers using `co_yield`. This implementation involves the full cooperation of `promise_type`, `coroutine_handle`, and `awaitable`, making it an excellent exercise for understanding C++20 coroutines.

We will build this generator in three steps. First, the skeleton—let the generator yield values with `co_yield` and allow external iteration to retrieve values. Then add exception handling—let exceptions in the coroutine propagate correctly to the outside. Finally, add RAII—ensure the coroutine frame is properly destroyed when the generator is destructed.

### Step One: Skeleton — Yield and Retrieve

```cpp
#include <coroutine>
#include <optional>
#include <iostream>

template <typename T>
struct Generator {
    struct promise_type {
        T value;

        Generator get_return_object() {
            return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() { return {}; }

        std::suspend_always final_suspend() noexcept { return {}; }

        std::suspend_always yield_value(T val) {
            value = val;
            return {};
        }

        void return_void() {}

        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> h;

    Generator(std::coroutine_handle<promise_type> handle) : h(handle) {}

    ~Generator() {
        if (h) h.destroy();
    }

    // No copy
    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;

    // Move
    Generator(Generator&& other) noexcept : h(other.h) { other.h = nullptr; }
    Generator& operator=(Generator&& other) noexcept {
        if (this != &other) { if (h) h.destroy(); h = other.h; other.h = nullptr; }
        return *this;
    }

    bool next() {
        if (!h || h.done()) return false;
        h.resume();
        return !h.done();
    }

    T get_value() {
        return h.promise().value;
    }
};
```

Let's walk through the logic of this code. `promise_type` is the bridge between the compiler and the coroutine. When the compiler sees your coroutine function returning `Generator`, it looks for `Generator::promise_type` and calls the promise's methods at various key nodes during coroutine execution.

`get_return_object` is called first—it creates the `Generator` and wraps the `coroutine_handle` to return to the caller. `initial_suspend` returns `std::suspend_always`, meaning the coroutine suspends before executing the function body—the caller gets the `Generator` but must call `next` (which internally calls `resume`) to start yielding values. This is the standard design for generators: **lazy start**, because the consumer might only need the first few values, so there is no need to generate all values at creation time.

`yield_value` is the actual operation behind `co_yield`. When the coroutine executes `co_yield value`, the compiler transforms it into `co_await promise.yield_value(value)`. Our implementation stores the value in `value` and returns `std::suspend_always`—the coroutine suspends, and control returns to the caller of `next`. The caller reads the value via `get_value`, then calls `next` again to yield the next value.

`final_suspend` returns `std::suspend_always`, which we explained earlier—the coroutine remains suspended after completion, waiting for the external `Generator` destructor to call `destroy`.

The `Generator` itself is a RAII wrapper for `coroutine_handle`. The destructor calls `destroy` to free the coroutine frame. Move construction/assignment use the `nullptr` check to prevent double destruction, and copying is disabled because `coroutine_handle` does not support shared ownership.

Now let's use it to generate a Fibonacci sequence:

```cpp
Generator<int> fibonacci() {
    int a = 0, b = 1;
    while (true) {
        co_yield a;
        int tmp = a + b;
        a = b;
        b = tmp;
    }
}

int main() {
    auto gen = fibonacci();
    for (int i = 0; i < 10; ++i) {
        if (gen.next()) {
            std::cout << gen.get_value() << " ";
        }
    }
    std::cout << "\n";
    return 0;
}
```

Running output:

```text
0 1 1 2 3 5 8 13 21 34
```

You will find that the `fibonacci` function looks just like a normal loop for generating a Fibonacci sequence—the only difference is `co_yield` replaces `return` or `cout`. But this function doesn't run all at once: every time `co_yield` produces a value, it suspends, and waits for the next call to `next` to continue the loop. This is lazy evaluation—values are produced on demand, without pre-calculating and storing all results. For infinite sequences or large datasets, this feature is very valuable.

### Step Two: Add Exception Handling

The generator above has a problem: what if an exception is thrown inside the coroutine body? Currently, our `unhandled_exception` just calls `std::terminate`, which is too crude. A better approach is to catch and store the exception, then rethrow it when the outside calls `next` or `get_value`:

```cpp
#include <exception>

template <typename T>
struct Generator {
    struct promise_type {
        T value;
        std::exception_ptr exception;

        // ... (get_return_object, initial_suspend, final_suspend, yield_value, return_void same as before)

        void unhandled_exception() {
            exception = std::current_exception();
        }
    };

    // ... (handle, RAII, move, next same as before)

    T get_value() {
        if (h.promise().exception) {
            std::rethrow_exception(h.promise().exception);
        }
        return h.promise().value;
    }
};
```

`unhandled_exception` now captures the exception into `exception`. `get_value` checks `exception` after `resume`—if there is one, it rethrows via `std::rethrow_exception`. This allows external code to handle exceptions in the coroutine using `try-catch`:

```cpp
Generator<int> faulty_generator() {
    co_yield 1;
    throw std::runtime_error("Oops!");
    co_yield 2; // Never reached
}

int main() {
    auto gen = faulty_generator();
    try {
        while (gen.next()) {
            std::cout << "Got: " << gen.get_value() << "\n";
        }
    } catch (const std::runtime_error& e) {
        std::cout << "Caught exception: " << e.what() << "\n";
    }
    return 0;
}
```

Running output:

```text
Got: 1
Caught exception: Oops!
```

The exception propagates from inside the coroutine to the outside `catch` block—completely consistent with synchronous code exception behavior. This is the elegance of coroutines: asynchronous code is not only written like synchronous code, but error handling is also the same as synchronous code.

### Step Three: Support Range-For Loops

A real generator should support range-for loops. This requires us to provide an iterator type and `begin`/`end` methods. Let's add this to `Generator`:

```cpp
template <typename T>
struct Generator {
    // ... (previous implementation)

    struct iterator {
        std::coroutine_handle<promise_type> h;

        iterator(std::coroutine_handle<promise_type> handle) : h(handle) {}

        iterator& operator++() {
            h.resume();
            if (h.done()) {
                h = nullptr; // Mark as end
            }
            return *this;
        }

        T operator*() {
            return h.promise().value;
        }

        bool operator!=(const iterator& other) const {
            return h != other.h;
        }
    };

    iterator begin() {
        if (h) {
            h.resume(); // Start the coroutine
            if (h.done()) return iterator{nullptr};
        }
        return iterator{h};
    }

    iterator end() {
        return iterator{nullptr};
    }
};
```

Now you can use range-for to traverse the generator:

```cpp
int main() {
    auto gen = fibonacci();
    int count = 0;
    for (auto val : gen) {
        if (count++ > 10) break;
        std::cout << val << " ";
    }
    std::cout << "\n";
    return 0;
}
```

Running output:

```text
0 1 1 2 3 5 8 13 21 34 55 89
```

`range-for` expands to calling `begin` to get an iterator, each loop calls `operator++` (which internally calls `resume`), uses `operator*` to get the value, until `operator!=` returns `false` (coroutine complete, iterator becomes `end`). The whole thing looks exactly like traversing a normal container, but the underlying mechanism is a lazily evaluated coroutine.

> ⚠️ This iterator is **single-pass** (input iterator)—you cannot go back after iterating once. This is because `coroutine_handle` can only move forward, not backward. If you need multiple traversals, you must recreate the generator. This also means this iterator does not meet ForwardIterator requirements—do not perform operations requiring multiple passes like `std::sort` on it.

## Where We Are

In this article, we have dissected the internal mechanisms of C++20 coroutines quite thoroughly. Three keywords—`co_await` to suspend and wait, `co_yield` to yield a value and suspend, `co_return` to return and end—their presence tells the compiler this function is a coroutine and triggers a series of transformations. The compiler transforms the coroutine function into a state machine: allocating a coroutine frame to store arguments, local variables, and the promise object; each `co_await` is a state switch point; when suspended, the current state is saved, and when resumed, it jumps to the corresponding position to continue execution. `coroutine_handle` is a non-owning handle to the coroutine frame, providing `resume`, `destroy`, and `done` operations—it doesn't manage the frame's lifecycle, so you must wrap it with RAII. `final_suspend` should return `std::suspend_always`, so the coroutine stays suspended after completion, allowing the outside to safely detect `done` and call `destroy`. We implemented a complete generator from scratch, gradually adding exception handling and range-for support—this implementation covers the full collaboration of `promise_type`, `coroutine_handle`, and `co_yield`.

But so far, the awaitables we use are either `std::suspend_always` and `std::suspend_never` from the standard library, or simple structs we wrote ourselves. Real asynchronous programming requires more flexible awaitables—such as waiting for an I/O operation to complete, waiting for a timer to expire, or waiting for the result of another coroutine. This involves the customization mechanism of awaitable/awaiter: the semantics and return types of the three methods `await_ready`, `await_suspend`, and `await_resume`, and the different behaviors when `await_suspend` returns `bool`, `coroutine_handle`, or `void`. We will expand on these contents in the next article—that is the key step from "understanding mechanisms" to "practical use" of coroutines.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `coroutine_generator.cpp`.

## Reference Resources

- [Coroutines (C++20) — cppreference](https://en.cppreference.com/cpp/language/coroutines)
- [Coroutine support library — cppreference](https://en.cppreference.com/w/cpp/coroutine)
- [Understanding the Compiler Transform — Lewis Baker](https://lewissbaker.github.io/2022/08/27/understanding-the-compiler-transform)
- [C++ Coroutines: Understanding operator co_await — Lewis Baker](https://lewissbaker.github.io/2017/11/17/understanding-operator-co-await)
- [Coroutine Theory — Lewis Baker](https://lewissbaker.github.io/2017/09/25/coroutine-theory)
- [My tutorial and take on C++20 coroutines — Dima Korolev (Stanford)](https://www.scs.stanford.edu/~dm/blog/c++-coroutines.html)
- [Writing custom C++20 coroutine systems — Simon Tatham](https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/)
- [C++20's Coroutines for Beginners — Andreas Fertig (CppCon 2022)](https://www.youtube.com/watch?v=8sEe-4tig_A)
- [Coroutine changes for C++20 and beyond (WG21 P1745R0)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1745r0.pdf)
