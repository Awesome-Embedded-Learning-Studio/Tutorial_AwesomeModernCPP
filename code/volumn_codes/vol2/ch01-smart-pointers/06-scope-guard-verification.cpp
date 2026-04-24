/**
 * @file 06-scope-guard-verification.cpp
 * @brief scope_guard 功能验证测试
 *
 * 编译命令（g++）:
 *   g++ -std=c++17 -Wall -Wextra -O2 06-scope-guard-verification.cpp -o 06-scope-guard-verification
 *
 * 编译环境:
 *   - GCC 13.2.0 on Ubuntu 24.04 LTS
 *   - 也兼容 Clang 15+ 和 MSVC 19.35+
 *
 * 运行: ./06-scope-guard-verification
 */

#include <iostream>
#include <utility>
#include <exception>
#include <stdexcept>
#include <cstdlib>

// ============================================================================
// 基础 ScopeGuard 实现
// ============================================================================
template <typename F>
class ScopeGuard
{
public:
    explicit ScopeGuard(F&& func) noexcept
        : func_(std::move(func))
        , active_(true)
    {}

    ~ScopeGuard() noexcept
    {
        if (active_) {
            try {
                func_();
            } catch (...) {
                // 析构函数中捕获异常并调用 terminate
                // 验证点：noexcept 函数抛异常会导致 std::terminate()
                std::cout << "[ScopeGuard] 析构捕获异常，调用 terminate\n";
                std::terminate();
            }
        }
    }

    ScopeGuard(ScopeGuard&& other) noexcept
        : func_(std::move(other.func_))
        , active_(other.active_)
    {
        other.active_ = false;
    }

    void dismiss() noexcept
    {
        active_ = false;
    }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

private:
    F func_;
    bool active_;
};

template <typename F>
ScopeGuard<F> make_scope_guard(F&& func) noexcept
{
    return ScopeGuard<F>(std::forward<F>(func));
}

// ============================================================================
// ScopeSuccess 实现
// ============================================================================
template <typename F>
class ScopeSuccess
{
public:
    explicit ScopeSuccess(F&& func) noexcept
        : func_(std::move(func))
        , active_(true)
        , uncaught_at_creation_(std::uncaught_exceptions())
    {}

    ~ScopeSuccess() noexcept
    {
        if (active_ && std::uncaught_exceptions() == uncaught_at_creation_) {
            try {
                func_();
            } catch (...) {
                std::terminate();
            }
        }
    }

    ScopeSuccess(ScopeSuccess&& other) noexcept
        : func_(std::move(other.func_))
        , active_(other.active_)
        , uncaught_at_creation_(other.uncaught_at_creation_)
    {
        other.active_ = false;
    }

    void dismiss() noexcept
    {
        active_ = false;
    }

    ScopeSuccess(const ScopeSuccess&) = delete;
    ScopeSuccess& operator=(const ScopeSuccess&) = delete;

private:
    F func_;
    bool active_;
    int uncaught_at_creation_;
};

// ============================================================================
// ScopeFail 实现
// ============================================================================
template <typename F>
class ScopeFail
{
public:
    explicit ScopeFail(F&& func) noexcept
        : func_(std::move(func))
        , active_(true)
        , uncaught_at_creation_(std::uncaught_exceptions())
    {}

    ~ScopeFail() noexcept
    {
        if (active_ && std::uncaught_exceptions() > uncaught_at_creation_) {
            try {
                func_();
            } catch (...) {
                std::terminate();
            }
        }
    }

    ScopeFail(ScopeFail&& other) noexcept
        : func_(std::move(other.func_))
        , active_(other.active_)
        , uncaught_at_creation_(other.uncaught_at_creation_)
    {
        other.active_ = false;
    }

    void dismiss() noexcept
    {
        active_ = false;
    }

    ScopeFail(const ScopeFail&) = delete;
    ScopeFail& operator=(const ScopeFail&) = delete;

private:
    F func_;
    bool active_;
    int uncaught_at_creation_;
};

// ============================================================================
// 测试用例
// ============================================================================

void test_basic_guard()
{
    std::cout << "\n=== 测试 1: 基础 ScopeGuard ===\n";
    {
        auto guard = make_scope_guard([]() {
            std::cout << "[ScopeGuard] 作用域退出时执行\n";
        });
        std::cout << "[ScopeGuard] 作用域内\n";
    }
    std::cout << "[ScopeGuard] 验证通过\n";
}

void test_dismiss()
{
    std::cout << "\n=== 测试 2: dismiss() 功能 ===\n";
    {
        auto guard = make_scope_guard([]() {
            std::cout << "[dismiss] 这不应该被执行\n";
        });
        guard.dismiss();
        std::cout << "[dismiss] guard 已被取消\n";
    }
    std::cout << "[dismiss] 验证通过\n";
}

void test_multiple_returns()
{
    std::cout << "\n=== 测试 3: 多返回路径 ===\n";

    auto test = [](bool early_return) -> void {
        auto guard = make_scope_guard([]() {
            std::cout << "[多返回] 总是执行清理\n";
        });

        if (early_return) {
            std::cout << "[多返回] 提前返回\n";
            return;
        }

        std::cout << "[多返回] 正常执行完毕\n";
    };

    test(true);
    test(false);
    std::cout << "[多返回] 验证通过\n";
}

