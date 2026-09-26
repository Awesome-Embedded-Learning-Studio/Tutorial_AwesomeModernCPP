---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Implement a lightweight, zero-overhead general-purpose scope guard pattern
difficulty: intermediate
order: 6
platform: host
prerequisites:
- 'Deep Dive into RAII: The Cornerstone of Resource Management'
reading_time_minutes: 13
related:
- 'Custom Deleters and Intrusive Reference Counting'
tags:
- host
- cpp-modern
- intermediate
- RAII守卫
title: 'scope_guard and defer: A General-Purpose Scope Guard'
translation:
  source: documents/vol2-modern-features/ch01-smart-pointers/06-scope-guard.md
  source_hash: 49199867412ff6de89b2ed5a939ce8f47c5c7f7fbc58addd46c299d72e8fbd19
  translated_at: '2026-09-25T14:43:52+00:00'
  engine: anthropic
  token_count: 3300
---
# scope_guard and defer: A General-Purpose Scope Guard

In the previous few articles we discussed smart pointers—they manage the "lifecycle of resources" (memory, file handles, sockets, and so on). But in real-world engineering there is another category of scenarios: you need to perform an action when a scope exits, and that action is not necessarily "releasing a resource". It might be restoring some global state, committing or rolling back a transaction, writing a log entry, or notifying a monitoring component. This "execute on exit" need is more widespread and more flexible than resource management, and smart pointers—designed specifically for resource management—do not cover these scenarios well.

The scope_guard is the general-purpose tool designed for exactly this kind of need. Its core idea is dead simple: **bind a callable object to the destructor of a stack object—when the scope exits, it is invoked automatically**. That is how plain it is, and that is how useful it is.

## The Motivation for scope_guard: Not Just Resources, but Also State Rollback

Let's start with a real-world scenario: suppose you are writing a configuration-update function that needs to temporarily switch the system's operating mode, then restore the original mode once the operation completes. If the function has only one return point, restoring by hand is no problem. But once the function has multiple return paths, or calls in the middle that might throw, manual restoration becomes fragile.

```cpp
// Fragile version without a scope_guard
void update_config(Config& cfg) {
    Mode old_mode = get_current_mode();
    set_current_mode(kMaintenance);  // Temporarily switch the mode

    if (!validate(cfg)) {
        set_current_mode(old_mode);  // Restore point 1
        return;
    }

    if (!apply(cfg)) {
        set_current_mode(old_mode);  // Restore point 2
        return;
    }

    notify_observers();
    set_current_mode(old_mode);  // Restore point 3
    // What if notify_observers() throws? We forgot to restore!
}
```

Every time you modify this function—adding a new return path, adding a call that might throw—you have to check all of those "restore points" for misses. As the function grows more complex, the probability of missing one approaches 100%.

With a scope_guard, it is much simpler:

```cpp
void update_config_guarded(Config& cfg) {
    Mode old_mode = get_current_mode();
    set_current_mode(kMaintenance);

    // Automatically restored on scope exit—no matter how we exit
    auto restore_mode = make_scope_guard([&]() noexcept {
        set_current_mode(old_mode);
    });

    if (!validate(cfg)) return;  // Restored automatically
    if (!apply(cfg)) return;     // Restored automatically
    notify_observers();          // Restored automatically even if this throws
}  // Restored automatically on normal exit too
```

`restore_mode` is an RAII object—its destructor calls that lambda when the scope exits. Whether you leave via `return`, via a propagating exception, or by executing the function to its end normally, the restore action runs. You write the restore code exactly once, and never again worry about missing a path.

Put the guard's lifetime on a timeline: there is exactly one construction point, yet three exits, and every one of those exits flows through the same destructor logic:

![scope_guard execution timeline](./06-scope-guard-timeline.drawio)

## Implementing a General-Purpose ScopeGuard Class

The core implementation of a scope_guard is remarkably compact—a template class wrapping a callable object and an active flag. Let's start from the most basic version and refine it step by step.

