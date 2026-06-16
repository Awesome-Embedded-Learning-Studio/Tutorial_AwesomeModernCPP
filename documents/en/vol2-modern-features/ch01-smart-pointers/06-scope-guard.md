---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Implement a lightweight, zero-overhead generic scope guard pattern
difficulty: intermediate
order: 6
platform: host
prerequisites:
- 'Chapter 1: RAII 深入理解'
reading_time_minutes: 13
related:
- 自定义删除器
tags:
- host
- cpp-modern
- intermediate
- RAII守卫
title: 'scope_guard and defer: Generic Scope Guard'
translation:
  source: documents/vol2-modern-features/ch01-smart-pointers/06-scope-guard.md
  source_hash: 84ca494fc921c473ad96e64f42023bc535b587062073282c194e409de28c867e
  translated_at: '2026-06-16T03:56:29.658748+00:00'
  engine: anthropic
  token_count: 2904
---
# scope_guard and defer: General-Purpose Scope Guards

In previous articles, we discussed smart pointers—they manage the "lifecycle of resources" (memory, file handles, sockets, etc.). However, in real-world engineering, there is another category of scenarios: you need to execute an operation when a scope exits, but that operation isn't necessarily "releasing a resource." It might be restoring a global state, committing or rolling back a transaction, logging a message, or notifying a monitoring component. This "execute on exit" requirement is more common and flexible than resource management, and smart pointers, which are specifically designed for resource management, do not cover these scenarios well.

The scope_guard is a general-purpose tool designed for exactly this need. Its core concept is extremely simple: **bind a callable object to the destructor of a stack object—automatically invoke it when the scope exits.** It is that simple, and yet that useful.

## Motivation for scope_guard: Not Just Resources, But State Rollback

Let's look at a real-world scenario: suppose you are writing a configuration modification function that needs to temporarily change the system's operating mode and restore the original mode after the operation is complete. If the function has only one return point, manual restoration is fine. But if the function has multiple return paths, or might throw an exception in the middle, manual restoration becomes very fragile.

```cpp
void modify_config() {
    SystemMode old_mode = current_mode;
    set_mode(new_mode); // Change to new mode

    // ... do some work ...

    if (error_condition) {
        set_mode(old_mode); // Restore manually
        return;
    }

    // ... do more work ...

    set_mode(old_mode); // Restore manually
}
```

Every time you modify this function—adding a new return path or adding a call that might throw an exception—you have to check if all "restore points" are missing. As the function grows more complex, the probability of missing one approaches 100%.

Using a scope_guard is much simpler:

```cpp
void modify_config() {
    SystemMode old_mode = current_mode;
    set_mode(new_mode);

    ScopeGuard guard([&]() { set_mode(old_mode); }); // RAII guard

    // ... do some work ...

    if (error_condition) {
        return; // Automatic restoration
    }

    // ... do more work ...
    // Automatic restoration
}
```

`guard` is a RAII object—its destructor calls that lambda when the scope exits. Whether it is an early `return`, exception propagation, or the function reaching the end normally, the restoration operation will be executed. You only need to write the restoration code once, and you never have to worry about missing it.

## Implementing a General-Purpose ScopeGuard Class

The core implementation of a scope_guard is very concise—a template class wrapping a callable object and an active flag. We will start with the most basic version and gradually refine it.

First, the core implementation:

```cpp
template <typename F>
class ScopeGuard {
public:
    explicit ScopeGuard(F&& f) : active_(true), func_(std::move(f)) {}

    ~ScopeGuard() noexcept {
        if (active_) {
            try {
                func_();
            } catch (...) {
                // If an exception occurs during stack unwinding,
                // std::terminate will be called.
                std::terminate();
            }
        }
    }

    void dismiss() noexcept { active_ = false; }

    // Disable copy semantics
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

    // Enable move semantics
    ScopeGuard(ScopeGuard&& other) noexcept
        : active_(other.active_), func_(std::move(other.func_)) {
        other.active_ = false;
    }

private:
    bool active_;
    F func_;
};
```

This implementation has several notable design decisions. The destructor wraps the `func_()` call in a `try-catch` block and calls `std::terminate()` in the catch block. In the C++ standard, if a destructor throws an exception during stack unwinding, the program immediately calls `std::terminate`—after all, the runtime cannot handle two exceptions simultaneously. Although a function marked `noexcept` throwing an exception also leads to `std::terminate` (which the compiler will warn you about via `-Wterminate`), the explicit try-catch gives us an opportunity to add logging or cleanup in the future. If you are unsure about the behavior of `noexcept` exception handling, you can run the relevant tests in the verification code (`test_scope_guard.cpp`) to observe the timing of `terminate` triggers.

