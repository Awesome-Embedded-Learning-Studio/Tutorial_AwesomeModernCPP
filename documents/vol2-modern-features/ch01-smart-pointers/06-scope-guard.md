---
title: "scope_guard 与 defer：通用作用域守卫"
description: "实现轻量级、零开销的通用作用域守卫模式"
chapter: 1
order: 6
tags:
  - host
  - cpp-modern
  - intermediate
  - RAII守卫
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
reading_time_minutes: 15
prerequisites:
  - "Chapter 1: RAII 深入理解"
related:
  - "自定义删除器"
---

# scope_guard 与 defer：通用作用域守卫

在前面几篇我们讨论了智能指针——它们管理的是"资源的生命周期"（内存、文件句柄、socket 等）。但在实际工程中，还有一类场景：你需要在作用域退出时执行某个操作，但这个操作不一定是"释放资源"。它可能是恢复某个全局状态、提交或回滚一个事务、记录一条日志、通知某个监控组件。这种"退出时执行"的需求比资源管理更普遍、更灵活，而专门为资源管理设计的智能指针并不能很好地覆盖这些场景。

scope_guard（作用域守卫）就是为这类需求设计的通用工具。它的核心思想极其简单：**把一个可调用对象绑定到一个栈对象的析构函数上——作用域退出时，自动调用**。就这么朴素，但就这么有用。

## scope_guard 的动机：不只是资源，还有状态回滚

我们先来看一个真实的场景：假设你在写一个配置修改函数，需要临时改变系统的运行模式，在操作完成后恢复原来的模式。如果函数只有一个 return 点，手动恢复没问题。但如果函数有多个 return path，或者中间可能抛出异常，手动恢复就会变得很脆弱。

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

每次修改这个函数——添加新的 return path、增加可能抛异常的调用——你都得检查所有的"恢复点"有没有遗漏。随着函数越来越复杂，遗漏的概率趋近于 100%。

用 scope_guard 就简单多了：

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

`restore_mode` 是一个 RAII 对象——它的析构函数会在作用域退出时调用那个 lambda。不管是 `return`、异常传播、还是函数正常执行到末尾，恢复操作都会被执行。你只需要写一次恢复代码，再也不用担心遗漏。

## 实现一个通用的 ScopeGuard 类

scope_guard 的核心实现非常精简——一个模板类，包装一个可调用对象和一个 active 标志位。我们从最基础版本开始，逐步完善。

首先是核心实现：

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

这个实现有几个值得注意的设计决策。析构函数用 `try-catch(...)` 包裹了 `func_()` 的调用，并在 catch 块中调用 `std::terminate()`。这是因为析构函数在栈展开过程中被调用时，如果再次抛出异常，C++ 运行时会直接终止程序（因为无法同时处理两个异常）。虽然 `noexcept` 函数抛异常也会导致 `terminate()`，但显式的 try-catch 给了我们一个将来添加日志的机会。

`dismiss()` 方法允许你在成功路径上取消守卫。这在"只在失败时回滚"的场景中非常有用——我们后面会看到更优雅的 `scope_fail` 实现。

## defer 模式：Go 风格的延迟执行

Go 语言有一个 `defer` 关键字，它可以把一个函数调用延迟到当前函数返回时执行。这个特性在 Go 社区广受欢迎，因为它让"清理代码紧跟在获取代码后面"成为一种自然的编码风格。

虽然 C++ 没有语言级别的 `defer`，但通过宏 + `ScopeGuard` 可以实现非常接近的体验：

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

用法非常直观——`DEFER` 后面跟一段代码，这段代码会在当前作用域退出时执行：

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

`DEFER` 宏的好处是把清理代码和获取代码放在了一起——读者不需要跳到函数末尾就能看到"这个资源会在什么时候释放"。这种局部性大大提高了代码的可读性和可维护性。

⚠️ `DEFER` 宏的 lambda 捕获了 `[&]`（引用捕获），这意味着它引用了外层作用域的局部变量。如果在 `DEFER` 执行时这些变量已经离开作用域，就会产生悬垂引用。不过在实际使用中，`DEFER` 和它捕获的变量通常在同一个作用域内，所以这个问题很少出现——但你要意识到这个风险。

## scope_success 和 scope_fail：区分成功与失败路径

有时候你只想在函数"正常返回"时执行某个操作（比如提交事务），或者只在"异常退出"时执行（比如回滚事务）。C++17 提供了 `std::uncaught_exceptions()` 来检测当前是否处于异常传播中——它返回当前正在传播但尚未被捕获的异常数量。基于这个信息，我们可以实现 `scope_success` 和 `scope_fail`。

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

