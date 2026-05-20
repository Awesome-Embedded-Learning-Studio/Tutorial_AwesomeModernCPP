/*
 * 演示：std::thread 的创建方式与基本生命周期管理
 *
 * 背景：文章 ch01 "线程生命周期与 RAII 管理" 涵盖——
 *       1. 函数指针、lambda、仿函数（functor）三种创建线程的方式
 *       2. join 与 detach 的语义差异
 *       3. std::thread::id 与 hardware_concurrency
 *       4. 线程中的异常安全：safe_worker 模式
 *
 * 预期结果：
 *   程序依次演示各种线程创建方式，输出各线程的 ID 与执行结果，
 *   并展示异常安全包装器的行为。
 *
 * 编译命令：
 *   cmake -B build && cmake --build build
 *   ./build/01_thread_creation
 *
 * 编译器：GCC 12+ | Clang 15+ | MSVC 19.3+
 * 平台：x86-64 Linux / macOS / Windows
 * C++ 标准：C++17
 */

#include <chrono>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// 用于线程安全输出的辅助互斥量
std::mutex g_cout_mutex;

// 线程安全的 printf 风格输出并非本例重点，
// 这里仅用互斥量保证各行不交错
void safe_print(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_cout_mutex);
    std::cout << msg << std::endl;
}

// ============================================================
// Demo 1: 函数指针创建线程
// ============================================================

void worker_function(int id, int iterations) {
    for (int i = 0; i < iterations; ++i) {
        safe_print("  [函数指针线程 " + std::to_string(id) + "] 迭代 " + std::to_string(i));
    }
    safe_print("  [函数指针线程 " + std::to_string(id) + "] 完成");
}

void demo_function_pointer() {
    safe_print("\n=== Demo 1: 函数指针创建线程 ===");

    // std::thread 的第一个参数是可调用对象，
    // 后续参数会按值传递给该可调用对象
    std::thread t1(worker_function, 1, 3);
    std::thread t2(worker_function, 2, 3);

    t1.join(); // 阻塞等待 t1 结束
    t2.join(); // 阻塞等待 t2 结束

    safe_print("  两个函数指针线程均已结束");
}

// ============================================================
// Demo 2: Lambda 创建线程
// ============================================================

void demo_lambda_thread() {
    safe_print("\n=== Demo 2: Lambda 创建线程 ===");

    int shared_value = 0;

    // lambda 捕获外部变量（按引用），启动线程执行
    // 注意：此处仅作演示，实际生产中需要同步机制保护 shared_value
    std::thread t([&shared_value]() {
        for (int i = 0; i < 5; ++i) {
            ++shared_value;
            safe_print("  [Lambda 线程] shared_value = " + std::to_string(shared_value));
        }
    });

    t.join();
    safe_print("  [主线程] 最终 shared_value = " + std::to_string(shared_value));
}

// ============================================================
// Demo 3: 仿函数（Functor）创建线程
// ============================================================

// 仿函数：重载 operator() 的类，可像函数一样调用
class TaskWorker {
  public:
    TaskWorker(std::string name, int count) : name_(std::move(name)), count_(count) {}

    void operator()() const {
        for (int i = 0; i < count_; ++i) {
            safe_print("  [" + name_ + "] 执行任务 " + std::to_string(i));
        }
        safe_print("  [" + name_ + "] 全部任务完成");
    }

  private:
    std::string name_;
    int count_;
};

void demo_functor_thread() {
    safe_print("\n=== Demo 3: 仿函数创建线程 ===");

    // 注意：C++ 最令人头疼的解析（most vexing parse）问题
    //   std::thread t(TaskWorker("worker", 3));
    // 会被编译器解释为函数声明，而非对象构造！
    // 解决方案：使用 {} 初始化或额外括号
    std::thread t{TaskWorker("FunctorWorker", 4)};

    t.join();
}

// ============================================================
// Demo 4: join 与 detach 演示
// ============================================================

void background_task(int id) {
    safe_print("  [detach 线程 " + std::to_string(id) + "] 开始后台工作");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    safe_print("  [detach 线程 " + std::to_string(id) + "] 后台工作完成");
}

