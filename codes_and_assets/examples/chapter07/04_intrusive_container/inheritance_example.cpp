// inheritance_example.cpp - 继承式侵入链表示例
#include "intrusive_list.h"
#include <iostream>

struct Task : IntrusiveListNode<Task> {
    int id;
    const char* name;

    Task(int i, const char* n) : id(i), name(n) {}
};

int main() {
    IntrusiveList<Task> runq;

    // 创建任务对象
    Task task1(1, "Task A");
    Task task2(2, "Task B");
    Task task3(3, "Task C");

    std::cout << "=== Pushing tasks to queue ===\n";

    // 添加到队列
    runq.push_back(&task1);
    runq.push_back(&task2);
    runq.push_front(&task3);  // 链表顺序： task3, task1, task2

    std::cout << "\nQueue contents:\n";
    for (auto& t : runq) {
        std::cout << "  Task " << t.id << ": " << t.name << '\n';
    }

    std::cout << "\n=== Removing task1 ===\n";
    runq.erase(&task1);

    std::cout << "Queue contents after erase:\n";
    for (auto& t : runq) {
        std::cout << "  Task " << t.id << ": " << t.name << '\n';
    }

    std::cout << "\n=== Popping from front ===\n";
    while (auto p = runq.pop_front()) {
        std::cout << "  Popped: Task " << p->id << ": " << p->name << '\n';
    }

    std::cout << "\nQueue empty: " << (runq.empty() ? "yes" : "no") << '\n';

    // 演示错误检查（断言会触发）
    std::cout << "\n=== Error handling demo ===\n";
    Task task4(4, "Task D");
    runq.push_back(&task4);

    std::cout << "Attempting to push already-linked node (should assert):\n";
    std::cout << "(Commented out to prevent crash)\n";
    // runq.push_back(&task4);  // 这会触发断言

    return 0;
}