原理是：在构造时记录当前的 `uncaught_exceptions()` 数量，在析构时比较——如果数量没变，说明没有新的异常被抛出（`scope_success`）；如果数量增加了，说明有新的异常正在传播（`scope_fail`）。

⚠️ 注意使用 `std::uncaught_exceptions()`（复数）而不是旧的 `std::uncaught_exception()`（单数）。后者在嵌套 try-catch 的场景下行为不正确——它只能告诉你"有没有异常"，而不能告诉你"有没有**新的**异常"。`uncaught_exceptions()` 返回精确的数量，可以正确检测嵌套场景。旧的 `uncaught_exception()` 在 C++17 中已被弃用。

## 状态回滚示例：事务处理

`scope_success` 和 `scope_fail` 最经典的应用场景是事务处理——成功时提交，失败时回滚：

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

运行结果：

```text
BEGIN TRANSACTION
Transfer -50 from 1001 to 2002
异常导致自动回滚
ROLLBACK
捕获异常: amount must be positive
```

## 异常安全与 scope_guard

scope_guard 与异常安全的关系非常紧密。在 C++ 中，异常安全有三个级别（基本保证、强保证、不抛出保证），而 scope_guard 是实现强保证的重要工具。

考虑一个"先修改 A，再修改 B"的操作。如果 A 修改成功但 B 修改失败，我们需要回滚 A 以保证强异常安全：

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

这种"先操作，失败则回滚"的模式在数据库操作、文件系统操作、网络协议实现中非常常见。scope_guard 让这种模式变得自然且不容易出错。

## 标准化进展：std::scope_exit 与 Boost.Scope

scope_guard 模式已经被 C++ 标准委员会注意到。Library Fundamentals TS v3（ISO/IEC TS 19568:2024）定义了三个作用域守卫类模板：`std::experimental::scope_exit`（作用域退出时执行）、`std::experimental::scope_success`（仅在正常退出时执行）和 `std::experimental::scope_fail`（仅在异常退出时执行）。它们的行为与我们上面实现的基本一致，但提供了更严格的接口约束和更好的编译器支持。

Boost 库也提供了 Boost.Scope，实现了类似的组件。如果你不想自己实现 scope_guard，可以直接使用 Boost.Scope 或者头文件-only 的 scope-lite 库（Martin Moene 编写，提供与标准提案兼容的接口，支持 C++98 起的编译器）。

在实际项目中，笔者通常的做法是：如果项目已经依赖 Boost，就用 Boost.Scope；如果不想引入 Boost 依赖，就用自己的轻量实现（就像我们今天写的那个 `ScopeGuard`）——总共不到 40 行代码，但能显著提高代码的异常安全性。

## 小结

scope_guard 是 RAII 思想的通用化——不仅管理资源的获取和释放，还管理任何需要在作用域退出时执行的操作。通过把操作包装在一个栈对象的析构函数中，scope_guard 保证了不管控制流如何离开作用域（正常返回、提前 return、异常传播），操作都会被执行。

我们今天实现了三个守卫变体：`ScopeGuard`（总是执行）、`ScopeSuccess`（仅正常退出时执行）、`ScopeFail`（仅异常退出时执行），以及 `DEFER` 宏来提供 Go 风格的延迟执行语法。这些工具在事务处理、状态回滚、资源清理等场景中都能显著简化代码并提高可靠性。

这个章节到这里就告一段落了。从 RAII 到智能指针（`unique_ptr`、`shared_ptr`、`weak_ptr`），从自定义删除器到侵入式引用计数，再到通用的 scope_guard——我们完整地覆盖了现代 C++ 资源管理的核心工具链。掌握这些工具，就掌握了写出安全、高效、可维护的 C++ 代码的基础。

## 参考资源

- [cppreference: std::uncaught_exceptions](https://en.cppreference.com/w/cpp/error/uncaught_exception)
- [cppreference: Library Fundamentals TS v3 - scope_exit](https://en.cppreference.com/cpp/experimental/scope_exit)
- [Boost.Scope documentation](https://www.boost.org/libs/scope/)
- [scope-lite: A single-header implementation](https://github.com/martinmoene/scope-lite)
- Andrei Alexandrescu, *ScopeGuard*, Dr. Dobb's Journal, 2000
- [C++ Core Guidelines: Resource Management](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#S-resource)