The `dismiss()` method allows you to cancel the guard on the success path. This is very useful in "rollback only on failure" scenarios—we will see a more elegant `ScopeFail` implementation later.

## The defer Pattern: Go-Style Deferred Execution

The Go language has a `defer` keyword that defers a function call until the current function returns. This feature is widely popular in the Go community because it makes "putting cleanup code right after acquisition code" a natural coding style.

Although C++ does not have a language-level `defer`, we can achieve a very similar experience through a macro + `ScopeGuard`:

```cpp
#define CONCAT_IMPL(x, y) x##y
#define MACRO_CONCAT(x, y) CONCAT_IMPL(x, y)
#define DEFER(code) ScopeGuard MACRO_CONCAT(_defer_, __LINE__)([&]() { code; })
```

The usage is very intuitive—put a block of code after `DEFER`, and that code will execute when the current scope exits:

```cpp
void process_file(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "r");
    DEFER(fclose(fp)); // Automatically close when scope exits

    // ... read file ...
}
```

The `DEFER` macro keeps cleanup code and acquisition code together—readers don't need to jump to the end of the function to see "when this resource will be released." This locality significantly improves code readability and maintainability.

⚠️ The `DEFER` macro's lambda captures `this` by reference, meaning it refers to local variables in the outer scope. If the variables have left the scope when the `defer` executes, a dangling reference will occur. However, in practice, `DEFER` and the variables it captures are usually in the same scope, so this problem rarely arises—but you must be aware of this risk. If you do need to use the guard object across scopes, consider capturing by value (`[=]`) or ensure the guard object's lifetime does not exceed the captured variables.

## scope_success and scope_fail: Distinguishing Success and Failure Paths

Sometimes you only want to execute an operation when a function "returns normally" (e.g., committing a transaction), or only when it "exits via exception" (e.g., rolling back a transaction). C++17 provides `std::uncaught_exceptions` to detect whether an exception is currently propagating—it returns the number of exceptions currently propagating but not yet caught. Based on this information, we can implement `ScopeSuccess` and `ScopeFail`.

```cpp
class ScopeFail {
public:
    explicit ScopeFail(std::function<void()> f)
        : uncaught_(std::uncaught_exceptions()), func_(std::move(f)) {}

    ~ScopeFail() {
        if (std::uncaught_exceptions() > uncaught_) {
            func_();
        }
    }

private:
    int uncaught_;
    std::function<void()> func_;
};

class ScopeSuccess {
public:
    explicit ScopeSuccess(std::function<void()> f)
        : uncaught_(std::uncaught_exceptions()), func_(std::move(f)) {}

    ~ScopeSuccess() {
        if (std::uncaught_exceptions() == uncaught_) {
            func_();
        }
    }

private:
    int uncaught_;
    std::function<void()> func_;
};
```

The principle is: record the current count of `uncaught_exceptions` at construction, and compare at destruction—if the count hasn't changed, no new exception was thrown (`ScopeSuccess`); if the count increased, a new exception is propagating (`ScopeFail`).

⚠️ Note the use of `std::uncaught_exceptions` (plural) instead of the old `std::uncaught_exception` (singular). The latter behaves incorrectly in nested try-catch scenarios—it can only tell you "if there is an exception," not "if there is a **new** exception." `std::uncaught_exceptions` returns an accurate count and can correctly detect nested scenarios. The old `std::uncaught_exception` was deprecated in C++17.

## State Rollback Example: Transaction Processing

`ScopeSuccess` and `ScopeFail` are most classically used in transaction processing—commit on success, rollback on failure:

```cpp
void transfer_money(Account& from, Account& to, int amount) {
    from.lock();
    ScopeGuard unlock_from([&]() { from.unlock(); });

    to.lock();
    ScopeGuard unlock_to([&]() { to.unlock(); });

    if (from.balance() < amount) {
        throw std::runtime_error("Insufficient funds");
    }

    from.withdraw(amount);
    to.deposit(amount);

    // If we reach here, everything succeeded
    ScopeSuccess commit([&]() {
        log_transaction(from, to, amount);
    });
}
```

Output:

```text
[INFO] Transaction committed: from=1234 to=5678 amount=100
```

## Exception Safety and scope_guard