void demo_join_vs_detach() {
    safe_print("\n=== Demo 4: join 与 detach 演示 ===");

    // join: 主线程阻塞等待子线程结束
    {
        std::thread t([]() { safe_print("  [join 线程] 正在工作..."); });
        safe_print("  [主线程] 等待 join 线程结束...");
        t.join();
        safe_print("  [主线程] join 线程已结束，继续执行");
    }

    // detach: 将线程与 std::thread 对象分离，线程在后台独立运行
    // 分离后 std::thread 对象不再管理该线程，joinable() 变为 false
    {
        std::thread t(background_task, 99);
        safe_print("  [主线程] detach 之前 joinable = " +
                   std::string(t.joinable() ? "true" : "false"));
        t.detach();
        safe_print("  [主线程] detach 之后 joinable = " +
                   std::string(t.joinable() ? "true" : "false"));
        safe_print("  [主线程] 已 detach，主线程继续执行");
    }

    // 等待 detach 线程完成，避免它在 main 结束后才输出
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

// ============================================================
// Demo 5: 线程 ID 与 hardware_concurrency
// ============================================================

void print_thread_info() {
    // std::this_thread::get_id() 获取当前线程的 ID
    std::thread::id tid = std::this_thread::get_id();
    safe_print("  [子线程] 我的 thread::id = " +
               std::to_string(*reinterpret_cast<const unsigned long*>(&tid)));
}

void demo_thread_id_and_hardware() {
    safe_print("\n=== Demo 5: 线程 ID 与 hardware_concurrency ===");

    // hardware_concurrency 返回硬件支持的并发线程数
    // 这是一个提示值，可能返回 0（无法检测时）
    unsigned int hw = std::thread::hardware_concurrency();
    safe_print("  hardware_concurrency = " + std::to_string(hw));

    // 主线程也有自己的 ID
    std::thread::id main_id = std::this_thread::get_id();
    safe_print("  [主线程] thread::id = " +
               std::to_string(*reinterpret_cast<const unsigned long*>(&main_id)));

    // 每个线程都有唯一的 ID
    std::thread t1(print_thread_info);
    std::thread t2(print_thread_info);
    t1.join();
    t2.join();
}

// ============================================================
// Demo 6: 异常安全 —— safe_worker 包装器
// ============================================================

// 线程函数中抛出异常会导致 std::terminate，
// 因为异常无法跨线程传播。
// safe_worker 模式：在进入线程前捕获异常，通过 std::exception_ptr
// 或 optional<error_code> 传回主线程。

// 简易版：捕获异常并打印，防止程序终止
template <typename Func> void safe_worker(Func&& func, const std::string& task_name) {
    try {
        std::forward<Func>(func)();
    } catch (const std::exception& e) {
        safe_print("  [safe_worker: " + task_name + "] 捕获异常: " + std::string(e.what()));
    } catch (...) {
        safe_print("  [safe_worker: " + task_name + "] 捕获未知异常");
    }
}

// 一个会抛出异常的工作函数
void faulty_worker(int value) {
    if (value < 0) {
        throw std::runtime_error("value 不能为负数: " + std::to_string(value));
    }
    safe_print("  [faulty_worker] 正常处理 value = " + std::to_string(value));
}

void demo_exception_safety() {
    safe_print("\n=== Demo 6: 异常安全 —— safe_worker 包装器 ===");

    // 正常调用：不抛异常
    std::thread t1([]() { safe_worker([]() { faulty_worker(42); }, "正常任务"); });

    // 异常调用：会被 safe_worker 捕获，不会导致 std::terminate
    std::thread t2([]() { safe_worker([]() { faulty_worker(-1); }, "异常任务"); });

    t1.join();
    t2.join();

    safe_print("  所有线程安全结束（异常被捕获，未导致 terminate）");
}

// ============================================================
// 主函数：依次运行所有 Demo
// ============================================================

int main() {
    std::cout << "===== ch01: 线程创建方式与基本生命周期 =====\n";

    demo_function_pointer();
    demo_lambda_thread();
    demo_functor_thread();
    demo_join_vs_detach();
    demo_thread_id_and_hardware();
    demo_exception_safety();

    std::cout << "\n===== 所有 Demo 运行完毕 =====" << std::endl;
    return 0;
}
