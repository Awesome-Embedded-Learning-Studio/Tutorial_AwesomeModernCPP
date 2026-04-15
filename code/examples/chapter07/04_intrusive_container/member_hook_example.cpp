// member_hook_example.cpp - 成员 hook 方式示例
#include "member_hook.h"
#include <iostream>

// 可以同时属于多个链表的对象
struct MyObject {
    int id;
    IntrusiveNode ready_hook;   // for ready list
    IntrusiveNode wait_hook;    // for wait list

    MyObject(int i) : id(i) {}
};

int main() {
    IntrusiveListGeneric ready_list;
    IntrusiveListGeneric wait_list;

    // 创建对象
    MyObject obj1(1);
    MyObject obj2(2);
    MyObject obj3(3);

    std::cout << "=== Member Hook Demo ===\n\n";

    // 添加到 ready_list
    ready_list.push_back(&obj1.ready_hook);
    ready_list.push_back(&obj2.ready_hook);

    // 添加到 wait_list
    wait_list.push_back(&obj3.wait_hook);

    std::cout << "Ready list:\n";
    for (IntrusiveNode* node = ready_list.front(); node; node = node->next) {
        MyObject* obj = CONTAINER_OF(node, MyObject, ready_hook);
        std::cout << "  Object " << obj->id << '\n';
    }

    std::cout << "\nWait list:\n";
    for (IntrusiveNode* node = wait_list.front(); node; node = node->next) {
        MyObject* obj = CONTAINER_OF(node, MyObject, wait_hook);
        std::cout << "  Object " << obj->id << '\n';
    }

    // 演示移动：从 wait_list 移到 ready_list
    std::cout << "\n=== Moving obj3 from wait to ready ===\n";
    wait_list.erase(&obj3.wait_hook);
    ready_list.push_back(&obj3.ready_hook);

    std::cout << "Ready list after move:\n";
    for (IntrusiveNode* node = ready_list.front(); node; node = node->next) {
        MyObject* obj = CONTAINER_OF(node, MyObject, ready_hook);
        std::cout << "  Object " << obj->id << '\n';
    }

    std::cout << "\nWait list after move:\n";
    std::cout << "  " << (wait_list.empty() ? "(empty)" : "not empty") << '\n';

    return 0;
}
