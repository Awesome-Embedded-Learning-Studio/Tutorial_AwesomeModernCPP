# 嵌入式现代C++教程——作用域守卫（Scope Guard）：让清理代码乖乖在“出门顺手关灯”那一刻执行

写嵌入式代码时，总会遇到这样的人生真相：你在函数某处申请了资源（打开外设、上锁、禁中断、分配缓冲……），后来代码分叉、提前 `return`、甚至抛出异常——结果忘了释放/恢复。结果就是内存泄漏、死锁、外设状态奇怪，或者你被老大盯着问“为什么这段代码跑了两分钟还没返回”。

作用域守卫（Scope Guard）就是为了解决这个问题的——把“离开当前作用域时必须做的事”绑定在一个对象的析构函数上：只要对象离开作用域，析构函数就会执行，清理也就稳了。它是 RAII 的小而美的实用变体，尤其适合嵌入式场景（没有堆分配、追求确定性）。

------

# 先看最简单的：lambda + 小模板

这是最常见的现代 C++ 写法（C++11 起就能用）。核心思想：封装一个可调用对象，析构时调用它（如果没有被取消）。

```cpp
#include <utility>
#include <exception>
#include <cstdlib> // for std::terminate

template <typename F>
class ScopeGuard {
public:
    explicit ScopeGuard(F&& f) noexcept
        : fn_(std::move(f)), active_(true) {}

    // 不允许拷贝（避免重复调用）
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

    // 允许移动
    ScopeGuard(ScopeGuard&& other) noexcept
        : fn_(std::move(other.fn_)), active_(other.active_) {
        other.dismiss();
    }

    ~ScopeGuard() noexcept {
        if (active_) {
            try {
                fn_();
            } catch (...) {
                // 析构函数不可抛出 —— 在嵌入式中通常直接终止
                std::terminate();
            }
        }
    }

    void dismiss() noexcept { active_ = false; }

private:
    F fn_;
    bool active_;
};

// 辅助函数方便模板推导
template <typename F>
ScopeGuard<F> make_scope_guard(F&& f) {
    return ScopeGuard<F>(std::forward<F>(f));
}
```

用法示例：

```cpp
void foo() {
    auto g = make_scope_guard([](){ close_device(); });
    // do something...
    if (error) return; // close_device 会被保证调用
    g.dismiss(); // 如果想提前取消清理
}
```

幽默注：`dismiss()` 就是给守卫放假，不让它在离职那天烦你。

------

# 成功/失败分支：`scope_success` 和 `scope_fail`

有时候你只想在函数“正常返回”（no exception）时做事，或者只在**抛异常**时处理。C++17 提供了 `std::uncaught_exceptions()` 来判断析构时是否处于异常传播中。基于它，我们可以实现 `scope_exit`（总是执行）、`scope_success`（仅在没有异常时执行）、`scope_fail`（仅在有异常时执行）。

```cpp
#include <exception>

template <typename F>
class ScopeGuardOnExit {
    // 同上，始终执行
};

template <typename F>
class ScopeGuardOnSuccess {
public:
    explicit ScopeGuardOnSuccess(F&& f) noexcept
      : fn_(std::move(f)), active_(true), uncaught_at_construction_(std::uncaught_exceptions()) {}

    ~ScopeGuardOnSuccess() noexcept {
        if (active_ && std::uncaught_exceptions() == uncaught_at_construction_) {
            try { fn_(); } catch(...) { std::terminate(); }
        }
    }
    // ... move/dismiss same as above
private:
    F fn_;
    bool active_;
    int uncaught_at_construction_;
};

template <typename F>
class ScopeGuardOnFail {
public:
    explicit ScopeGuardOnFail(F&& f) noexcept
      : fn_(std::move(f)), active_(true), uncaught_at_construction_(std::uncaught_exceptions()) {}

    ~ScopeGuardOnFail() noexcept {
        if (active_ && std::uncaught_exceptions() > uncaught_at_construction_) {
            try { fn_(); } catch(...) { std::terminate(); }
        }
    }
    // ...
private:
    F fn_;
    bool active_;
    int uncaught_at_construction_;
};
```

这样你就可以写：

```cpp
auto on_success = make_scope_guard_success([](){ commit_tx(); });
auto on_fail = make_scope_guard_fail([](){ rollback_tx(); });
```

在嵌入式里如果禁用异常，这俩就没用武之地 —— 但是 `scope_exit`（总是执行）仍然非常有用。

------

# 方便的宏：减少样板代码

写守卫变量名挺烦的，宏可以帮你：

```cpp
#define CONCAT_IMPL(x, y) x##y
#define CONCAT(x, y) CONCAT_IMPL(x, y)
#define SCOPE_GUARD(code) \
    auto CONCAT(_scope_guard_, __COUNTER__) = make_scope_guard([&](){ code; })
```

用法：

```cpp
SCOPE_GUARD({ disable_irq(); restore_irq_state(saved); });
// 作用域结束时自动调用
```

在没有 `__COUNTER__` 的编译器上用 `__LINE__` 也行，不过 `__COUNTER__` 更保险。

------

# 例子

1. 禁用中断并保证恢复（伪代码，平台提供 `__disable_irq()` / `__enable_irq()`）：

```cpp
void critical_section() {
    bool prev = save_and_disable_irq();
    auto restore = make_scope_guard([&]{ restore_irq(prev); });

    // 关键操作
}
```

1. 上锁/解锁

```cpp
mutex.lock();
auto unlock = make_scope_guard([&]{ mutex.unlock(); });
// 如果函数中途 return，mutex 会被正确解锁
```

1. 临时改变寄存器、并在退出恢复

```cpp
uint32_t old = REG_CTRL;
REG_CTRL = old | ENABLE_BIT;
auto restore_reg = make_scope_guard([=]{ REG_CTRL = old; });
```

------

# 嵌入式注意事项与最佳实践

- **不要分配堆内存**：守卫对象本身应该在栈上，成员不要包含动态分配，嵌入式通常禁用或不喜欢堆。
- **析构函数必须不抛异常**：标准要求析构不能抛出（会导致 `std::terminate` 在异常传播时）。我们的实现用 `try`/`catch(...) { std::terminate(); }` 或者如果你有日志系统可以先记录再终止。另一个选择是静默吞掉异常，但那可能掩盖错误。
- **尽量内联（`inline`）**：模板 + `constexpr` / `inline` 有利于编译器优化，不增加运行时开销。
- **对象大小**：实现非常小（一个可调用对象 + 一个 `bool`），对内存敏感的场景适合。避免把大对象捕获到 lambda 中。
- **编译器/标准**：如果你用的是老旧编译器，确保至少支持 C++11（lambda、移动语义）。若要 `scope_success/fail`，需要 C++17 的 `std::uncaught_exceptions()`。
- **禁用异常的环境**：如果工程编译时禁用异常（`-fno-exceptions`），`scope_fail`/`scope_success` 不可用。`scope_exit` 仍然适用，仍能保证清理行为。
- **避免在中断上下文做复杂事情**：在 ISR 中创建守卫要小心（栈空间有限、不要调用可能阻塞或分配的函数）。

------

# 用 `std::unique_ptr` 的小技巧（简单场景）

有时候你只是想用现有工具来做简单清理：

```cpp
#include <memory>

auto closer = std::unique_ptr<void, decltype([](void*){ close_fd(fd); })>(nullptr, [](void*){ close_fd(fd); });
```

但这种写法语义上不如专门的 `ScopeGuard` 清晰（模板更适合做任意清理），我提是为了给你多一个“武器库”里的小工具。