scope_guard is closely related to exception safety. In C++, there are three levels of exception safety (basic guarantee, strong guarantee, and no-throw guarantee), and scope_guard is an important tool for achieving the strong guarantee.

Consider an operation that "modifies A, then modifies B." If A succeeds but B fails, we need to roll back A to ensure strong exception safety:

```cpp
void update_data(Data& a, Data& b) {
    Data backup_a = a; // Create backup
    a.modify();        // Modify A

    ScopeGuard rollback_a([&]() { a = backup_a; });

    b.modify();        // Modify B (may throw)

    rollback_a.dismiss(); // Success, cancel rollback
}
```

This "act first, rollback on failure" pattern is very common in database operations, file system operations, and network protocol implementations. scope_guard makes this pattern natural and error-proof.

## Standardization Progress: std::scope_exit and Boost.Scope

The scope_guard pattern has been noticed by the C++ Standards Committee. Library Fundamentals TS v3 (ISO/IEC TS 19568:2024) defines three scope guard class templates: `std::scope_exit` (execute on scope exit), `std::scope_success` (execute only on normal exit), and `std::scope_fail` (execute only on exception exit). Their behavior is basically consistent with our implementation above, but the standardized version provides stricter exception safety guarantees and more complete interface constraints—for example, `std::scope_exit`'s constructor is `noexcept` and does not allow throwing during construction (otherwise it would directly call `std::terminate`).

The Boost library also provides Boost.Scope, which implements similar components. If you don't want to implement scope_guard yourself, you can directly use Boost.Scope or the header-only scope-lite library (written by Martin Moene, providing an interface compatible with the standard proposal, supporting compilers from C++98 onwards).

In actual projects, my usual approach is: if the project already depends on Boost, use Boost.Scope; if you don't want to introduce Boost dependencies, use your own lightweight implementation (like the `ScopeGuard` we wrote today). In terms of functional completeness, our basic implementation is about 40 lines of code and already covers the core functionality—you can run `test_scope_guard.cpp` to see its actual performance in scenarios like multiple return paths, exception handling, and transaction patterns.

## Verification Code

We have written complete verification tests for this chapter that you can use to verify the various behaviors of scope_guard:

```cpp
// test_scope_guard.cpp
#include <iostream>
#include <functional>
#include <stdexcept>
#include <exception>

// ... (Implementation of ScopeGuard, ScopeSuccess, ScopeFail, DEFER) ...

void test_basic_scope_guard() {
    std::cout << "=== Test: Basic ScopeGuard ===" << std::endl;
    bool executed = false;
    {
        ScopeGuard guard([&]() { executed = true; });
    }
    std::cout << "Executed: " << (executed ? "Yes" : "No") << std::endl;
}

void test_dismiss() {
    std::cout << "\n=== Test: Dismiss ===" << std::endl;
    bool executed = false;
    {
        ScopeGuard guard([&]() { executed = true; });
        guard.dismiss();
    }
    std::cout << "Executed (should be No): " << (executed ? "Yes" : "No") << std::endl;
}

void test_multiple_returns() {
    std::cout << "\n=== Test: Multiple Returns ===" << std::endl;
    auto helper = [](bool early_return) {
        ScopeGuard guard([]() { std::cout << "Cleanup executed" << std::endl; });
        if (early_return) {
            std::cout << "Early return" << std::endl;
            return;
        }
        std::cout << "Normal execution" << std::endl;
    };

    helper(true);
    helper(false);
}

void test_scope_fail_exception() {
    std::cout << "\n=== Test: ScopeFail (Exception) ===" << std::endl;
    try {
        ScopeFail guard([]() { std::cout << "Rollback executed" << std::endl; });
        throw std::runtime_error("Error");
    } catch (...) {
        std::cout << "Exception caught" << std::endl;
    }
}

void test_scope_fail_no_exception() {
    std::cout << "\n=== Test: ScopeFail (No Exception) ===" << std::endl;
    ScopeFail guard([]() { std::cout << "Rollback (should not execute)" << std::endl; });
    std::cout << "Normal exit" << std::endl;
}

void test_scope_success_normal() {
    std::cout << "\n=== Test: ScopeSuccess (Normal) ===" << std::endl;
    ScopeSuccess guard([]() { std::cout << "Commit executed" << std::endl; });
    std::cout << "Normal exit" << std::endl;
}

void test_scope_success_exception() {
    std::cout << "\n=== Test: ScopeSuccess (Exception) ===" << std::endl;
    try {
        ScopeSuccess guard([]() { std::cout << "Commit (should not execute)" << std::endl; });
        throw std::runtime_error("Error");
    } catch (...) {
        std::cout << "Exception caught" << std::endl;
    }
}

void test_transaction_pattern() {
    std::cout << "\n=== Test: Transaction Pattern ===" << std::endl;
    try {
        bool step1_success = true;
        bool step2_success = false; // Simulate failure

        ScopeGuard rollback_step1([&]() { std::cout << "Rollback Step 1" << std::endl; });

        std::cout << "Step 1 completed" << std::endl;

        if (!step2_success) {
            throw std::runtime_error("Step 2 failed");
        }

        rollback_step1.dismiss();
        std::cout << "Transaction committed" << std::endl;
    } catch (...) {
        std::cout << "Transaction failed" << std::endl;
    }
}

void test_defer_macro() {
    std::cout << "\n=== Test: DEFER Macro ===" << std::endl;
    {
        DEFER(std::cout << "Deferred cleanup 1" << std::endl);
        DEFER(std::cout << "Deferred cleanup 2" << std::endl);
        std::cout << "Main action" << std::endl;
    }
}

void test_uncaught_exceptions() {
    std::cout << "\n=== Test: std::uncaught_exceptions ===" << std::endl;
    std::cout << "Initial count: " << std::uncaught_exceptions() << std::endl;
    try {
        ScopeFail guard([]() { std::cout << "Exception detected" << std::endl; });
        throw std::runtime_error("Test");
    } catch (...) {
        std::cout << "In catch block, count: " << std::uncaught_exceptions() << std::endl;
    }
}

int main() {
    test_basic_scope_guard();
    test_dismiss();
    test_multiple_returns();
    test_scope_fail_exception();
    test_scope_fail_no_exception();
    test_scope_success_normal();
    test_scope_success_exception();
    test_transaction_pattern();
    test_defer_macro();
    test_uncaught_exceptions();
    return 0;
}
```