First, the core implementation:

```cpp
#include <utility>
#include <exception>
#include <cstdlib>

template <typename F>
class ScopeGuard {
public:
    explicit ScopeGuard(F&& func) noexcept
        : func_(std::move(func)), active_(true)
    {}

    ScopeGuard(ScopeGuard&& other) noexcept
        : func_(std::move(other.func_)), active_(other.active_)
    {
        other.active_ = false;
    }

    ~ScopeGuard() noexcept {
        if (active_) {
            try {
                func_();
            } catch (...) {
                // An exception must never escape the destructor
                // otherwise it would cause std::terminate during stack unwinding
                std::terminate();
            }
        }
    }

    // Dismiss the guard: no cleanup needed after success
    void dismiss() noexcept { active_ = false; }

    // Copying is forbidden
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

private:
    F func_;
    bool active_;
};

template <typename F>
ScopeGuard<F> make_scope_guard(F&& func) noexcept {
    return ScopeGuard<F>(std::forward<F>(func));
}
```

Several design decisions here are worth noting. The destructor wraps the call to `func_()` in `try-catch(...)` and calls `std::terminate()` in the catch block. Under the C++ standard, if a destructor throws during stack unwinding, the program calls `std::terminate()` directly—the runtime cannot handle two exceptions at once. A function marked `noexcept` that throws would also lead to `terminate()` (the compiler reminds you with a `-Wterminate` warning), but the explicit try-catch leaves room to add logging or cleanup later on.

The `dismiss()` method lets you cancel the guard on the success path. This is extremely useful in "roll back only on failure" scenarios—we will see a more elegant `scope_fail` implementation later.

## The defer Pattern: Go-Style Deferred Execution

The Go language has a `defer` keyword that postpones a function call until the current function returns. The feature is widely popular in the Go community because it makes "cleanup code right next to acquisition code" a natural coding style.

C++ has no language-level `defer`, but macros + `ScopeGuard` deliver an experience remarkably close to it:

```cpp
// Helper macros: generate unique variable names automatically
#define SCOPE_GUARD_CONCAT_IMPL(x, y) x##y
#define SCOPE_GUARD_CONCAT(x, y) SCOPE_GUARD_CONCAT_IMPL(x, y)
#define SCOPE_GUARD_VAR(counter) SCOPE_GUARD_CONCAT(_scope_guard_, counter)

// __COUNTER__ guarantees a unique variable name every time
// __COUNTER__ is an extension supported by GCC/Clang/MSVC alike
#define DEFER(code) \
    auto SCOPE_GUARD_VAR(__COUNTER__) = make_scope_guard([&]() noexcept { code; })

// Fallback: if the compiler doesn't support __COUNTER__, use __LINE__
#define DEFER_LINE(code) \
    auto SCOPE_GUARD_CONCAT(_scope_guard_, __LINE__) = \
        make_scope_guard([&]() noexcept { code; })
```

Usage is very intuitive—`DEFER` followed by a piece of code, and that code runs when the current scope exits:

```cpp
void process_with_defer() {
    auto* region = allocate_region();
    DEFER({ release_region(region); });

    auto* buffer = acquire_buffer();
    DEFER({ release_buffer(buffer); });

    // All the cleanup code sits right next to the acquisition code
    // no need to pile up a bunch of release calls at the end of the function
    do_processing(region, buffer);

    // On scope exit, buffer is released first (last defined, first destroyed)
    // then region is released (first defined, last destroyed)
}
```

The benefit of the `DEFER` macro is putting the cleanup code and the acquisition code together—readers can see "when this resource gets released" without jumping to the end of the function. This locality greatly improves readability and maintainability.

The `DEFER` macro's lambda captures `[&]` (by reference), which means it refers to local variables of the enclosing scope. If those variables have already left scope by the time the `DEFER` code executes, you get dangling references. In practice, `DEFER` and the variables it captures usually live in the same scope, so this problem rarely shows up—but you should be aware of the risk. If you genuinely need to use the guard object across scopes, consider capturing by value (`[=]`), or make sure the guard object's lifetime never exceeds the captured variables.

