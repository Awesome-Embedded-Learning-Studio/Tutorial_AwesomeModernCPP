// RAII 临界区守卫 - 演示如何使用 RAII 模式管理临界区
// 本示例展示了各种 RAII 封装，确保异常安全和自动释放

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <atomic>

// ============================================================================
// 问题演示：手动管理的风险
// ============================================================================

namespace manual_management {
    int shared_data = 0;
    std::mutex mtx;

    // ❌ 危险：手动 lock/unlock
    void bad_function() {
        mtx.lock();
        shared_data++;

        // 如果这里抛异常，unlock 永远不会执行！
        // throw std::runtime_error("Something went wrong");

        mtx.unlock();
    }

    // ❌ 更糟：多个返回路径
    void worse_function(int condition) {
        mtx.lock();
        shared_data++;

        if (condition == 0) {
            mtx.unlock();
            return;  // 记得解锁
        } else if (condition == 1) {
            // 忘记解锁了！
            return;
        }

        shared_data++;
        mtx.unlock();
    }

    void demonstrate() {
        printf("\n=== 问题：手动管理的风险 ===\n");
        printf("手动 lock/unlock 容易出错：\n");
        printf("1. 异常跳出会导致死锁\n");
        printf("2. 多个返回路径容易忘记解锁\n");
        printf("3. 代码复杂时难以追踪锁的状态\n");
    }
}

// ============================================================================
// 解决方案：RAII 锁守卫
// ============================================================================

namespace raii_solution {
    int shared_data = 0;
    std::mutex mtx;

    // ✅ 好：使用 lock_guard
    void good_function() {
        std::lock_guard<std::mutex> lock(mtx);  // 构造时自动 lock
        shared_data++;
        // 无论怎么退出（return、异常），lock 析构时都会自动 unlock
    }

    // ✅ 更简洁
    void another_good_function(int condition) {
        std::lock_guard<std::mutex> lock(mtx);
        shared_data++;

        if (condition == 0) {
            return;  // 自动解锁
        } else if (condition == 1) {
            return;  // 自动解锁
        }

        shared_data++;
        // 自动解锁
    }

    void demonstrate() {
        printf("\n=== 解决方案：RAII 锁守卫 ===\n");

        good_function();
        another_good_function(0);
        another_good_function(1);

        printf("shared_data = %d\n", shared_data);
        printf("RAII 确保锁一定会被释放！\n");
    }
}

// ============================================================================
// 自定义 RAII 封装
// ============================================================================

namespace custom_raii {
    // 模拟中断控制
    std::atomic<int> irq_disable_depth{0};

    inline uint32_t disable_irq() {
        printf("  [IRQ] 禁用中断 (深度: %d)\n", irq_disable_depth.load() + 1);
        return irq_disable_depth.fetch_add(1, std::memory_order_relaxed);
    }

    inline void enable_irq(uint32_t) {
        int depth = irq_disable_depth.fetch_sub(1, std::memory_order_relaxed);
        printf("  [IRQ] 恢复中断 (深度: %d)\n", depth - 1);
    }

    // 自定义中断守卫
    class InterruptGuard {
    public:
        InterruptGuard() noexcept
            : state_(disable_irq())
        {
            printf("  InterruptGuard: 进入临界区\n");
        }

        ~InterruptGuard() noexcept {
            printf("  InterruptGuard: 离开临界区\n");
            enable_irq(state_);
        }

        // 禁止拷贝和移动
        InterruptGuard(const InterruptGuard&) = delete;
        InterruptGuard& operator=(const InterruptGuard&) = delete;

    private:
        uint32_t state_;
    };

    // 模拟的临界区操作
    void critical_operation() {
        InterruptGuard guard;
        // 临界区代码
        printf("    执行临界区操作...\n");
    }

    void demonstrate() {
        printf("\n=== 自定义 RAII：中断守卫 ===\n");

        critical_operation();
        printf("\n演示嵌套临界区：\n");

        InterruptGuard outer;
        printf("  外层临界区\n");
        {
            InterruptGuard inner;
            printf("    内层临界区\n");
        }
        printf("  回到外层临界区\n");
    }
}

// ============================================================================
// 各种 RAII 封装对比
// ============================================================================

namespace raii_comparison {
    std::mutex mtx;

    void demonstrate_lock_guard() {
        printf("\n--- std::lock_guard ---\n");
        printf("特点：简单、轻量、构造即加锁\n");
        printf("适用：大多数场景\n");

        std::lock_guard<std::mutex> lock(mtx);
        printf("  已加锁，离开作用域自动解锁\n");
    }

