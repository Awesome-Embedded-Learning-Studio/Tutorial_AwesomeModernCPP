#include <iostream>
#include <cstdint>
#include <cstddef>
#include <new>
#include <cassert>

// 固定池分配器 - 最简单的内存池实现

template<size_t BLOCK_SIZE, size_t BLOCK_COUNT>
class FixedPool {
    struct Block {
        alignas(std::max_align_t) uint8_t data[BLOCK_SIZE];
    };

    Block pool_[BLOCK_COUNT];
    size_t free_list_[BLOCK_COUNT];
    size_t free_head_;
    size_t used_count_;

public:
    FixedPool() : used_count_(0) {
        // 初始化空闲链表
        for (size_t i = 0; i < BLOCK_COUNT; ++i) {
            free_list_[i] = i + 1;
        }
        free_list_[BLOCK_COUNT - 1] = static_cast<size_t>(-1);  // 标记结尾
        free_head_ = 0;
    }

    // 分配一个块
    void* allocate() {
        if (free_head_ == static_cast<size_t>(-1)) {
            return nullptr;  // 池已满
        }

        size_t idx = free_head_;
        free_head_ = free_list_[idx];
        used_count_++;

        return &pool_[idx].data[0];
    }

    // 释放一个块
    void deallocate(void* ptr) {
        if (!ptr) return;

        size_t idx = static_cast<Block*>(ptr) - pool_;
        assert(idx < BLOCK_COUNT && "Pointer not from this pool");

        free_list_[idx] = free_head_;
        free_head_ = idx;
        used_count_--;
    }

    size_t block_size() const { return BLOCK_SIZE; }
    size_t capacity() const { return BLOCK_COUNT; }
    size_t used_count() const { return used_count_; }
    size_t free_count() const { return BLOCK_COUNT - used_count_; }
    bool is_full() const { return free_head_ == static_cast<size_t>(-1); }
    bool is_empty() const { return used_count_ == 0; }

    // 调试：打印池状态
    void print_status() const {
        std::cout << "Pool[" << BLOCK_SIZE << " x " << BLOCK_COUNT << "]: "
                  << used_count_ << " used, " << free_count() << " free\n";
    }
};

// 类型安全的固定池
template<typename T, size_t N>
class TypedFixedPool {
    alignas(T) uint8_t storage_[N * sizeof(T)];
    size_t free_list_[N];
    size_t free_head_;
    size_t used_count_;

public:
    TypedFixedPool() : used_count_(0) {
        for (size_t i = 0; i < N; ++i) {
            free_list_[i] = i + 1;
        }
        free_list_[N - 1] = static_cast<size_t>(-1);
        free_head_ = 0;
    }

    T* allocate() {
        if (free_head_ == static_cast<size_t>(-1)) {
            return nullptr;
        }

        size_t idx = free_head_;
        free_head_ = free_list_[idx];
        used_count_++;

        T* obj = reinterpret_cast<T*>(&storage_[idx * sizeof(T)]);
        new (obj) T();  // placement new，默认构造
        return obj;
    }

    template<typename... Args>
    T* construct(Args&&... args) {
        if (free_head_ == static_cast<size_t>(-1)) {
            return nullptr;
        }

        size_t idx = free_head_;
        free_head_ = free_list_[idx];
        used_count_++;

        T* obj = reinterpret_cast<T*>(&storage_[idx * sizeof(T)]);
        new (obj) T(std::forward<Args>(args)...);
        return obj;
    }

    void deallocate(T* ptr) {
        if (!ptr) return;

        ptr->~T();  // 调用析构函数

        size_t idx = (reinterpret_cast<uint8_t*>(ptr) - storage_) / sizeof(T);
        free_list_[idx] = free_head_;
        free_head_ = idx;
        used_count_--;
    }

    size_t capacity() const { return N; }
    size_t used_count() const { return used_count_; }
    size_t free_count() const { return N - used_count_; }
};

// 使用示例

struct Packet {
    uint8_t data[64];
    size_t length;

    Packet() : length(0) {
        std::cout << "Packet constructed\n";
    }

    ~Packet() {
        std::cout << "Packet destructed\n";
    }

    void set_data(const char* msg) {
        size_t i = 0;
        while (msg[i] && i < sizeof(data) - 1) {
            data[i] = static_cast<uint8_t>(msg[i]);
            ++i;
        }
        data[i] = '\0';
        length = i;
    }

    void print() const {
        std::cout << "Packet[len=" << length << ", data=\"";
        for (size_t i = 0; i < length && i < 20; ++i) {
            std::cout << static_cast<char>(data[i]);
        }
        std::cout << "\"]\n";
    }
};

