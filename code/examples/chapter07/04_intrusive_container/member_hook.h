// member_hook.h - 成员 hook 方式的侵入式链表
#pragma once
#include <cstddef>
#include <cassert>

// container_of 宏 - 给定成员指针，获取包含对象指针
#define CONTAINER_OF(ptr, type, member) \
    reinterpret_cast<type*>(reinterpret_cast<char*>(ptr) - offsetof(type, member))

// 简单的通用链表节点
struct IntrusiveNode {
    IntrusiveNode* prev = nullptr;
    IntrusiveNode* next = nullptr;
};

// 通用侵入式双向链表
class IntrusiveListGeneric {
public:
    IntrusiveListGeneric() : head(nullptr), tail(nullptr) {}

    bool empty() const { return head == nullptr; }

    void push_back(IntrusiveNode* node) {
        assert(node && node->prev == nullptr && node->next == nullptr);
        node->prev = tail;
        if (tail) tail->next = node;
        tail = node;
        if (!head) head = node;
    }

    void push_front(IntrusiveNode* node) {
        assert(node && node->prev == nullptr && node->next == nullptr);
        node->next = head;
        if (head) head->prev = node;
        head = node;
        if (!tail) tail = node;
    }

    void erase(IntrusiveNode* node) {
        if (node->prev) node->prev->next = node->next;
        else head = node->next;

        if (node->next) node->next->prev = node->prev;
        else tail = node->prev;

        node->prev = node->next = nullptr;
    }

    IntrusiveNode* pop_front() {
        if (!head) return nullptr;
        IntrusiveNode* n = head;
        head = head->next;
        if (head) head->prev = nullptr;
        else tail = nullptr;
        n->next = n->prev = nullptr;
        return n;
    }

    IntrusiveNode* front() { return head; }
    const IntrusiveNode* front() const { return head; }

private:
    IntrusiveNode* head;
    IntrusiveNode* tail;
};
