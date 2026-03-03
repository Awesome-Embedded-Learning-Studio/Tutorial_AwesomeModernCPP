// fixed_pool.h - 单线程固定大小内存池
#pragma once
#include <cstddef>
#include <cstdint>

class SimpleFixedPool {
    struct Node { Node* next; };

    void* buffer_;
    Node* free_head_;
    std::size_t slot_size_;
    std::size_t slot_count_;

public:
    SimpleFixedPool(void* buf, std::size_t slot_size, std::size_t count)
        : buffer_(buf)
        , free_head_(nullptr)
        , slot_size_((slot_size < sizeof(Node*)) ? sizeof(Node*) : slot_size)
        , slot_count_(count)
    {
        // 初始化空闲链表
        char* p = static_cast<char*>(buffer_);
        for (std::size_t i = 0; i < slot_count_; ++i) {
            Node* n = reinterpret_cast<Node*>(p + i * slot_size_);
            n->next = free_head_;
            free_head_ = n;
        }
    }

    void* allocate() noexcept {
        if (!free_head_) return nullptr;
        Node* n = free_head_;
        free_head_ = n->next;
        return n;
    }

    void deallocate(void* p) noexcept {
        Node* n = static_cast<Node*>(p);
        n->next = free_head_;
        free_head_ = n;
    }

    std::size_t slot_size() const noexcept { return slot_size_; }
    std::size_t slot_count() const noexcept { return slot_count_; }
    std::size_t capacity() const noexcept { return slot_count_ * slot_size_; }
};