void basic_pool_demo() {
    std::cout << "=== Basic Fixed Pool Demo ===\n\n";

    FixedPool<64, 8> pool;

    std::cout << "Initial: ";
    pool.print_status();

    // 分配一些块
    void* blocks[5];
    for (int i = 0; i < 5; ++i) {
        blocks[i] = pool.allocate();
        std::cout << "Allocated block " << i << " at " << blocks[i] << "\n";
    }

    std::cout << "\nAfter 5 allocations: ";
    pool.print_status();

    // 释放一些块
    pool.deallocate(blocks[1]);
    pool.deallocate(blocks[3]);

    std::cout << "\nAfter freeing 2 blocks: ";
    pool.print_status();

    // 再次分配
    void* new_block = pool.allocate();
    std::cout << "Reallocated at " << new_block << "\n";
    std::cout << "Should be same as freed block 1: " << (new_block == blocks[1] ? "yes" : "no") << "\n";

    // 清理
    for (int i = 0; i < 5; ++i) {
        if (i != 1 && i != 3) {
            pool.deallocate(blocks[i]);
        }
    }
    pool.deallocate(new_block);
}

void typed_pool_demo() {
    std::cout << "\n=== Typed Fixed Pool Demo ===\n\n";

    TypedFixedPool<Packet, 4> pool;

    std::cout << "Packet pool: " << pool.used_count() << " / " << pool.capacity() << " used\n";

    // 分配包
    Packet* p1 = pool.construct();
    Packet* p2 = pool.construct();
    Packet* p3 = pool.construct();

    std::cout << "\nAfter 3 allocations: " << pool.used_count() << " / " << pool.capacity() << " used\n";

    // 使用包
    p1->set_data("Hello");
    p2->set_data("World");
    p3->set_data("Test");

    p1->print();
    p2->print();
    p3->print();

    // 尝试分配超过容量
    Packet* p4 = pool.construct();
    Packet* p5 = pool.construct();  // 应该失败

    std::cout << "\np4 allocated: " << (p4 != nullptr ? "yes" : "no") << "\n";
    std::cout << "p5 allocated: " << (p5 != nullptr ? "yes (unexpected!)" : "no (expected)") << "\n";

    // 清理
    pool.deallocate(p1);
    pool.deallocate(p2);
    pool.deallocate(p3);
    if (p4) pool.deallocate(p4);
}

// 多尺寸池系统
class MultiSizePoolSystem {
    FixedPool<32, 16> pool32_;
    FixedPool<64, 16> pool64_;
    FixedPool<128, 16> pool128_;
    FixedPool<256, 16> pool256_;

public:
    void* allocate(size_t size) {
        if (size <= 32) return pool32_.allocate();
        if (size <= 64) return pool64_.allocate();
        if (size <= 128) return pool128_.allocate();
        if (size <= 256) return pool256_.allocate();
        return nullptr;
    }

    void deallocate(void* ptr, size_t size) {
        if (size <= 32) pool32_.deallocate(ptr);
        else if (size <= 64) pool64_.deallocate(ptr);
        else if (size <= 128) pool128_.deallocate(ptr);
        else if (size <= 256) pool256_.deallocate(ptr);
    }

    void print_stats() const {
        std::cout << "Pool Statistics:\n";
        std::cout << "  32-byte: "; pool32_.print_status();
        std::cout << "  64-byte: "; pool64_.print_status();
        std::cout << "  128-byte: "; pool128_.print_status();
        std::cout << "  256-byte: "; pool256_.print_status();
    }
};

void multi_size_pool_demo() {
    std::cout << "\n=== Multi-Size Pool System Demo ===\n\n";

    MultiSizePoolSystem pools;

    // 分配不同大小的对象
    void* a = pools.allocate(20);   // 使用32字节池
    void* b = pools.allocate(50);   // 使用64字节池
    void* c = pools.allocate(100);  // 使用128字节池
    void* d = pools.allocate(200);  // 使用256字节池

    std::cout << "After allocations:\n";
    pools.print_stats();

    // 释放
    pools.deallocate(a, 20);
    pools.deallocate(c, 100);

    std::cout << "\nAfter freeing some:\n";
    pools.print_stats();

    // 再次分配
    void* e = pools.allocate(30);   // 应该重用32字节池
    std::cout << "\nReallocation matches freed: " << (e == a ? "yes" : "no") << "\n";

    // 清理
    pools.deallocate(b, 50);
    pools.deallocate(d, 200);
    pools.deallocate(e, 30);
}

int main() {
    basic_pool_demo();
    typed_pool_demo();
    multi_size_pool_demo();

    std::cout << "\n=== Key Takeaways ===\n";
    std::cout << "1. Fixed pool provides O(1) allocation/deallocation\n";
    std::cout << "2. No fragmentation with fixed-size blocks\n";
    std::cout << "3. Typed pool adds type safety and construction/destruction\n";
    std::cout << "4. Multi-size system serves various allocation needs\n";
    std::cout << "5. Predictable memory usage and timing\n";

    return 0;
}
