// task_scheduler_example.cpp - 使用侵入式链表的任务调度器
#include "intrusive_list.h"
#include <iostream>
#include <queue>
#include <vector>

// 任务状态枚举
enum class TaskState {
    Ready,
    Running,
    Waiting,
    Done
};

// 任务类 - 侵入式设计
struct Task : IntrusiveListNode<Task> {
    int id;
    const char* name;
    TaskState state;
    int priority;

    Task(int i, const char* n, int p = 0)
        : id(i), name(n), state(TaskState::Ready), priority(p) {}

    void run() {
        std::cout << "  Running Task " << id << ": " << name << '\n';
        state = TaskState::Running;
        // 模拟任务执行
        state = TaskState::Done;
    }
};

// 任务比较器（用于优先队列）
struct TaskCompare {
    bool operator()(const Task* a, const Task* b) const {
        return a->priority < b->priority;
    }
};

// 简单的任务调度器
class TaskScheduler {
public:
    void add_task(Task* task) {
        ready_list.push_back(task);
    }

    Task* get_next_task() {
        return ready_list.pop_front();
    }

    void run_all() {
        std::cout << "\n=== Running all tasks ===\n";
        while (auto task = get_next_task()) {
            if (task->state == TaskState::Ready) {
                task->run();
            }
        }
    }

    bool empty() const {
        return ready_list.empty();
    }

private:
    IntrusiveList<Task> ready_list;
};

int main() {
    TaskScheduler scheduler;

    std::cout << "=== Intrusive Container Task Scheduler Demo ===\n\n";

    // 创建任务（栈上分配，无堆操作）
    Task task1(1, "Initialize Hardware", 10);
    Task task2(2, "Read Sensors", 5);
    Task task3(3, "Process Data", 8);
    Task task4(4, "Send Report", 3);

    // 添加任务到调度器
    scheduler.add_task(&task1);
    scheduler.add_task(&task2);
    scheduler.add_task(&task3);
    scheduler.add_task(&task4);

    std::cout << "Tasks added to scheduler (no memory allocation!)\n";
    std::cout << "Scheduler empty: " << (scheduler.empty() ? "yes" : "no") << '\n';

    // 运行所有任务
    scheduler.run_all();

    std::cout << "\nScheduler empty: " << (scheduler.empty() ? "yes" : "no") << '\n';

    std::cout << "\n=== Benefits demonstrated ===\n";
    std::cout << "- No heap allocation for task nodes\n";
    std::cout << "- Tasks can be on stack or in static memory\n";
    std::cout << "- Zero overhead abstraction\n";
    std::cout << "- Deterministic memory usage\n";

    return 0;
}
