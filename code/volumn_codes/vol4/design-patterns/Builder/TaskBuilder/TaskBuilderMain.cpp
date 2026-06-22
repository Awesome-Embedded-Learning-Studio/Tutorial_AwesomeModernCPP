#include "TaskBuilder.h"

#include <iostream>
#include <print>
#include <string>

// 把一个 Task 打印成文章里那种可观察的格式:
//   Task{desc=..., prio=N, ddl=YYYY-M-D, title=..., details=...}
static void print_task(const Task& t) {
    const auto& ddl = t.ddl();
    std::println("Task{{desc={}, prio={}, ddl={}-{}-{}, title={}, details={}}}", t.description(),
                 static_cast<int>(t.priority()), ddl.year, ddl.month, ddl.day,
                 t.title().value_or("(none)"), t.details().value_or("(none)"));
}

// 文章第四步:完整的链式调用,字段齐全,build() 成功。
static void demo_fluent_build() {
    std::println("== Fluent Builder: full chain ==");
    Task task = TaskBuilder{}
                    .with_priority(Task::Priority::High)
                    .with_ddl({2025, 9, 25, 10, 0, 0})
                    .with_description("Finish Builder blog")
                    .with_title("Fluent Builder")
                    .with_details("return *this chains the calls")
                    .build();
    print_task(task);
    std::println("");
}

// 文章第四步:漏填 priority 和 ddl,build() 抛异常,演示运行时校验。
static void demo_missing_required() {
    std::println("== Fluent Builder: missing required field ==");
    try {
        Task task =
            TaskBuilder{}.with_description("Forget priority and ddl").with_title("Oops").build();
        (void)task; // 不该走到这里
    } catch (const std::exception& e) {
        std::println("caught: {}", e.what());
    }
    std::println("");
}

// 文章第四步:构建器是可以传递的中间状态。
// 先把必填项攒成半成品,「查到真正的标题」后再接着填、再 build。
static void demo_deferred_partial() {
    std::println("== Fluent Builder: deferred partial ==");
    auto partial = TaskBuilder{}
                       .with_priority(Task::Priority::High)
                       .with_ddl({2025, 9, 25, 10, 0, 0})
                       .with_description("Complete the final project report.");

    // 模拟「异步查到真正的标题」
    const std::string title = "Project Report";
    Task task = partial.with_title(title).with_details("Check all data points.").build();
    print_task(task);
    std::println("");
}

// 文章「RVO 真的省掉了那次拷贝吗」:挂计数器,
// 验证 build() 里 return t; 在 -O2 / -O0 下都 moves=0 copies=0。
static void demo_rvo_zero_copy() {
    std::println("== RVO: mandatory copy elision ==");
    Tracked t = TrackedBuilder{}.with_value(42).build();
    std::println("value={} moves={} copies={}", t.v, Tracked::kMoveCount, Tracked::kCopyCount);
    std::println("");
}

int main() {
    demo_fluent_build();
    demo_missing_required();
    demo_deferred_partial();
    demo_rvo_zero_copy();
    return 0;
}
