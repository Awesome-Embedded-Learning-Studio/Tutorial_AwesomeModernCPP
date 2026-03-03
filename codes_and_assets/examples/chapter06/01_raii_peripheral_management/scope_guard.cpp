// Scope Guard 示例 - 通用的作用域守卫实现
// 演示如何使用 RAII 确保清理代码在作用域结束时执行

#include <utility>
#include <cstdio>
#include <cstdlib>

// 通用 ScopeGuard 实现
template <typename F>
class ScopeExit {
public:
    explicit ScopeExit(F f) noexcept : func_(std::move(f)), active_(true) {}

    ~ScopeExit() noexcept {
        if (active_) func_();
    }

    ScopeExit(ScopeExit&& o) noexcept
        : func_(std::move(o.func_)), active_(o.active_) {
        o.active_ = false;
    }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

    void dismiss() noexcept { active_ = false; }

private:
    F func_;
    bool active_;
};

template <typename F>
ScopeExit<F> make_scope_exit(F f) noexcept {
    return ScopeExit<F>(std::move(f));
}

// 模拟资源管理 API
namespace res {
    inline void lock() {
        printf("[RES] Resource locked\n");
    }

    inline void unlock() {
        printf("[RES] Resource unlocked\n");
    }

    inline void acquire() {
        printf("[RES] Resource acquired\n");
    }

    inline void release() {
        printf("[RES] Resource released\n");
    }
}

// 使用示例 1: 确保解锁
void critical_section_example() {
    printf("=== Critical Section Example ===\n");
    res::lock();
    auto unlock_guard = make_scope_exit([]{ res::unlock(); });

    // 临界区操作
    printf("In critical section...\n");

    // 多个 return 路径
    bool success = true;
    if (success) {
        printf("Operation successful\n");
        return;  // unlock_guard 仍会执行！
    }

    // 不会到达这里
    unlock_guard.dismiss();
}

// 使用示例 2: 多资源管理
void multi_resource_example() {
    printf("\n=== Multi-Resource Example ===\n");

    res::acquire();
    auto release_guard = make_scope_exit([]{ res::release(); });

    res::lock();
    auto unlock_guard = make_scope_exit([]{ res::unlock(); });

    printf("Working with resources...\n");

    // 任何地方的 return 都会正确清理
    if (true) {
        printf("Early return\n");
        return;
    }

    // dismiss 取消清理（成功时）
    unlock_guard.dismiss();
    release_guard.dismiss();
}

// 使用示例 3: 嵌套守卫
void nested_guards_example() {
    printf("\n=== Nested Guards Example ===\n");

    auto outer = make_scope_exit([]{ printf("Outer cleanup\n"); });

    {
        auto inner = make_scope_exit([]{ printf("Inner cleanup\n"); });
        printf("Inside nested scope\n");
    }

    printf("Back in outer scope\n");
}

// 使用示例 4: 条件性 dismiss
void conditional_dismiss_example() {
    printf("\n=== Conditional Dismiss Example ===\n");

    auto guard = make_scope_exit([]{ printf("Cleanup executed\n"); });

    bool operation_succeeded = true;

    if (operation_succeeded) {
        printf("Operation succeeded, dismissing cleanup\n");
        guard.dismiss();
    } else {
        printf("Operation failed, cleanup will execute\n");
    }
}

int main() {
    printf("=== Scope Guard Examples ===\n\n");

    critical_section_example();

    printf("\n---\n");

    multi_resource_example();

    printf("\n---\n");

    nested_guards_example();

    printf("\n---\n");

    conditional_dismiss_example();

    printf("\n=== All Examples Complete ===\n");
    printf("Notice how cleanup code executes reliably on all exit paths.\n");

    return 0;
}
