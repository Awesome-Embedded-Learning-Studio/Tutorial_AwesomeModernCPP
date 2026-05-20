/*
 * 演示：std::thread 的参数传递与生命周期陷阱
 *
 * 背景：文章 ch01 "线程生命周期与 RAII 管理" 涵盖——
 *       1. std::thread 默认按值拷贝参数 —— 与 std::bind 语义一致
 *       2. 需要引用传递时必须使用 std::ref / std::cref
 *       3. 移动语义：unique_ptr 等只移动类型通过 std::move 传入
 *       4. detach 后的悬垂引用（dangling reference）——最危险的陷阱
 *       5. shared_ptr 延长生命周期 —— detach 的安全替代方案
 *       6. join 保证被引用数据的生命周期
 *
 * 预期结果：
 *   程序依次演示参数传递的各种场景，包括正确用法与常见错误。
 *   悬垂引用 demo 使用 sleep 故意暴露问题（但不会 crash，
 *   因为 detach 后主线程等待足够长时间）。
 *
 * 编译命令：
 *   cmake -B build && cmake --build build
 *   ./build/02_thread_arguments_lifetime
 *
 * 编译器：GCC 12+ | Clang 15+ | MSVC 19.3+
 * 平台：x86-64 Linux / macOS / Windows
 * C++ 标准：C++17
 */

#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// 线程安全输出
std::mutex g_cout_mutex;

void safe_print(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_cout_mutex);
    std::cout << msg << std::endl;
}

// ============================================================
// Demo 1: std::ref —— 引用传递
// ============================================================

// 默认情况下，std::thread 会拷贝所有参数。
// 即使函数签名是 void f(int&)，thread 构造时也会先拷贝，
// 然后将拷贝的值绑定到引用参数。
// 要传递真正的引用，必须使用 std::ref 或 std::cref。

void increment_value(int& value) {
    ++value;
    safe_print("  [子线程] value = " + std::to_string(value));
}

void demo_ref_passing() {
    safe_print("\n=== Demo 1: std::ref 引用传递 ===");

    int value = 10;

    // 错误写法（编译不过）：
    //   std::thread t(increment_value, value);
    // 编译器会报错，因为 thread 内部按值拷贝了 value，
    // 但 increment_value 期望非 const 引用，无法绑定到临时拷贝。

    // 正确写法：使用 std::ref 包装
    std::thread t(increment_value, std::ref(value));
    t.join();

    safe_print("  [主线程] value = " + std::to_string(value) + "  (已被子线程修改)");

    // 使用 std::cref 传递 const 引用
    auto print_value = [](const int& v) {
        safe_print("  [子线程] 只读访问 value = " + std::to_string(v));
    };
    std::thread t2(print_value, std::cref(value));
    t2.join();
}

// ============================================================
// Demo 2: std::move —— 移动语义传递 unique_ptr
// ============================================================

// unique_ptr 是只移动类型（move-only），无法拷贝。
// std::thread 的构造函数会按值接收参数，
// 所以对于 unique_ptr 需要使用 std::move 将所有权转移到子线程。

void process_unique_ptr(std::unique_ptr<int> ptr) {
    if (ptr) {
        safe_print("  [子线程] unique_ptr 值 = " + std::to_string(*ptr));
        *ptr *= 2;
        safe_print("  [子线程] 修改后值 = " + std::to_string(*ptr));
    }
    // ptr 在子线程结束时销毁
}

void demo_move_semantics() {
    safe_print("\n=== Demo 2: std::move 移动语义 ===");

    auto ptr = std::make_unique<int>(42);
    safe_print("  [主线程] 创建 unique_ptr, 值 = " + std::to_string(*ptr));

    // std::move 将 unique_ptr 的所有权转移到子线程
    // 移动后，主线程的 ptr 变为 nullptr
    std::thread t(process_unique_ptr, std::move(ptr));

    // 此处 ptr 已经是 nullptr
    safe_print("  [主线程] 移动后 ptr " + std::string(ptr ? "非空" : "为空"));

    t.join();
    safe_print("  [主线程] 子线程结束，unique_ptr 已在子线程中销毁");
}

// ============================================================
// Demo 3: 悬垂引用 —— detach 的致命陷阱
// ============================================================

// 这是多线程编程中最危险的 bug 之一：
// 使用 detach 启动线程后，被引用的局部变量可能先于线程结束而被销毁，
// 导致未定义行为（UB）。

// 注意：本 demo 故意展示了问题模式，但通过 sleep 保证不 crash。
// 实际项目中这种 bug 极难调试，因为它只在特定时序下才出错。

