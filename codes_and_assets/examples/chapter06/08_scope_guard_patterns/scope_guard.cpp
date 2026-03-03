// Scope Guard 作用域守卫示例
// 演示如何使用作用域守卫确保清理代码在作用域结束时执行

#include <utility>
#include <cstdio>
#include <cstdlib>

// ========== 基础 ScopeGuard 实现 ==========

template <typename F>
class ScopeGuard {
public:
    explicit ScopeGuard(F&& f) noexcept
        : func_(std::move(f)), active_(true) {}

    ~ScopeGuard() noexcept {
        if (active_) {
            func_();
        }
    }

    // 不允许拷贝（避免重复调用）
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

    // 允许移动
    ScopeGuard(ScopeGuard&& other) noexcept
        : func_(std::move(other.func_)), active_(other.active_) {
        other.dismiss();
    }

    void dismiss() noexcept { active_ = false; }

private:
    F func_;
    bool active_;
};

template <typename F>
ScopeGuard<typename std::decay<F>::type> make_scope_guard(F&& f) {
    return ScopeGuard<typename std::decay<F>::type>(std::forward<F>(f));
}

// ========== 辅助宏 ==========

#define CONCAT_IMPL(x, y) x##y
#define CONCAT(x, y) CONCAT_IMPL(x, y)
#define SCOPE_GUARD(code) \
    auto CONCAT(_scope_guard_, __COUNTER__) = make_scope_guard([&](){ code; })

// ========== 资源管理模拟 ==========

namespace res {
    inline void lock() {
        printf("[Resource] Locked\n");
    }

    inline void unlock() {
        printf("[Resource] Unlocked\n");
    }

    inline void acquire() {
        printf("[Resource] Acquired\n");
    }

    inline void release() {
        printf("[Resource] Released\n");
    }

    inline void enable_irq() {
        printf("[IRQ] Enabled\n");
    }

    inline void disable_irq() {
        printf("[IRQ] Disabled\n");
    }
}

// ========== 使用示例 ==========

void basic_example() {
    printf("=== Basic ScopeGuard Example ===\n");

    res::acquire();
    auto guard = make_scope_guard([]{ res::release(); });

    printf("Doing work...\n");

    // 任何 return 都会触发 guard
    if (true) {
        printf("Early return\n");
        return;
    }

    guard.dismiss();  // 不会到达这里
}

void multiple_guards() {
    printf("\n=== Multiple Guards Example ===\n");

    res::acquire();
    auto release_guard = make_scope_guard([]{ res::release(); });

    res::lock();
    auto unlock_guard = make_scope_guard([]{ res::unlock(); });

    printf("Critical section...\n");

    // 以相反顺序销毁（RAII 特性）
}

void dismiss_example() {
    printf("\n=== Dismiss Example ===\n");

    res::acquire();
    auto guard = make_scope_guard([]{ res::release(); });

    printf("Operation succeeded\n");

    guard.dismiss();  // 取消清理
    printf("Guard dismissed\n");
}

void macro_example() {
    printf("\n=== Macro Example ===\n");

    res::lock();
    SCOPE_GUARD({ res::unlock(); });

    printf("In critical section (using macro)\n");
}

// ========== 实际应用场景 ==========

void register_restore_example() {
    printf("\n=== Register Restore Example ===\n");

    // 模拟寄存器
    static uint32_t REG_CTRL = 0x1000;
    uint32_t old_value = REG_CTRL;

    SCOPE_GUARD({ REG_CTRL = old_value; });

    printf("Old register value: 0x%X\n", old_value);

    // 修改寄存器
    REG_CTRL = 0x2000;
    printf("New register value: 0x%X\n", REG_CTRL);

    printf("Doing work with modified register\n");

    // 离开作用域时自动恢复
    printf("After scope, register restored to: 0x%X\n", REG_CTRL);
}

void irq_save_restore_example() {
    printf("\n=== IRQ Save/Restore Example ===\n");

    // 模拟中断状态
    static bool irq_enabled = true;
    bool saved_irq = irq_enabled;

    // 禁用中断
    irq_enabled = false;
    printf("IRQ disabled\n");

    SCOPE_GUARD({ irq_enabled = saved_irq; printf("IRQ restored\n"); });

    printf("In critical section with IRQ disabled\n");

    // 离开作用域时自动恢复
}

// ========== 异常分支（概念示例）==========

// 注意：如果项目禁用异常，这些示例主要用于说明概念
// 在实际嵌入式代码中，通常用返回值表示错误

void error_handling_example() {
    printf("\n=== Error Handling Example ===\n");

    res::acquire();
    auto cleanup = make_scope_guard([]{ res::release(); });

    bool operation_failed = false;

    if (operation_failed) {
        printf("Operation failed, cleanup will execute\n");
        return;  // cleanup 仍会执行
    }

    printf("Operation succeeded\n");
    cleanup.dismiss();  // 成功时取消清理
}

// ========== 嵌套作用域 ==========

void nested_scope_example() {
    printf("\n=== Nested Scope Example ===\n");

    auto outer = make_scope_guard([]{ printf("Outer cleanup\n"); });

    {
        auto inner = make_scope_guard([]{ printf("Inner cleanup\n"); });
        printf("Inside nested scope\n");
    }

    printf("Back in outer scope\n");
}

// ========== 条件性清理 ==========

void conditional_cleanup_example() {
    printf("\n=== Conditional Cleanup Example ===\n");

    bool transaction_active = true;
    auto rollback = make_scope_guard([]{
        printf("Transaction rolled back\n");
    });

    printf("Transaction started\n");

    // 模拟成功
    bool success = true;

    if (success) {
        printf("Transaction committing\n");
        rollback.dismiss();  // 成功，不需要回滚
    }
}

// ========== 状态切换示例 ==========

class StateMachine {
public:
    enum State { Idle, Busy, Error };

    void enter_busy() {
        old_state_ = current_state_;
        current_state_ = Busy;
        printf("State: %s -> Busy\n", to_string(old_state_));

        // 确保退出时恢复状态
        SCOPE_GUARD({ current_state_ = old_state_;
                     printf("State restored to %s\n", to_string(current_state_)); });

        printf("Doing busy work...\n");
        // 离开作用域时自动恢复
    }

private:
    State current_state_ = Idle;
    State old_state_;

    const char* to_string(State s) {
        switch (s) {
            case Idle: return "Idle";
            case Busy: return "Busy";
            case Error: return "Error";
        }
        return "Unknown";
    }
};

void state_machine_example() {
    printf("\n=== State Machine Example ===\n");

    StateMachine sm;
    sm.enter_busy();
}

int main() {
    basic_example();
    multiple_guards();
    dismiss_example();
    macro_example();
    register_restore_example();
    irq_save_restore_example();
    error_handling_example();
    nested_scope_example();
    conditional_cleanup_example();
    state_machine_example();

    printf("\n=== All Examples Complete ===\n");
    printf("\nKey Takeaways:\n");
    printf("- ScopeGuard ensures cleanup code runs on all exit paths\n");
    printf("- No heap allocation, suitable for embedded\n");
    printf("- dismiss() allows cancelling cleanup when not needed\n");
    printf("- Guards execute in reverse order of construction (RAII)\n");

    return 0;
}