The verification code includes the following test cases:

1. **Basic ScopeGuard** — Verifies execution on scope exit.
2. **dismiss() functionality** — Verifies canceling the guard.
3. **Multiple return paths** — Verifies cleanup on both early return and normal exit.
4. **ScopeFail (on exception)** — Verifies trigger on exception exit.
5. **ScopeFail (no exception)** — Verifies no trigger on normal exit.
6. **ScopeSuccess (on normal)** — Verifies trigger on normal exit.
7. **ScopeSuccess (on exception)** — Verifies no trigger on exception exit.
8. **Transaction Pattern** — Verifies actual transaction processing scenarios.
9. **DEFER macro simulation** — Verifies resource release order.
10. **std::uncaught_exceptions() behavior** — Verifies exception detection mechanism.

These tests cover all the key scenarios we discussed. You can run them directly to observe the output, or modify the code to test edge cases.

## Summary

scope_guard is a generalization of the RAII concept—it manages not only resource acquisition and release, but any operation that needs to be executed when a scope exits. By wrapping an operation in the destructor of a stack object, scope_guard guarantees that the operation will be executed regardless of how the control flow leaves the scope (normal return, early return, exception propagation).

Today we implemented three guard variants: `ScopeGuard` (always execute), `ScopeSuccess` (execute only on normal exit), `ScopeFail` (execute only on exception exit), and the `DEFER` macro to provide Go-style deferred execution syntax. These tools can simplify code and improve reliability in scenarios like transaction processing, state rollback, and resource cleanup—you can run the verification code to see their performance in actual scenarios.

This chapter comes to an end here. From RAII to smart pointers (`unique_ptr`, `shared_ptr`, `weak_ptr`), from custom deleters to intrusive reference counting, to the general-purpose scope_guard—we have fully covered the core toolkit for modern C++ resource management. Mastering these tools equips you with the foundation to write safe, efficient, and maintainable C++ code.

## References

- [cppreference: std::uncaught_exceptions](https://en.cppreference.com/w/cpp/error/uncaught_exception)
- [cppreference: Library Fundamentals TS v3 - scope_exit](https://en.cppreference.com/cpp/experimental/scope_exit)
- [Boost.Scope documentation](https://www.boost.org/libs/scope/)
- [scope-lite: A single-header implementation](https://github.com/martinmoene/scope-lite)
- Andrei Alexandrescu, *ScopeGuard*, Dr. Dobb's Journal, 2000
- [C++ Core Guidelines: Resource Management](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#S-resource)