    void demonstrate_unique_lock() {
        printf("\n--- std::unique_lock ---\n");
        printf("特点：灵活、支持手动操作、可移动\n");
        printf("适用：需要条件变量或手动控制加锁时机\n");

        std::unique_lock<std::mutex> lock(mtx);
        printf("  已加锁\n");

        lock.unlock();
        printf("  手动解锁\n");

        lock.lock();
        printf("  重新加锁\n");
    }

    void demonstrate_defer_lock() {
        printf("\n--- std::unique_lock with defer_lock ---\n");
        printf("特点：延迟加锁，条件性加锁\n");
        printf("适用：需要先创建锁对象，稍后才加锁\n");

        std::unique_lock<std::mutex> lock(mtx, std::defer_lock);
        printf("  已创建锁对象，但未加锁\n");

        // 根据条件决定是否加锁
        bool need_lock = true;
        if (need_lock) {
            lock.lock();
            printf("  现在加锁了\n");
        }
    }

    void demonstrate_scoped_lock() {
        printf("\n--- std::scoped_lock (C++17) ---\n");
        printf("特点：同时锁定多个互斥量，避免死锁\n");
        printf("适用：需要同时访问多个资源\n");

        std::mutex mtx1, mtx2;
        std::scoped_lock lock(mtx1, mtx2);
        printf("  同时锁住了两个互斥量\n");
    }
}

// ============================================================================
// 作用域守卫 (Scope Guard)
// ============================================================================

namespace scope_guard {
    // 简单的作用域守卫实现
    template<typename F>
    class ScopeGuard {
    public:
        explicit ScopeGuard(F&& f) noexcept
            : exit_function_(std::forward<F>(f))
            , active_(true)
        {}

        ~ScopeGuard() noexcept {
            if (active_) {
                exit_function_();
            }
        }

        ScopeGuard(ScopeGuard&& other) noexcept
            : exit_function_(std::move(other.exit_function_))
            , active_(other.active_)
        {
            other.active_ = false;
        }

        // 禁止拷贝
        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;

        void dismiss() noexcept {
            active_ = false;
        }

    private:
        F exit_function_;
        bool active_;
    };

    // 辅助函数
    template<typename F>
    ScopeGuard<typename std::decay<F>::type> make_guard(F&& f) {
        return ScopeGuard<typename std::decay<F>::type>(std::forward<F>(f));
    }

    // 使用示例
    void open_resource() {
        printf("  [资源] 打开资源\n");
    }

    void close_resource() {
        printf("  [资源] 关闭资源\n");
    }

    void process_with_exception() {
        printf("\n=== 作用域守卫示例 ===\n");
        printf("打开资源...\n");
        open_resource();

        // 创建守卫，确保资源被关闭
        auto guard = make_guard([&]() {
            close_resource();
            printf("  [守卫] 资源已自动关闭\n");
        });

        printf("处理资源...\n");

        // 可以取消守卫
        // guard.dismiss();

        // 即使抛异常，守卫也会执行
        // throw std::runtime_error("Error");

        printf("处理完成\n");
        // guard 离开作用域，自动关闭资源
    }

    void demonstrate_multiple_actions() {
        printf("\n=== 多个清理操作 ===\n");

        int fd1 = 1, fd2 = 2, fd3 = 3;
        printf("  打开了 3 个资源: %d, %d, %d\n", fd1, fd2, fd3);

        // 多个守卫按相反顺序执行（RAII 特性）
        auto guard3 = make_guard([&]() { printf("  关闭资源 3\n"); });
        auto guard2 = make_guard([&]() { printf("  关闭资源 2\n"); });
        auto guard1 = make_guard([&]() { printf("  关闭资源 1\n"); });

        printf("  使用资源中...\n");
        // 发生异常或正常返回，守卫都会按相反顺序执行
    }
}

// ============================================================================
// 实用示例：组合多种 RAII
// ============================================================================

namespace practical_example {
    struct Device {
        void open() { printf("  [设备] 打开\n"); }
        void close() { printf("  [设备] 关闭\n"); }
        void configure() { printf("  [设备] 配置\n"); }
        void start() { printf("  [设备] 启动\n"); }
        void stop() { printf("  [设备] 停止\n"); }
    };

