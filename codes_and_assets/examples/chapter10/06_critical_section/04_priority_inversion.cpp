// 优先级反转演示 - 模拟 RTOS 中的优先级反转问题
// 本示例展示了优先级反转现象及其解决方案

#include <cstdint>
#include <cstdio>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>
#include <queue>
#include <functional>
#include <utility>

// ============================================================================
// 模拟 RTOS 任务调度
// ============================================================================

namespace rtos_simulation {
    // 模拟任务优先级（数值越小优先级越高）
    enum class Priority : int {
        High   = 1,
        Medium = 2,
        Low    = 3
    };

    const char* priority_name(Priority p) {
        switch (p) {
            case Priority::High:   return "高";
            case Priority::Medium: return "中";
            case Priority::Low:    return "低";
        }
        return "未知";
    }

    // 模拟任务
    class Task {
    public:
        enum class State { Ready, Running, Finished };

        Task(const char* name, Priority priority)
            : name_(name)
            , priority_(priority)
            , state_(State::Ready)
        {}

        void run() {
            state_ = State::Running;
            printf("[%s任务] 开始执行 (优先级: %s)\n",
                priority_name(priority_), name_);

            if (task_func_) {
                task_func_();
            }

            state_ = State::Finished;
            printf("[%s任务] 完成\n", priority_name(priority_));
        }

        void set_task_func(std::function<void()> func) {
            task_func_ = std::move(func);
        }

        const char* name() const { return name_; }
        Priority priority() const { return priority_; }
        State state() const { return state_; }

        void sleep_ms(int ms) {
            printf("[%s任务] 睡眠 %d ms\n", priority_name(priority_), ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }

    private:

        const char* name_;
        Priority priority_;
        State state_;
        std::function<void()> task_func_;
    };

    // 简单的调度器模拟
    class SimpleScheduler {
    public:
        void add_task(std::unique_ptr<Task> task) {
            tasks_.push(std::move(task));
        }

        void run() {
            while (!tasks_.empty()) {
                auto task = std::move(tasks_.front());
                tasks_.pop();

                // 创建线程运行任务
                std::thread thread([task = std::move(task)]() mutable {
                    task->run();
                });
                thread.join();
            }
        }

    private:
        // 优先级队列（简单的 FIFO 实现）
        std::queue<std::unique_ptr<Task>> tasks_;
    };
}

// ============================================================================
// 问题演示：优先级反转
// ============================================================================

namespace priority_inversion_demo {
    using namespace rtos_simulation;

    std::mutex shared_resource;
    int resource_data = 0;