## scope_success and scope_fail: Distinguishing the Success and Failure Paths

Sometimes you want an action to run only when the function returns normally (committing a transaction, for instance), or only when it exits via an exception (rolling back a transaction, for instance). C++17 provides `std::uncaught_exceptions()` to detect whether exception propagation is in flight—it returns the number of exceptions currently propagating but not yet caught. Based on this information, we can implement `scope_success` and `scope_fail`.

```cpp
template <typename F>
class ScopeSuccess {
public:
    explicit ScopeSuccess(F&& func) noexcept
        : func_(std::move(func))
        , active_(true)
        , uncaught_at_creation_(std::uncaught_exceptions())
    {}

    ~ScopeSuccess() noexcept {
        if (active_ && std::uncaught_exceptions() == uncaught_at_creation_) {
            try { func_(); } catch (...) { std::terminate(); }
        }
    }

    ScopeSuccess(ScopeSuccess&& other) noexcept
        : func_(std::move(other.func_))
        , active_(other.active_)
        , uncaught_at_creation_(other.uncaught_at_creation_)
    {
        other.active_ = false;
    }

    void dismiss() noexcept { active_ = false; }

    ScopeSuccess(const ScopeSuccess&) = delete;
    ScopeSuccess& operator=(const ScopeSuccess&) = delete;

private:
    F func_;
    bool active_;
    int uncaught_at_creation_;
};

template <typename F>
class ScopeFail {
public:
    explicit ScopeFail(F&& func) noexcept
        : func_(std::move(func))
        , active_(true)
        , uncaught_at_creation_(std::uncaught_exceptions())
    {}

    ~ScopeFail() noexcept {
        if (active_ && std::uncaught_exceptions() > uncaught_at_creation_) {
            try { func_(); } catch (...) { std::terminate(); }
        }
    }

    ScopeFail(ScopeFail&& other) noexcept
        : func_(std::move(other.func_))
        , active_(other.active_)
        , uncaught_at_creation_(other.uncaught_at_creation_)
    {
        other.active_ = false;
    }

    void dismiss() noexcept { active_ = false; }

    ScopeFail(const ScopeFail&) = delete;
    ScopeFail& operator=(const ScopeFail&) = delete;

private:
    F func_;
    bool active_;
    int uncaught_at_creation_;
};
```

The principle: record the current `uncaught_exceptions()` count at construction, then compare at destruction—if the count is unchanged, no new exception was thrown (`scope_success`); if the count has grown, a new exception is propagating (`scope_fail`).

Note the use of `std::uncaught_exceptions()` (plural), not the old `std::uncaught_exception()` (singular). The latter behaves incorrectly in nested try-catch scenarios—it can only tell you "is there an exception", not "is there a **new** exception". `uncaught_exceptions()` returns the precise count and detects nested scenarios correctly. The old `uncaught_exception()` was deprecated in C++17.

## A State Rollback Example: Transaction Processing

The most classic application of `scope_success` and `scope_fail` is transaction processing—commit on success, roll back on failure:

```cpp
#include <iostream>
#include <stdexcept>

class DatabaseTransaction {
public:
    void begin() { std::cout << "BEGIN TRANSACTION\n"; }
    void commit() { std::cout << "COMMIT\n"; }
    void rollback() { std::cout << "ROLLBACK\n"; }
};

void transfer_money(DatabaseTransaction& tx, int from, int to, int amount) {
    tx.begin();

    // Roll back automatically on failure
    auto on_fail = ScopeFail<std::decay_t<decltype([]() noexcept {
        std::cout << "自动回滚触发\n";
    })>>([]() noexcept {
        std::cout << "异常导致自动回滚\n";
    });

    // In a real project, a helper function can simplify this
    // auto on_fail = make_scope_fail([&]() noexcept { tx.rollback(); });

    if (amount <= 0) {
        throw std::invalid_argument("amount must be positive");
    }

    std::cout << "Transfer " << amount << " from " << from << " to " << to << "\n";

    // Commit on success
    // auto on_success = make_scope_success([&]() noexcept { tx.commit(); });
    // dismiss + manual commit is another common pattern here
}

void transaction_demo() {
    DatabaseTransaction tx;

    try {
        transfer_money(tx, 1001, 2002, -50);
    } catch (const std::exception& e) {
        std::cout << "捕获异常: " << e.what() << "\n";
    }
}
```