void demo_dangling_reference() {
    safe_print("\n=== Demo 3: 悬垂引用（detach 的致命陷阱）===");

    // ---- 错误模式（注释中展示，不实际运行）----
    // {
    //     int local_var = 100;
    //     std::thread t([&local_var]() {
    //         // 如果 detach 后 local_var 已被销毁，
    //         // 这里访问的是悬垂引用 —— 未定义行为！
    //         std::cout << local_var << std::endl;  // UB!
    //     });
    //     t.detach();
    // }   // <-- local_var 在此处被销毁，但线程可能仍在运行

    safe_print("  [警告] 以下代码模式会导致未定义行为：");
    safe_print("  {");
    safe_print("      int local_var = 100;");
    safe_print("      std::thread t([&local_var]() { ... });");
    safe_print("      t.detach();");
    safe_print("  }  // local_var 被销毁，但线程仍在运行 -> 悬垂引用!");
    safe_print("  ");

    // ---- 安全的替代方案：使用值捕获 ----
    {
        int local_var = 100;
        // 值捕获：拷贝一份 local_var，线程拥有自己的副本
        std::thread t([local_var]() {
            safe_print("  [detach 线程] 值捕获 local_var = " + std::to_string(local_var) +
                       " (安全!)");
        });
        t.detach();
    } // local_var 被销毁，但线程持有的是拷贝，没有问题

    // 等待 detach 线程输出
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// ============================================================
// Demo 4: shared_ptr 延长生命周期 —— detach 的安全方案
// ============================================================

// 当确实需要 detach 一个线程，而又希望它访问的对象不被提前销毁，
// 可以将对象包装在 shared_ptr 中。引用计数保证对象在最后一个
// shared_ptr 被销毁时才析构。

void background_worker(std::shared_ptr<std::vector<int>> data) {
    // shared_ptr 按值传入，引用计数 +1
    // 即使主线程中的 shared_ptr 已被销毁，data 仍然有效
    safe_print("  [detach 线程] 收到 data, size = " + std::to_string(data->size()));
    data->push_back(99);
    safe_print("  [detach 线程] 添加元素后 size = " + std::to_string(data->size()));
    // data 在此销毁，引用计数 -1
}

void demo_shared_ptr_lifetime() {
    safe_print("\n=== Demo 4: shared_ptr 延长生命周期 ===");

    {
        auto data = std::make_shared<std::vector<int>>();
        data->push_back(1);
        data->push_back(2);
        data->push_back(3);

        safe_print("  [主线程] data.use_count() = " + std::to_string(data.use_count()));

        // shared_ptr 拷贝传入子线程，引用计数变为 2
        std::thread t(background_worker, data);

        safe_print("  [主线程] 传入后 data.use_count() = " + std::to_string(data.use_count()));

        t.detach();
    } // data 在此销毁，但子线程仍持有拷贝，引用计数从 2 降到 1
    // 对象不会析构，直到子线程中的 shared_ptr 也被销毁

    // 等待 detach 线程完成
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    safe_print("  [主线程] data 对象在子线程结束后才被销毁（安全）");
}

// ============================================================
// Demo 5: join 保证生命周期
// ============================================================

// 最简单也最安全的方案：使用 join 而非 detach。
// join 保证子线程在 join 点之前结束，因此子线程引用的所有
// 局部变量只要在 join 点之前仍然有效就没有问题。

void worker_with_ref(const std::string& message) {
    safe_print("  [子线程] message = \"" + message + "\"");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    safe_print("  [子线程] 工作完成");
}

void demo_join_lifetime_guarantee() {
    safe_print("\n=== Demo 5: join 保证生命周期 ===");

    std::string message = "Hello from main thread!";

    // 使用 std::cref 传递 const 引用给子线程
    std::thread t(worker_with_ref, std::cref(message));

    // join 保证：t 在此处之前完成，message 仍然有效
    t.join();

    safe_print("  [主线程] 子线程已结束，message 仍然有效: \"" + message + "\"");
    safe_print("  [结论] join 是最安全的同步方式，优先使用 join");
}

// ============================================================
// 主函数：依次运行所有 Demo
// ============================================================

int main() {
    std::cout << "===== ch01: 线程参数传递与生命周期陷阱 =====\n";

    demo_ref_passing();
    demo_move_semantics();
    demo_dangling_reference();
    demo_shared_ptr_lifetime();
    demo_join_lifetime_guarantee();

    std::cout << "\n===== 所有 Demo 运行完毕 =====" << std::endl;
    return 0;
}