    void demonstrate() {
        printf("\n=== 问题演示：优先级反转 ===\n");
        printf("场景：低优先级任务持有锁，高优先级任务等待锁\n");
        printf("      中优先级任务抢占低优先级任务\n\n");

        std::atomic<bool> low_has_lock{false};
        std::atomic<bool> high_waiting{false};
        std::atomic<bool> medium_done{false};

        // 低优先级任务
        auto low_task = std::thread([&]() {
            printf("[低任务] 尝试获取共享资源...\n");
            shared_resource.lock();
            printf("[低任务] 获取资源成功\n");
            low_has_lock = true;

            // 持有锁期间进行耗时操作
            printf("[低任务] 处理资源中...\n");
            for (int i = 0; i < 5; ++i) {
                printf("[低任务] 仍在处理... (%d/5)\n", i + 1);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            printf("[低任务] 释放资源\n");
            low_has_lock = false;
            shared_resource.unlock();
        });

        // 给低任务一点时间先获取锁
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // 高优先级任务
        auto high_task = std::thread([&]() {
            printf("[高任务] 尝试获取共享资源...\n");
            high_waiting = true;

            // 等待低任务获取锁
            while (!low_has_lock) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            printf("[高任务] 等待资源...（被阻塞）\n");
            shared_resource.lock();  // 阻塞等待
            printf("[高任务] 获取资源成功！\n");
            high_waiting = false;

            printf("[高任务] 快速处理...\n");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            printf("[高任务] 释放资源\n");
            shared_resource.unlock();
        });

        // 给高任务一点时间开始等待
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // 中优先级任务（不使用共享资源）
        auto medium_task = std::thread([&]() {
            printf("[中任务] 开始执行（不需要共享资源）\n");
            printf("[中任务] 抢占了低任务！\n");

            for (int i = 0; i < 3; ++i) {
                printf("[中任务] 执行中... (%d/3)\n", i + 1);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            printf("[中任务] 完成\n");
            medium_done = true;
        });

        // 等待所有任务完成
        low_task.join();
        high_task.join();
        medium_task.join();

        printf("\n问题分析：\n");
        printf("- 高优先级任务被低优先级任务阻塞\n");
        printf("- 中优先级任务运行，延长了高优先级任务的等待时间\n");
        printf("- 高优先级任务的表现像是降低了优先级\n");
    }
}

// ============================================================================
// 解决方案1：优先级继承
// ============================================================================

namespace priority_inheritance_demo {
    using namespace rtos_simulation;

    // 支持优先级继承的互斥锁
    class PriorityInheritanceMutex {
    public:
        void lock(Priority caller_priority) {
            // 简化的模拟：记录当前持有者的优先级
            std::lock_guard<std::mutex> lock(internal_mtx_);
            owner_priority_ = caller_priority;
            printf("    [优先级继承] 锁被优先级 %s 的任务获取\n",
                priority_name(caller_priority));
        }

        void unlock() {
            std::lock_guard<std::mutex> lock(internal_mtx_);
            printf("    [优先级继承] 锁释放\n");
        }

    private:
        std::mutex internal_mtx_;
        Priority owner_priority_;
    };

    PriorityInheritanceMutex pi_mutex;
    std::atomic<bool> low_has_lock{false};
    std::atomic<bool> high_waiting{false};

    void demonstrate() {
        printf("\n=== 解决方案1：优先级继承 ===\n");
        printf("当高优先级任务等待低优先级任务持有的锁时\n");
        printf("低优先级任务临时提升到高优先级\n\n");

        // 低优先级任务
        auto low_task = std::thread([&]() {
            printf("[低任务] 尝试获取资源...\n");
            pi_mutex.lock(Priority::Low);
            low_has_lock = true;
            printf("[低任务] 获取资源，当前优先级提升到高\n");

            for (int i = 0; i < 5; ++i) {
                printf("[低任务] 处理中... (%d/5)\n", i + 1);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            printf("[低任务] 释放资源，优先级恢复\n");
            low_has_lock = false;
            pi_mutex.unlock();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // 高优先级任务
        auto high_task = std::thread([&]() {
            printf("[高任务] 尝试获取资源...\n");
            high_waiting = true;

            while (!low_has_lock) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            printf("[高任务] 等待资源...\n");
            printf("[高任务] 触发优先级继承：低任务被提升\n");
            pi_mutex.lock(Priority::High);

            printf("[高任务] 获取资源成功！\n");
            high_waiting = false;

            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            printf("[高任务] 释放资源\n");
            pi_mutex.unlock();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // 中优先级任务
        auto medium_task = std::thread([&]() {
            printf("[中任务] 尝试执行...\n");
            printf("[中任务] 但低任务现在以高优先级运行！\n");
            printf("[中任务] 被阻塞，等待低任务完成\n");

            // 等待高任务完成
            while (high_waiting) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            printf("[中任务] 现在可以执行了\n");
            for (int i = 0; i < 3; ++i) {
                printf("[中任务] 执行... (%d/3)\n", i + 1);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });

        low_task.join();
        high_task.join();
        medium_task.join();

        printf("\n解决方案效果：\n");
        printf("- 高优先级任务的等待时间减少了\n");
        printf("- 低优先级任务临时提升了优先级\n");
        printf("- 中优先级任务不能抢占提升后的低任务\n");
    }
}

// ============================================================================
// 解决方案2：优先级天花板
// ============================================================================

namespace priority_ceiling_demo {
    using namespace rtos_simulation;

    // 优先级天花板协议
    class PriorityCeilingMutex {
    public:
        explicit PriorityCeilingMutex(Priority ceiling)
            : ceiling_(ceiling)
            , locked_(false)
        {}

        bool lock(Priority caller_priority) {
            std::lock_guard<std::mutex> lock(internal_mtx_);

            if (locked_) {
                printf("    [天花板] 锁已被占用，等待...\n");
                return false;
            }

            if (caller_priority > ceiling_) {
                printf("    [天花板] 警告：调用者优先级低于天花板！\n");
            }

            locked_ = true;
            owner_priority_ = caller_priority;
            current_priority_ = ceiling_;  // 提升到天花板

            printf("    [天花板] 锁获取，优先级提升到 %s\n",
                priority_name(current_priority_));
            return true;
        }

        void unlock() {
            std::lock_guard<std::mutex> lock(internal_mtx_);
            locked_ = false;
            printf("    [天花板] 锁释放\n");
        }

    private:
        Priority ceiling_;
        Priority owner_priority_;
        Priority current_priority_;
        bool locked_;
        std::mutex internal_mtx_;
    };

    void demonstrate() {
        printf("\n=== 解决方案2：优先级天花板协议 ===\n");
        printf("每个锁有一个\"天花板\"优先级\n");
        printf("获取锁时，任务立即提升到天花板优先级\n\n");

        // 天花板设置为高优先级（因为高任务会使用这个锁）
        PriorityCeilingMutex ceiling_mutex(Priority::High);

        // 低优先级任务
        auto low_task = std::thread([&]() {
            printf("[低任务] 尝试获取资源...\n");
            ceiling_mutex.lock(Priority::Low);
            printf("[低任务] 获取资源，优先级已是天花板（高）\n");

            for (int i = 0; i < 5; ++i) {
                printf("[低任务] 处理... (%d/5)\n", i + 1);
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
            }

            printf("[低任务] 释放资源\n");
            ceiling_mutex.unlock();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // 高优先级任务
        auto high_task = std::thread([&]() {
            printf("[高任务] 尝试获取资源...\n");
            while (true) {
                if (ceiling_mutex.lock(Priority::High)) {
                    printf("[高任务] 获取资源成功！\n");
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    printf("[高任务] 释放资源\n");
                    ceiling_mutex.unlock();
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // 中优先级任务
        auto medium_task = std::thread([&]() {
            printf("[中任务] 尝试执行...\n");
            printf("[中任务] 但持有锁的任务以天花板优先级运行！\n");
            printf("[中任务] 不能抢占\n");

            for (int i = 0; i < 3; ++i) {
                printf("[中任务] 等待... (%d/3)\n", i + 1);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });

        low_task.join();
        high_task.join();
        medium_task.join();

        printf("\n解决方案效果：\n");
        printf("- 任务获取锁时立即提升到天花板优先级\n");
        printf("- 避免了中优先级任务的抢占\n");
        printf("- 需要预先确定每个锁的天花板优先级\n");
    }
}

// ============================================================================
// 实际RTOS示例
// ============================================================================

namespace rtos_example {
    void demonstrate_freertos() {
        printf("\n=== 实际 RTOS 支持：FreeRTOS ===\n");

        printf("FreeRTOS 提供的互斥量支持优先级继承：\n\n");

        printf("// 创建支持优先级继承的互斥量\n");
        printf("SemaphoreHandle_t mutex = xSemaphoreCreateMutex();\n");
        printf("// FreeRTOS 的互斥量默认启用优先级继承\n\n");

        printf("// 使用示例\n");
        printf("void task(void* pvParameters) {\n");
        printf("    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {\n");
        printf("        // 临界区\n");
        printf("        // 如果高优先级任务等待，当前任务会被提升\n");
        printf("        xSemaphoreGive(mutex);\n");
        printf("    }\n");
        printf("}\n\n");
    }

    void demonstrate_zephyr() {
        printf("\n=== 实际 RTOS 支持：Zephyr ===\n");

        printf("Zephyr 的互斥量默认支持优先级继承：\n\n");

        printf("// 定义互斥量\n");
        printf("K_MUTEX_DEFINE(my_mutex);\n\n");

        printf("// 使用示例\n");
        printf("void task() {\n");
        printf("    k_mutex_lock(&my_mutex, K_FOREVER);\n");
        printf("    // 临界区\n");
        printf("    k_mutex_unlock(&my_mutex);\n");
        printf("}\n\n");
    }

    void demonstrate_rtt_thread() {
        printf("\n=== 实际 RTOS 支持：RT-Thread ===\n");

        printf("RT-Thread 的互斥量支持优先级继承：\n\n");

        printf("// 创建互斥量\n");
        printf("static rt_mutex_t dynamic_mutex = RT_NULL;\n");
        printf("dynamic_mutex = rt_mutex_create(\"dmutex\", RT_IPC_FLAG_PRIO);\n\n");

        printf("// 使用示例\n");
        printf("void thread_entry(void* parameter) {\n");
        printf("    while (1) {\n");
        printf("        if (rt_mutex_take(dynamic_mutex, RT_WAITING_FOREVER) == RT_EOK) {\n");
        printf("            // 临界区\n");
        printf("            rt_mutex_release(dynamic_mutex);\n");
        printf("        }\n");
        printf("    }\n");
        printf("}\n\n");
    }
}

// ============================================================================
// 避免优先级反转的最佳实践
// ============================================================================

namespace best_practices {
    void demonstrate() {
        printf("\n=== 避免优先级反转的最佳实践 ===\n\n");

        printf("1. 使用支持优先级继承/天花板的 RTOS\n");
        printf("   - FreeRTOS: 互斥量默认启用\n");
        printf("   - Zephyr: 互斥量默认启用\n");
        printf("   - RT-Thread: 创建时指定 RT_IPC_FLAG_PRIO\n\n");

        printf("2. 临界区尽可能短\n");
        printf("   - 减少持有锁的时间\n");
        printf("   - 降低优先级反转发生的概率\n\n");

        printf("3. 避免嵌套锁\n");
        printf("   - 嵌套锁增加优先级反转的复杂性\n");
        printf("   - 如果必须嵌套，确保 RTOS 正确处理\n\n");

        printf("4. 考虑使用无锁数据结构\n");
        printf("   - 环形缓冲区\n");
        printf("   - 原子操作\n");
        printf("   - SPSC 队列\n\n");

        printf("5. 对于 ISR-任务通信\n");
        printf("   - 使用队列或信号量\n");
        printf("   - 避免在 ISR 中使用互斥量\n\n");
    }
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    printf("========================================\n");
    printf("  优先级反转演示\n");
    printf("========================================\n");

    printf("优先级反转是多任务系统中的经典问题。\n");
    printf("它发生在：\n");
    printf("  - 低优先级任务持有锁\n");
    printf("  - 高优先级任务等待该锁\n");
    printf("  - 中优先级任务抢占低优先级任务\n");
    printf("结果：高优先级任务被中优先级任务间接阻塞\n");

    priority_inversion_demo::demonstrate();
    priority_inheritance_demo::demonstrate();
    priority_ceiling_demo::demonstrate();
    rtos_example::demonstrate_freertos();
    rtos_example::demonstrate_zephyr();
    rtos_example::demonstrate_rtt_thread();
    best_practices::demonstrate();

    printf("\n=== 总结 ===\n");
    printf("1. 优先级反转：高优先级任务被低优先级阻塞\n");
    printf("2. 解决方案1：优先级继承（动态提升）\n");
    printf("3. 解决方案2：优先级天花板（静态提升）\n");
    printf("4. 主流RTOS都支持这些机制\n");
    printf("5. 设计时应尽量减少临界区和锁的使用\n");

    return 0;
}

// 编译命令：
// g++ -std=c++17 -Wall -Wextra -pthread 04_priority_inversion.cpp -o 04_priority_inversion