Output:

```text
BEGIN TRANSACTION
Transfer -50 from 1001 to 2002
异常导致自动回滚
ROLLBACK
捕获异常: amount must be positive
```

## Exception Safety and scope_guard

scope_guard is tightly connected to exception safety. In C++, exception safety comes in three levels (the basic guarantee, the strong guarantee, and the nothrow guarantee), and scope_guard is an important tool for achieving the strong guarantee.

Consider an operation that "modifies A first, then modifies B". If A's modification succeeds but B's fails, we need to roll back A to preserve the strong exception guarantee:

```cpp
void update_both(SubsystemA& a, SubsystemB& b, const Config& cfg) {
    StateA old_a = a.get_state();
    a.update(cfg);  // May throw

    // Set up a rollback guard for A
    auto rollback_a = make_scope_guard([&]() noexcept {
        a.restore(old_a);  // If a later step fails, roll A back
    });

    StateB old_b = b.get_state();
    b.update(cfg);  // If this throws, rollback_a's destructor rolls A back

    // B succeeded too—cancel A's rollback (a guard could be added for B as well if needed)
    rollback_a.dismiss();
}
```

This "operate first, roll back on failure" pattern is extremely common in database operations, filesystem operations, and network protocol implementations. scope_guard makes the pattern natural and less error-prone.

## Standardization Progress: std::scope_exit and Boost.Scope

The scope_guard pattern has caught the attention of the C++ standard committee. Library Fundamentals TS v3 (ISO/IEC TS 19568:2024) defines three scope guard class templates: `std::experimental::scope_exit` (executes on scope exit), `std::experimental::scope_success` (executes only on normal exit), and `std::experimental::scope_fail` (executes only on exceptional exit). Their behavior is essentially identical to what we implemented above, but the standardized versions provide stricter exception-safety guarantees and more complete interface constraints—for instance, `scope_exit`'s constructor is `noexcept`, and throwing at construction is not allowed (otherwise `terminate()` is called directly).

The Boost libraries also provide Boost.Scope, which implements similar components. If you would rather not implement a scope_guard yourself, you can use Boost.Scope directly, or the header-only scope-lite library (written by Martin Moene; it provides an interface compatible with the standard proposal and supports compilers going back to C++98).

In real projects, my usual practice is: if the project already depends on Boost, use Boost.Scope; if you do not want to take on a Boost dependency, use my own lightweight implementation (like the `ScopeGuard` we wrote today). In terms of feature completeness, the basic implementation is about 40 lines of code and already covers the core functionality.

That's a wrap for ch01. From RAII to smart pointers (`unique_ptr`, `shared_ptr`, `weak_ptr`), from custom deleters to intrusive reference counting, and finally to the general-purpose scope_guard—we have walked through the entire core toolkit of modern C++ resource management. Next chapter we will talk about `constexpr` and move computation to compile time.

## Reference Resources

- [cppreference: std::uncaught_exceptions](https://en.cppreference.com/w/cpp/error/uncaught_exception)
- [cppreference: Library Fundamentals TS v3 - scope_exit](https://en.cppreference.com/cpp/experimental/scope_exit)
- [Boost.Scope documentation](https://www.boost.org/libs/scope/)
- [scope-lite: A single-header implementation](https://github.com/martinmoene/scope-lite)
- Andrei Alexandrescu, *ScopeGuard*, Dr. Dobb's Journal, 2000
- [C++ Core Guidelines: Resource Management](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#S-resource)