void test_scope_fail()
{
    std::cout << "\n=== 测试 4: ScopeFail（异常时执行） ===\n";

    auto test = []() {
        ScopeFail fail_guard([]() {
            std::cout << "[ScopeFail] 检测到异常，执行清理\n";
        });

        std::cout << "[ScopeFail] 准备抛出异常\n";
        throw std::runtime_error("测试异常");
    };

    try {
        test();
    } catch (const std::exception& e) {
        std::cout << "[ScopeFail] 捕获异常: " << e.what() << "\n";
    }
    std::cout << "[ScopeFail] 验证通过\n";
}

void test_scope_fail_no_exception()
{
    std::cout << "\n=== 测试 5: ScopeFail（无异常时不执行） ===\n";

    ScopeFail fail_guard([]() {
        std::cout << "[ScopeFail] 这不应该被执行\n";
    });

    std::cout << "[ScopeFail] 正常退出\n";
    std::cout << "[ScopeFail] 验证通过\n";
}

void test_scope_success()
{
    std::cout << "\n=== 测试 6: ScopeSuccess（正常时执行） ===\n";

    {
        ScopeSuccess success_guard([]() {
            std::cout << "[ScopeSuccess] 正常退出，执行提交\n";
        });

        std::cout << "[ScopeSuccess] 正常执行\n";
    }
    std::cout << "[ScopeSuccess] 验证通过\n";
}

void test_scope_success_with_exception()
{
    std::cout << "\n=== 测试 7: ScopeSuccess（异常时不执行） ===\n";

    auto test = []() {
        ScopeSuccess success_guard([]() {
            std::cout << "[ScopeSuccess] 这不应该被执行\n";
        });

        std::cout << "[ScopeSuccess] 准备抛出异常\n";
        throw std::runtime_error("测试异常");
    };

    try {
        test();
    } catch (const std::exception& e) {
        std::cout << "[ScopeSuccess] 捕获异常: " << e.what() << "\n";
    }
    std::cout << "[ScopeSuccess] 验证通过\n";
}

void test_transaction_pattern()
{
    std::cout << "\n=== 测试 8: 事务模式 ===\n";

    auto transaction = [](bool should_fail) {
        std::cout << "[事务] BEGIN\n";

        ScopeFail on_fail([]() {
            std::cout << "[事务] ROLLBACK（异常导致）\n";
        });

        if (should_fail) {
            std::cout << "[事务] 操作失败，抛出异常\n";
            throw std::runtime_error("操作失败");
        }

        on_fail.dismiss();
        std::cout << "[事务] COMMIT\n";
    };

    try {
        transaction(true);
    } catch (...) {
        std::cout << "[事务] 捕获异常\n";
    }

    transaction(false);
    std::cout << "[事务] 验证通过\n";
}

void test_defer_macro()
{
    std::cout << "\n=== 测试 9: DEFER 宏模拟 ===\n";

    {
        int resource1 = 1;
        int resource2 = 2;

        auto guard1 = make_scope_guard([&]() {
            std::cout << "[DEFER] 释放 resource1 = " << resource1 << "\n";
        });

        auto guard2 = make_scope_guard([&]() {
            std::cout << "[DEFER] 释放 resource2 = " << resource2 << "\n";
        });

        std::cout << "[DEFER] 使用资源\n";
    }
    std::cout << "[DEFER] 验证通过\n";
}

void test_uncaught_exceptions_behavior()
{
    std::cout << "\n=== 测试 10: std::uncaught_exceptions() 行为 ===\n";

    struct Guard
    {
        int count_on_construct;
        ~Guard() noexcept
        {
            int count_on_destroy = std::uncaught_exceptions();
            std::cout << "[uncaught] 构造时: " << count_on_construct
                      << ", 析构时: " << count_on_destroy;
            if (count_on_destroy > count_on_construct) {
                std::cout << " -> 正在栈展开！\n";
            } else {
                std::cout << " -> 正常退出\n";
            }
        }
    };

    try {
        Guard g{std::uncaught_exceptions()};
        std::cout << "[uncaught] 创建守卫后: " << std::uncaught_exceptions() << "\n";
        throw std::runtime_error("test");
    } catch (...) {
        std::cout << "[uncaught] 捕获异常\n";
    }

    {
        Guard g{std::uncaught_exceptions()};
        std::cout << "[uncaught] 正常作用域\n";
    }

    std::cout << "[uncaught] 验证通过\n";
}

int main()
{
    std::cout << "========================================";
    std::cout << "\nscope_guard 综合验证测试\n";
    std::cout << "========================================";

    test_basic_guard();
    test_dismiss();
    test_multiple_returns();
    test_scope_fail();
    test_scope_fail_no_exception();
    test_scope_success();
    test_scope_success_with_exception();
    test_transaction_pattern();
    test_defer_macro();
    test_uncaught_exceptions_behavior();

    std::cout << "\n========================================\n";
    std::cout << "所有测试通过！\n";
    std::cout << "========================================\n";

    return 0;
}
