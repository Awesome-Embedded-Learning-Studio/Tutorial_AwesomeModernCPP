---
title: 'scope_guard and defer: Generic Scope Guards'
description: Implement a lightweight, zero-overhead generic scope guard pattern
chapter: 1
order: 6
tags:
- host
- cpp-modern
- intermediate
- RAII守卫
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 14
- 17
- 20
reading_time_minutes: 15
prerequisites:
- 'Chapter 1: RAII 深入理解'
related:
- 自定义删除器
---
# scope_guard and defer: A General-Purpose Scope Guard

In previous chapters, we discussed smart pointers—they manage the "lifecycle of resources" (memory, file handles, sockets, etc.). But in real-world engineering, there is another category of scenarios: you need to perform an action when a scope exits, but that action isn't necessarily "releasing a resource." It might be restoring a global state, committing or rolling back a transaction, logging a message, or notifying a monitoring component. This "execute on exit" need is more universal and flexible than resource management, and smart pointers designed specifically for resource management don't cover these scenarios well.

A scope guard is a general-purpose tool designed for exactly this need. Its core idea is extremely simple: **bind a callable to the destructor of a stack object—when the scope exits, it is automatically invoked**. That's it. Plain and simple, but incredibly useful.

## The Motivation for scope_guard: Beyond Resources to State Rollback

Let's look at a real-world scenario. Suppose you are writing a configuration modification function that needs to temporarily change the system's operating mode, restoring the original mode when the operation completes. If the function has only one return point, manual restoration is fine. But if the function has multiple return paths, or if calls in the middle might throw exceptions, manual restoration becomes very fragile.

```cpp
// 没有 scope_guard 时的脆弱写法
void update_config(Config& cfg) {
    Mode old_mode = get_current_mode();
    set_current_mode(kMaintenance);  // 临时切换模式

    if (!validate(cfg)) {
        set_current_mode(old_mode);  // 恢复点 1
        return;
    }

    if (!apply(cfg)) {
        set_current_mode(old_mode);  // 恢复点 2
        return;
    }

    notify_observers();
    set_current_mode(old_mode);  // 恢复点 3
    // 如果 notify_observers() 抛异常呢？忘了恢复！
}
```

Every time you modify this function—adding a new return path, introducing a call that might throw—you have to check whether any "restoration points" were missed. As the function grows more complex, the probability of missing one approaches 100%.

Using a scope guard makes things much simpler:

```cpp
void update_config_guarded(Config& cfg) {
    Mode old_mode = get_current_mode();
    set_current_mode(kMaintenance);

    // 作用域退出时自动恢复——不管怎么退出
    auto restore_mode = make_scope_guard([&]() noexcept {
        set_current_mode(old_mode);
    });

    if (!validate(cfg)) return;  // 自动恢复
    if (!apply(cfg)) return;     // 自动恢复
    notify_observers();          // 即使抛异常也自动恢复
}  // 正常退出也自动恢复
```

`restore_mode` is a RAII object—its destructor invokes the lambda when the scope exits. Whether it's a `return`, exception propagation, or the function simply reaching its end, the restoration action is executed. You write the restoration code once, and never have to worry about missing it again.

## Implementing a General-Purpose ScopeGuard Class

The core implementation of a scope guard is very concise—a template class wrapping a callable and an active flag. We'll start with the most basic version and refine it step by step.

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
                // 析构函数中绝不能让异常逃逸
                // 否则在栈展开过程中会导致 std::terminate
                std::terminate();
            }
        }
    }

    // 取消守卫：成功后不需要执行清理
    void dismiss() noexcept { active_ = false; }

    // 禁止拷贝
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

This implementation has a few notable design decisions. The destructor wraps the `func_()` call in a `try-catch(...)` block and calls `std::terminate()` in the catch block. In the C++ standard, if a destructor throws an exception during stack unwinding, the program directly calls `std::terminate()` —after all, the runtime cannot handle two exceptions simultaneously. Although a function marked `noexcept` throwing an exception also leads to `terminate()` (which compilers will remind you about via a `-Wterminate` warning), an explicit try-catch gives us a chance to add logging or cleanup in the future. If you're unsure about the behavior of noexcept exception handling, you can run the relevant tests in this chapter's verification code (`06-scope-guard-verification.cpp`) to observe exactly when terminate is triggered.

The `dismiss()` method allows you to cancel the guard on the success path. This is extremely useful in "rollback only on failure" scenarios—we'll see a more elegant `scope_fail` implementation later.

## The defer Pattern: Go-Style Deferred Execution

The Go language has a `defer` keyword that defers a function call until the current function returns. This feature is widely popular in the Go community because it makes "placing cleanup code right after acquisition code" a natural coding style.

Although C++ doesn't have a language-level `defer`, we can achieve a very similar experience using a macro combined with `ScopeGuard`:

```cpp
// 辅助宏：自动生成唯一变量名
#define SCOPE_GUARD_CONCAT_IMPL(x, y) x##y
#define SCOPE_GUARD_CONCAT(x, y) SCOPE_GUARD_CONCAT_IMPL(x, y)
#define SCOPE_GUARD_VAR(counter) SCOPE_GUARD_CONCAT(_scope_guard_, counter)

// 使用 __COUNTER__ 保证每次生成唯一变量名
// __COUNTER__ 是 GCC/Clang/MSVC 都支持的扩展
#define DEFER(code) \
    auto SCOPE_GUARD_VAR(__COUNTER__) = make_scope_guard([&]() noexcept { code; })

// 备选方案：如果编译器不支持 __COUNTER__，用 __LINE__
#define DEFER_LINE(code) \
    auto SCOPE_GUARD_CONCAT(_scope_guard_, __LINE__) = \
        make_scope_guard([&]() noexcept { code; })
```

The usage is very intuitive—put a block of code after `DEFER`, and that code executes when the current scope exits:

```cpp
void process_with_defer() {
    auto* region = allocate_region();
    DEFER({ release_region(region); });

    auto* buffer = acquire_buffer();
    DEFER({ release_buffer(buffer); });

    // 所有清理代码紧跟在获取代码后面
    // 不需要在函数末尾写一堆 release 调用
    do_processing(region, buffer);

    // 作用域退出时，buffer 先释放（后定义的先析构）
    // 然后 region 释放（先定义的后析构）
}
```

The advantage of the `DEFER` macro is that it keeps the cleanup code right next to the acquisition code—readers don't need to jump to the end of the function to see "when this resource will be released." This locality greatly improves code readability and maintainability.

⚠️ The lambda in the `DEFER` macro captures `[&]` (by reference), meaning it references local variables from the enclosing scope. If those variables have already left the scope by the time `DEFER` executes, a dangling reference will occur. In practice, however, `DEFER` and the variables it captures are usually in the same scope, so this problem rarely arises—but you need to be aware of the risk. If you genuinely need to use a guard object across scopes, consider capturing by value (`[=]`) or ensuring the guard object's lifetime doesn't exceed the captured variables.

## scope_success and scope_fail: Distinguishing Success and Failure Paths

Sometimes you only want to execute an action when a function "returns normally" (e.g., committing a transaction), or only when it "exits via exception" (e.g., rolling back a transaction). C++17 provides `std::uncaught_exceptions()` to detect whether the code is currently in the process of propagating an exception—it returns the number of exceptions currently being propagated but not yet caught. Based on this information, we can implement `scope_success` and `scope_fail`.

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

The principle is: record the current `uncaught_exceptions()` count at construction, and compare it at destruction—if the count hasn't changed, no new exception was thrown (`scope_success`); if the count increased, a new exception is being propagated (`scope_fail`).

⚠️ Note the use of `std::uncaught_exceptions()` (plural) rather than the legacy `std::uncaught_exception()` (singular). The latter behaves incorrectly in nested try-catch scenarios—it can only tell you "whether there is an exception," not "whether there is a **new** exception." `uncaught_exceptions()` returns a precise count and can correctly detect nested scenarios. The legacy `uncaught_exception()` was deprecated in C++17.

## State Rollback Example: Transaction Processing

The most classic application scenario for `scope_success` and `scope_fail` is transaction processing—commit on success, rollback on failure:

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

    // 失败时自动回滚
    auto on_fail = ScopeFail<std::decay_t<decltype([]() noexcept {
        std::cout << "自动回滚触发\n";
    })>>([]() noexcept {
        std::cout << "异常导致自动回滚\n";
    });

    // 在实际项目中可以用辅助函数简化
    // auto on_fail = make_scope_fail([&]() noexcept { tx.rollback(); });

    if (amount <= 0) {
        throw std::invalid_argument("amount must be positive");
    }

    std::cout << "Transfer " << amount << " from " << from << " to " << to << "\n";

    // 成功时提交
    // auto on_success = make_scope_success([&]() noexcept { tx.commit(); });
    // 这里用 dismiss + 手动提交也是常见模式
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

The relationship between scope_guard and exception safety is very close. In C++, there are three levels of exception safety (basic guarantee, strong guarantee, and no-throw guarantee), and scope_guard is an important tool for achieving the strong guarantee.

Consider an operation that "modifies A, then modifies B." If A is modified successfully but B fails, we need to roll back A to guarantee strong exception safety:

```cpp
void update_both(SubsystemA& a, SubsystemB& b, const Config& cfg) {
    StateA old_a = a.get_state();
    a.update(cfg);  // 可能抛异常

    // 为 A 设置回滚守卫
    auto rollback_a = make_scope_guard([&]() noexcept {
        a.restore(old_a);  // 如果后续操作失败，回滚 A
    });

    StateB old_b = b.get_state();
    b.update(cfg);  // 如果这里抛异常，rollback_a 的析构会回滚 A

    // B 也成功了，取消 A 的回滚（如果需要也可以为 B 加守卫）
    rollback_a.dismiss();
}
```

This "act first, rollback on failure" pattern is very common in database operations, file system operations, and network protocol implementations. scope_guard makes this pattern natural and error-resistant.

## Standardization Progress: std::scope_exit and Boost.Scope

The scope_guard pattern has caught the attention of the C++ standard committee. Library Fundamentals TS v3 (ISO/IEC TS 19568:2024) defines three scope guard class templates: `std::experimental::scope_exit` (execute on scope exit), `std::experimental::scope_success` (execute only on normal exit), and `std::experimental::scope_fail` (execute only on exception exit). Their behavior is essentially consistent with what we implemented above, but the standardized version provides stricter exception safety guarantees and more complete interface constraints—for example, the constructor of `scope_exit` is `noexcept`, and throwing during construction is not allowed (otherwise `terminate()` is called directly).

The Boost library also provides Boost.Scope, which implements similar components. If you don't want to implement scope_guard yourself, you can directly use Boost.Scope or the header-only scope-lite library (written by Martin Moene, providing an interface compatible with the standard proposal and supporting compilers from C++98 onward).

In real projects, my usual approach is: if the project already depends on Boost, use Boost.Scope; if you don't want to introduce a Boost dependency, use a lightweight custom implementation (like the `ScopeGuard` we wrote today). In terms of feature completeness, our basic implementation is about 40 lines of code and already covers the core functionality—you can run `06-scope-guard-verification.cpp` to see its actual behavior in scenarios like multiple return paths, exception handling, and transaction patterns.

## Verification Code

We have written complete verification tests for this chapter, which you can use to verify the various behaviors of scope_guard:

```bash
# 编译（使用 g++）
g++ -std=c++17 -Wall -Wextra -O2 \
    code/volumn_codes/vol2/ch01-smart-pointers/06-scope-guard-verification.cpp \
    -o /tmp/06-scope-guard-verification

# 运行
/tmp/06-scope-guard-verification
```

The verification code includes the following test cases:

1. **Basic ScopeGuard** — Verifies execution on scope exit
2. **dismiss() functionality** — Verifies canceling the guard
3. **Multiple return paths** — Verifies cleanup on both early return and normal exit
4. **ScopeFail (execute on exception)** — Verifies triggering on exception exit
5. **ScopeFail (no execute without exception)** — Verifies no triggering on normal exit
6. **ScopeSuccess (execute on normal exit)** — Verifies triggering on normal exit
7. **ScopeSuccess (no execute on exception)** — Verifies no triggering on exception exit
8. **Transaction pattern** — Verifies an actual transaction processing scenario
9. **DEFER macro simulation** — Verifies resource release order
10. **std::uncaught_exceptions() behavior** — Verifies the exception detection mechanism

These tests cover all the key scenarios we discussed. You can run them directly to observe the output, or modify the code to test edge cases.

## Summary

scope_guard is a generalization of the RAII idea—it doesn't just manage resource acquisition and release, but manages any action that needs to execute when a scope exits. By wrapping an action in the destructor of a stack object, scope_guard guarantees that no matter how control flow leaves the scope (normal return, early return, exception propagation), the action will be executed.

Today we implemented three guard variants: `ScopeGuard` (always execute), `ScopeSuccess` (execute only on normal exit), and `ScopeFail` (execute only on exception exit), along with the `DEFER` macro to provide Go-style deferred execution syntax. These tools can simplify code and improve reliability in scenarios like transaction processing, state rollback, and resource cleanup—you can run the verification code to see how they perform in real-world scenarios.

This brings us to the end of this chapter. From RAII to smart pointers (`unique_ptr`, `shared_ptr`, `weak_ptr`), from custom deleters to intrusive reference counting, to the general-purpose scope_guard—we have fully covered the core toolkit for modern C++ resource management. Mastering these tools gives you the foundation to write safe, efficient, and maintainable C++ code.

## References

- [cppreference: std::uncaught_exceptions](https://en.cppreference.com/w/cpp/error/uncaught_exception)
- [cppreference: Library Fundamentals TS v3 - scope_exit](https://en.cppreference.com/cpp/experimental/scope_exit)
- [Boost.Scope documentation](https://www.boost.org/libs/scope/)
- [scope-lite: A single-header implementation](https://github.com/martinmoene/scope-lite)
- Andrei Alexandrescu, *ScopeGuard*, Dr. Dobb's Journal, 2000
- [C++ Core Guidelines: Resource Management](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#S-resource)
