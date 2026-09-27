---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Implement a lightweight, zero-overhead, general-purpose scope guard pattern
difficulty: intermediate
order: 7
platform: host
prerequisites:
- 'Chapter 1: Deep Dive into RAII: The Cornerstone of Resource Management'
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
  source: documents/vol2-modern-features/ch01-smart-pointers/07-scope-guard.md
  source_hash: 44bf6ef73bdb875657166d758e6e712a2ab48fed5a3d1e2b1ec048e499b083a9
  translated_at: '2026-09-27T04:51:06+00:00'
  engine: anthropic
  token_count: 4200
---
# scope_guard and defer: A General-Purpose Scope Guard

In the past few articles we discussed smart pointers—they manage the “lifecycle of a resource” (memory, file handles, sockets, and so on). But in real-world engineering there is another family of scenarios: you need to execute some action when the scope exits, and that action is not necessarily “release the resource”. It might be restoring a piece of global state, committing or rolling back a transaction, writing a log entry, or notifying a monitoring component. This “execute on exit” need is more general and more flexible than resource management, and smart pointers—designed specifically for resource management—do not cover these scenarios well.

The scope_guard is the general-purpose tool built for exactly this family of needs. Its core idea is extremely simple: **bind a callable to the destructor of a stack object—when the scope exits, it gets invoked automatically**<RefLink :id="1" preview="Alexandrescu and Marginean, ScopeGuard, Dr. Dobbs Journal, 2000" />. That is all there is to it—and yet it is enormously useful.

## The Motivation for scope_guard: Not Just Resources, but State Rollback

Let's start with a real-world scenario: suppose you are writing a configuration-update function that temporarily switches the system's running mode and restores the original mode once the operation completes. If the function has a single return point, restoring by hand is fine. But if the function has multiple return paths, or if exceptions can be thrown along the way, manual restoration becomes fragile.

```cpp
// Fragile code without a scope_guard
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
    // But what if notify_observers() throws? Forgot to restore!
}
```

Every time you modify this function—adding a new return path, adding a call that might throw—you have to re-check every “restore point” for anything missed. As the function grows more complex, the probability of a miss approaches 100%.

With a scope_guard, things get much simpler:

```cpp
void update_config_guarded(Config& cfg) {
    Mode old_mode = get_current_mode();
    set_current_mode(kMaintenance);

    // Restored automatically on scope exit—no matter how we exit
    auto restore_mode = make_scope_guard([&]() noexcept {
        set_current_mode(old_mode);
    });

    if (!validate(cfg)) return;  // Restored automatically
    if (!apply(cfg)) return;     // Restored automatically
    notify_observers();          // Restored automatically even if this throws
}  // Restored automatically on normal exit too
```

`restore_mode` is a RAII object—its destructor invokes that lambda when the scope exits. Whether you `return`, an exception propagates, or the function simply runs to the end, the restore action gets executed. You write the restore code exactly once, and never again worry about missing a spot.

Put the guard's lifetime on a timeline and you can see it: one construction, but three exits—and every exit flows through the same destructor logic:

![scope_guard execution timeline](./07-scope-guard-timeline.drawio)

## Implementing a General-Purpose ScopeGuard Class

The core implementation of a scope_guard is remarkably compact—a class template that wraps a callable plus an active flag. Let's start from the most basic version and refine it step by step.

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
                // An exception must never be allowed to escape a destructor
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

Several design decisions here deserve a closer look. The destructor wraps the call to `func_()` in `try-catch(...)` and calls `std::terminate()` in the catch block. Under the C++ standard, if a destructor throws during stack unwinding, the program goes straight to `std::terminate()`—after all, the runtime cannot handle two exceptions at once. Throwing from a function marked `noexcept` also leads to `terminate()` (the compiler warns about it with `-Wterminate`), but the explicit try-catch leaves the door open for adding logging or cleanup later.

The `dismiss()` method lets you cancel the guard on the success path. That is very useful in “roll back only on failure” scenarios—later we will see a more elegant `scope_fail` implementation.

## The defer Pattern: Go-Style Delayed Execution

The Go language has a `defer` keyword that postpones a function call until the current function returns. The feature is wildly popular in the Go community because it turns “cleanup code right next to acquisition code” into a natural coding style.

C++ has no language-level `defer`, but macros plus `ScopeGuard` can deliver an experience very close to it:

```cpp
// Helper macros: generate a unique variable name automatically
#define SCOPE_GUARD_CONCAT_IMPL(x, y) x##y
#define SCOPE_GUARD_CONCAT(x, y) SCOPE_GUARD_CONCAT_IMPL(x, y)
#define SCOPE_GUARD_VAR(counter) SCOPE_GUARD_CONCAT(_scope_guard_, counter)

// __COUNTER__ guarantees a unique variable name every time
// __COUNTER__ is an extension supported by GCC/Clang/MSVC alike
#define DEFER(code) \
    auto SCOPE_GUARD_VAR(__COUNTER__) = make_scope_guard([&]() noexcept { code; })

// Fallback: if the compiler does not support __COUNTER__, use __LINE__
#define DEFER_LINE(code) \
    auto SCOPE_GUARD_CONCAT(_scope_guard_, __LINE__) = \
        make_scope_guard([&]() noexcept { code; })
```

Usage is very intuitive—`DEFER` takes a piece of code, and that code runs when the current scope exits:

```cpp
void process_with_defer() {
    auto* region = allocate_region();
    DEFER({ release_region(region); });

    auto* buffer = acquire_buffer();
    DEFER({ release_buffer(buffer); });

    // All cleanup code sits right next to the acquisition code
    // no need to pile up release calls at the end of the function
    do_processing(region, buffer);

    // On scope exit, buffer is released first (defined later, destroyed first)
    // then region is released (defined first, destroyed last)
}
```

The benefit of the `DEFER` macro is colocating cleanup code with acquisition code—the reader can see “when this resource gets released” without jumping to the end of the function. That locality greatly improves the readability and maintainability of the code.

The `DEFER` macro's lambda captures `[&]` (capture by reference), which means it refers to local variables of the enclosing scope. If those variables have already left scope by the time `DEFER` executes, you get a dangling reference. In practice, though, `DEFER` and the variables it captures usually live in the same scope, so the problem rarely shows up—but you should be aware of the risk. If you genuinely need to move the guard object across scopes, consider capturing by value (`[=]`), or make sure the guard object's lifetime never outlives the captured variables.

## scope_success and scope_fail: Telling the Success Path from the Failure Path

Sometimes you want an action to run only when the function “returns normally” (committing a transaction, say), or only when it “exits via an exception” (rolling the transaction back). C++17 provides `std::uncaught_exceptions()` to detect whether exception propagation is in flight—it returns the number of exceptions currently propagating but not yet caught<RefLink :id="2" preview="cppreference std::uncaught_exceptions — count of exceptions being propagated" />. On top of that information, we can implement `scope_success` and `scope_fail`.

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

The mechanism: record the current `uncaught_exceptions()` count at construction, then compare it at destruction—if the count is unchanged, no new exception was thrown (`scope_success`); if the count increased, a new exception is propagating (`scope_fail`).

Note the use of `std::uncaught_exceptions()` (plural) rather than the old `std::uncaught_exception()` (singular). The latter behaves incorrectly in nested try-catch scenarios—it can only tell you “is there an exception”, not “is there a **new** exception”. `uncaught_exceptions()` returns the precise count and detects nested scenarios correctly. The old `uncaught_exception()` was deprecated in C++17.

## A State-Rollback Example: Transaction Handling

The most classic application of `scope_success` and `scope_fail` is transaction handling—commit on success, roll back on failure:

```cpp
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <utility>

// make_scope_fail: a helper that leaves deduction to the compiler, no need to hand-write the decltype template argument
template <typename F>
ScopeFail<std::decay_t<F>> make_scope_fail(F&& func) noexcept {
    return ScopeFail<std::decay_t<F>>(std::forward<F>(func));
}

class DatabaseTransaction {
public:
    void begin() { std::cout << "BEGIN TRANSACTION\n"; }
    void commit() { std::cout << "COMMIT\n"; }
    void rollback() { std::cout << "ROLLBACK\n"; }
};

void transfer_money(DatabaseTransaction& tx, int from, int to, int amount) {
    tx.begin();

    // Automatic rollback on failure: when an exception propagates, ScopeFail's destructor triggers rollback
    auto on_fail = make_scope_fail([&tx]() noexcept {
        tx.rollback();
    });

    if (amount <= 0) {
        throw std::invalid_argument("amount must be positive");
    }

    std::cout << "Transfer " << amount << " from " << from << " to " << to << "\n";
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

The fully compilable version is right below (the `ScopeFail` template definition is in the source file too)—click “Try It Yourself” to run it directly:

<OnlineCompilerDemo
  title="Hands-On Verification: Automatic Transaction Rollback with scope_fail"
  source-path="code/examples/vol2/31_scope_fail_transaction.cpp"
  description="Verify transaction rollback online: a negative amount throws an exception, and ScopeFail triggers ROLLBACK during stack unwinding—note that the Transfer line never gets a chance to print, because the exception flies out before it."
  run-options="-std=c++17"
  allow-run
/>

## Exception Safety and scope_guard

scope_guard is deeply tied to exception safety. In C++, exception safety comes in three levels (the basic guarantee, the strong guarantee, and the nothrow guarantee), and scope_guard is an important tool for achieving the strong guarantee<RefLink :id="3" preview="C++ Core Guidelines, Section R: Resource Management" />.

Consider an operation that “first modifies A, then modifies B”. If A's modification succeeds but B's fails, we need to roll A back to preserve the strong exception guarantee:

```cpp
void update_both(SubsystemA& a, SubsystemB& b, const Config& cfg) {
    StateA old_a = a.get_state();
    a.update(cfg);  // May throw

    // Set up a rollback guard for A
    auto rollback_a = make_scope_guard([&]() noexcept {
        a.restore(old_a);  // If a later operation fails, roll A back
    });

    StateB old_b = b.get_state();
    b.update(cfg);  // If this throws, rollback_a's destructor rolls A back

    // B succeeded too, so cancel A's rollback (you could add a guard for B as well if needed)
    rollback_a.dismiss();
}
```

This “act first, roll back on failure” pattern is very common in database operations, filesystem operations, and network protocol implementations. scope_guard makes the pattern natural and hard to get wrong.

## Standardization Progress: std::scope_exit and Boost.Scope

The scope_guard pattern has caught the attention of the C++ standards committee. Library Fundamentals TS v3 (ISO/IEC TS 19568:2024) defines three scope guard class templates<RefLink :id="4" preview="cppreference std::experimental::scope_exit — Library Fundamentals TS v3" />: `std::experimental::scope_exit` (executes on scope exit), `std::experimental::scope_success` (executes only on normal exit), and `std::experimental::scope_fail` (executes only on exceptional exit). Their behavior matches what we implemented above almost exactly, but the standardized versions provide stricter exception-safety guarantees and more complete interface constraints—for example, `scope_exit`'s constructor is `noexcept` and is not allowed to throw during construction (otherwise `terminate()` is called directly).

The Boost libraries also provide Boost.Scope<RefLink :id="5" preview="Boost.Scope — C++-20 style scope guards for Boost" />, which implements similar components. If you would rather not implement scope_guard yourself, you can use Boost.Scope directly, or the header-only scope-lite library<RefLink :id="6" preview="Martin Moene, scope-lite — single-header scope guards, GitHub" /> (written by Martin Moene, it offers an interface compatible with the standard proposal and supports compilers going all the way back to C++98).

In real projects, the author's usual practice is: if the project already depends on Boost, use Boost.Scope; if introducing a Boost dependency is undesirable, use your own lightweight implementation (like the `ScopeGuard` we wrote today). In terms of feature completeness, the basic implementation is roughly 40 lines of code and already covers the core functionality.

With that, the toolkit is complete. The next article is the final one in this chapter, where we talk about the PIMPL idiom—putting the smart-pointer tools accumulated over the whole chapter into one engineering idiom, and closing out the chapter.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="Andrei Alexandrescu, Petru Marginean"
    title="Generic&lt;Programming&gt;: Change the Way You Write Exception-Safe Code — Forever (ScopeGuard)"
    publisher="Dr. Dobb's Journal"
    :year="2000"
  />
  <ReferenceItem
    :id="2"
    author="cppreference.com"
    title="std::uncaught_exception, std::uncaught_exceptions"
    chapter="uncaught_exceptions (C++17)"
    url="https://en.cppreference.com/w/cpp/error/uncaught_exception"
  />
  <ReferenceItem
    :id="3"
    author="Bjarne Stroustrup / Herb Sutter (eds.)"
    title="C++ Core Guidelines — R: Resource Management"
    publisher="isocpp.org"
    url="https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#r-resource-management"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="std::experimental::scope_exit"
    chapter="Library Fundamentals TS v3 (ISO/IEC TS 19568:2024)"
    url="https://en.cppreference.com/w/cpp/experimental/scope_exit"
  />
  <ReferenceItem
    :id="5"
    author="Boost"
    title="Boost.Scope"
    publisher="boost.org"
    url="https://www.boost.org/libs/scope/"
  />
  <ReferenceItem
    :id="6"
    author="Martin Moene"
    title="scope-lite: A Single-Header Scope Guard Library"
    publisher="GitHub"
    url="https://github.com/martinmoene/scope-lite"
  />
</ReferenceCard>