    // 设备 RAII 包装
    class DeviceGuard {
    public:
        explicit DeviceGuard(Device& dev) noexcept : device_(dev), active_(true) {
            device_.open();
        }

        ~DeviceGuard() noexcept {
            if (active_) {
                device_.close();
            }
        }

        Device* operator->() noexcept { return &device_; }

    private:
        Device& device_;
        bool active_;
    };

    // 演示：复杂的初始化流程
    void complex_initialization() {
        printf("\n=== 实用示例：复杂初始化 ===\n");

        Device dev;
        std::mutex mtx;

        std::lock_guard<std::mutex> lock(mtx);  // 锁 1
        printf("已获取互斥锁\n");

        DeviceGuard dev_guard(dev);  // 资源 2
        dev_guard->configure();

        {
            // 临时需要额外的保护
            std::lock_guard<std::mutex> inner_lock(mtx);
            printf("  内层临界区\n");
        }

        dev_guard->start();
        // 各种异常可能发生的地方...
        dev_guard->stop();

        // 离开作用域，自动清理：
        // 1. DeviceGuard 关闭设备
        // 2. lock 释放互斥锁
    }
}

// ============================================================================
// 异常安全演示
// ============================================================================

namespace exception_safety {
    std::mutex mtx;
    int resource = 0;

    void risky_operation(bool should_fail) {
        if (should_fail) {
            throw std::runtime_error("操作失败！");
        }
    }

    void with_raii(bool should_fail) {
        printf("\n=== 异常安全演示 ===\n");

        try {
            std::lock_guard<std::mutex> lock(mtx);
            printf("  获取锁，执行操作\n");

            resource = 42;
            risky_operation(should_fail);

            printf("  操作成功\n");
        } catch (const std::exception& e) {
            printf("  捕获异常: %s\n", e.what());
            printf("  锁已自动释放，资源 = %d\n", resource);
        }
    }

    void demonstrate() {
        printf("使用 RAII 的版本：\n");
        with_raii(false);  // 成功
        with_raii(true);   // 失败

        printf("\n如果没有 RAII，异常后锁不会被释放！\n");
    }
}

// ============================================================================
// 性能考虑
// ============================================================================

namespace performance_considerations {
    void demonstrate_critical_section_size() {
        printf("\n=== 性能考虑：临界区大小 ===\n");

        std::mutex mtx;
        int shared_data = 0;

        // ❌ 不好：临界区太大
        auto bad_example = [&]() {
            std::lock_guard<std::mutex> lock(mtx);
            shared_data++;  // 只有这行需要锁

            // 这些操作不需要锁，浪费了锁的时间
            int result = 0;
            for (int i = 0; i < 1000; ++i) {
                result += i * i;
            }
        };

        // ✅ 好：只锁必要的部分
        auto good_example = [&]() {
            // 先计算，不需要锁
            int result = 0;
            for (int i = 0; i < 1000; ++i) {
                result += i * i;
            }

            // 只锁这一行
            std::lock_guard<std::mutex> lock(mtx);
            shared_data++;
        };

        printf("原则：临界区尽可能短\n");
        printf("  计算在锁外进行\n");
        printf("  只锁共享数据的访问\n");
    }
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    printf("========================================\n");
    printf("  RAII 临界区守卫演示\n");
    printf("========================================\n");

    manual_management::demonstrate();
    raii_solution::demonstrate();
    custom_raii::demonstrate();
    raii_comparison::demonstrate_lock_guard();
    raii_comparison::demonstrate_unique_lock();
    raii_comparison::demonstrate_defer_lock();
    raii_comparison::demonstrate_scoped_lock();
    scope_guard::process_with_exception();
    scope_guard::demonstrate_multiple_actions();
    practical_example::complex_initialization();
    exception_safety::demonstrate();
    performance_considerations::demonstrate_critical_section_size();

    printf("\n=== 总结 ===\n");
    printf("1. RAII 自动管理锁的生命周期\n");
    printf("2. 异常安全：即使抛异常也会正确释放\n");
    printf("3. 代码清晰：一看就知道临界区范围\n");
    printf("4. lock_guard：简单场景首选\n");
    printf("5. unique_lock：需要灵活控制时使用\n");
    printf("6. scoped_lock：多锁场景防死锁\n");
    printf("7. 自定义 RAII：用于特殊资源管理\n");

    return 0;
}

// 编译命令：
// g++ -std=c++17 -Wall -Wextra -pthread 02_raii_critical_section.cpp -o 02_raii_critical_section
