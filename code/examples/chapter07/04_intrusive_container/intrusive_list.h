// intrusive_list.h - 简单、类型安全的侵入式双向链表（继承式）
#pragma once
#include <cassert>
#include <iterator>

// Intrusive list node base — 继承它即可成为链表节点
template<typename T>
struct IntrusiveListNode {
    T* prev = nullptr;
    T* next = nullptr;
};

// Intrusive doubly linked list
template<typename T>
class IntrusiveList {
public:
    IntrusiveList() : head(nullptr), tail(nullptr) {}

    bool empty() const { return head == nullptr; }

    void push_front(T* node) {
        assert(node && node->prev == nullptr && node->next == nullptr && "节点必须处于未链接状态");
        node->next = head;
        if (head) head->prev = node;
        head = node;
        if (!tail) tail = node;
    }

    void push_back(T* node) {
        assert(node && node->prev == nullptr && node->next == nullptr && "节点必须处于未链接状态");
        node->prev = tail;
        if (tail) tail->next = node;
        tail = node;
        if (!head) head = node;
    }

    T* pop_front() {
        if (!head) return nullptr;
        T* n = head;
        head = head->next;
        if (head) head->prev = nullptr;
        else tail = nullptr;
        n->next = n->prev = nullptr;
        return n;
    }

    void erase(T* node) {
        assert(node && "erase null");
        if (node->prev) node->prev->next = node->next;
        else head = node->next;

        if (node->next) node->next->prev = node->prev;
        else tail = node->prev;

        node->prev = node->next = nullptr;
    }

    void clear() {
        T* cur = head;
        while (cur) {
            T* nxt = cur->next;
            cur->prev = cur->next = nullptr;
            cur = nxt;
        }
        head = tail = nullptr;
    }

    // 简单迭代器（只读/可写）
    struct iterator {
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using pointer = T*;
        using reference = T&;

        explicit iterator(T* p) : p(p) {}
        reference operator*() const { return *p; }
        pointer operator->() const { return p; }
        iterator& operator++() { p = p->next; return *this; }
        iterator operator++(int) { iterator tmp = *this; ++*this; return tmp; }
        bool operator==(const iterator& o) const { return p == o.p; }
        bool operator!=(const iterator& o) const { return p != o.p; }
    private:
        T* p;
    };

    iterator begin() { return iterator(head); }
    iterator end() { return iterator(nullptr); }

private:
    T* head;
    T* tail;
};
