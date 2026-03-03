#include <iostream>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <new>

// Slab分配器 - 简化版Linux内核slab

template<size_t OBJECT_SIZE, size_t SLAB_CAPACITY>
class Slab {
public:
    struct SlabBlock {
        alignas(std::max_align_t) uint8_t memory[SLAB_CAPACITY * OBJECT_SIZE];
        uint32_t in_use;  // 位图跟踪使用情况（最多32个对象）
        SlabBlock* next;
        size_t free_count;

        SlabBlock() : in_use(0), next(nullptr), free_count(SLAB_CAPACITY) {}
    };

private:
    SlabBlock* partial_list_;  // 部分使用的slab
    SlabBlock* full_list_;     // 已满的slab
    SlabBlock* empty_list_;    // 全空的slab
    size_t total_slabs_;

    // 从slab中分配一个对象
    void* allocate_from_slab(SlabBlock* slab) {
        for (size_t i = 0; i < SLAB_CAPACITY; ++i) {
            if ((slab->in_use & (1U << i)) == 0) {
                slab->in_use |= (1U << i);
                slab->free_count--;
                return &slab->memory[i * OBJECT_SIZE];
            }
        }
        return nullptr;
    }

    SlabBlock* new_slab() {
        SlabBlock* slab = new (std::nothrow) SlabBlock();
        if (slab) {
            total_slabs_++;
        }
        return slab;
    }

public:
    Slab() : partial_list_(nullptr), full_list_(nullptr), empty_list_(nullptr), total_slabs_(0) {
        // 预分配一个空slab
        empty_list_ = new_slab();
    }

    ~Slab() {
        // 释放所有slab
        SlabBlock* slab = partial_list_;
        while (slab) {
            SlabBlock* next = slab->next;
            delete slab;
            slab = next;
        }

        slab = full_list_;
        while (slab) {
            SlabBlock* next = slab->next;
            delete slab;
            slab = next;
        }

        slab = empty_list_;
        while (slab) {
            SlabBlock* next = slab->next;
            delete slab;
            slab = next;
        }
    }

    void* allocate() {
        // 优先从partial slab分配
        if (partial_list_) {
            void* ptr = allocate_from_slab(partial_list_);
            if (ptr) {
                // 检查slab是否变满
                if (partial_list_->free_count == 0) {
                    // 移到full list
                    SlabBlock* full = partial_list_;
                    partial_list_ = partial_list_->next;
                    full->next = full_list_;
                    full_list_ = full;
                }
                return ptr;
            }
        }

        // 从empty list获取
        if (empty_list_) {
            void* ptr = allocate_from_slab(empty_list_);
            if (ptr) {
                // 移到partial list
                SlabBlock* partial = empty_list_;
                empty_list_ = empty_list_->next;
                partial->next = partial_list_;
                partial_list_ = partial;
                return ptr;
            }
        }

        // 需要分配新slab
        SlabBlock* new_slab_block = new_slab();
        if (!new_slab_block) {
            return nullptr;  // 分配失败
        }

        void* ptr = allocate_from_slab(new_slab_block);
        new_slab_block->next = partial_list_;
        partial_list_ = new_slab_block;
        return ptr;
    }

    void deallocate(void* ptr) {
        if (!ptr) return;

        // 查找ptr所属的slab
        SlabBlock** slab_ptr = &full_list_;
        while (*slab_ptr) {
            if (ptr >= (*slab_ptr)->memory &&
                ptr < (*slab_ptr)->memory + sizeof((*slab_ptr)->memory)) {
                break;
            }
            slab_ptr = &((*slab_ptr)->next);
        }

        if (!*slab_ptr) {
            slab_ptr = &partial_list_;
            while (*slab_ptr) {
                if (ptr >= (*slab_ptr)->memory &&
                    ptr < (*slab_ptr)->memory + sizeof((*slab_ptr)->memory)) {
                    break;
                }
                slab_ptr = &((*slab_ptr)->next);
            }
        }

        if (!*slab_ptr) {
            return;  // 不属于这个分配器
        }

        SlabBlock* slab = *slab_ptr;
        size_t index = (static_cast<uint8_t*>(ptr) - slab->memory) / OBJECT_SIZE;
        slab->in_use &= ~(1U << index);
        slab->free_count++;

        // 根据使用情况移动slab
        if (slab->free_count == SLAB_CAPACITY) {
            // 全空，移到empty list
            *slab_ptr = slab->next;
            slab->next = empty_list_;
            empty_list_ = slab;
        } else if (slab == full_list_) {
            // 从full移到partial
            *slab_ptr = slab->next;
            slab->next = partial_list_;
            partial_list_ = slab;
        }
    }

    void print_stats() const {
        size_t partial_count = 0;
        size_t full_count = 0;
        size_t empty_count = 0;

        SlabBlock* slab = partial_list_;
        while (slab) { partial_count++; slab = slab->next; }

        slab = full_list_;
        while (slab) { full_count++; slab = slab->next; }

        slab = empty_list_;
        while (slab) { empty_count++; slab = slab->next; }

        std::cout << "Slab stats: " << total_slabs_ << " total ("
                  << partial_count << " partial, "
                  << full_count << " full, "
                  << empty_count << " empty)\n";
    }
};

// Arena/Bump分配器
template<size_t ARENA_SIZE>
class ArenaAllocator {
    alignas(std::max_align_t) uint8_t memory_[ARENA_SIZE];
    size_t offset_;

public:
    ArenaAllocator() : offset_(0) {}

    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(memory_ + offset_);
        uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
        size_t aligned_offset = aligned - reinterpret_cast<uintptr_t>(memory_);

        if (aligned_offset + size > ARENA_SIZE) {
            return nullptr;
        }

        offset_ = aligned_offset + size;
        return reinterpret_cast<void*>(aligned);
    }

    void reset() {
        offset_ = 0;
    }

    size_t used() const { return offset_; }
    size_t available() const { return ARENA_SIZE - offset_; }
};

// 使用示例

struct Task {
    int id;
    char name[32];
    void (*func)(void);

    Task(int i, const char* n, void (*f)(void)) : id(i), func(f) {
        std::strncpy(name, n, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }

    void execute() {
        std::cout << "Task " << id << " (" << name << ") executing\n";
        if (func) func();
    }
};

void slab_demo() {
    std::cout << "=== Slab Allocator Demo ===\n\n";

    // 创建一个Task类型的slab分配器
    // 每个slab最多16个Task对象
    Slab<sizeof(Task), 16> task_slab;

    std::cout << "Initial state: ";
    task_slab.print_stats();

    // 分配任务
    Task* tasks[20];
    for (int i = 0; i < 20; ++i) {
        void* mem = task_slab.allocate();
        if (mem) {
            tasks[i] = new (mem) Task(i, "Task", nullptr);
        } else {
            tasks[i] = nullptr;
            std::cout << "Failed to allocate task " << i << "\n";
        }
    }

    std::cout << "\nAfter allocating 20 tasks: ";
    task_slab.print_stats();

    // 执行一些任务
    for (int i = 0; i < 5; ++i) {
        if (tasks[i]) tasks[i]->execute();
    }

    // 释放一些任务
    for (int i = 0; i < 10; ++i) {
        if (tasks[i]) {
            task_slab.deallocate(tasks[i]);
        }
    }

    std::cout << "\nAfter freeing 10 tasks: ";
    task_slab.print_stats();

    // 再次分配
    Task* new_task = nullptr;
    void* mem = task_slab.allocate();
    if (mem) {
        new_task = new (mem) Task(99, "NewTask", nullptr);
        std::cout << "Allocated new task after freeing\n";
    }

    // 清理剩余
    for (int i = 10; i < 20; ++i) {
        if (tasks[i]) {
            task_slab.deallocate(tasks[i]);
        }
    }
    if (new_task) {
        task_slab.deallocate(new_task);
    }
}

void arena_demo() {
    std::cout << "\n=== Arena Allocator Demo ===\n\n";

    ArenaAllocator<4096> arena;

    std::cout << "Arena: " << arena.available() << " bytes available\n";

    // 分配一些对象
    void* a = arena.allocate(sizeof(int), alignof(int));
    void* b = arena.allocate(100, 8);
    void* c = arena.allocate(sizeof(double), alignof(double));

    std::cout << "After allocations: " << arena.used() << " bytes used\n";

    // 重置
    arena.reset();
    std::cout << "After reset: " << arena.used() << " bytes used\n";

    // 新的分配从头开始
    void* d = arena.allocate(200, 8);
    std::cout << "New allocation at " << d << " (should be at start)\n";
}

// 混合使用：Slab + Arena
class HybridAllocator {
    Slab<64, 32> small_objects_;   // 小对象（<=64字节）
    ArenaAllocator<4096> arena_;   // 大对象/临时对象

public:
    enum class AllocationType {
        Small,
        Large
    };

    void* allocate(size_t size, AllocationType type = AllocationType::Small) {
        if (type == AllocationType::Small && size <= 64) {
            return small_objects_.allocate();
        } else {
            return arena_.allocate(size);
        }
    }

    void deallocate(void* ptr, AllocationType type) {
        if (type == AllocationType::Small) {
            small_objects_.deallocate(ptr);
        }
        // Arena对象不需要单独释放
    }

    void reset_arena() {
        arena_.reset();
    }

    void print_stats() const {
        std::cout << "Small object slab: ";
        small_objects_.print_stats();
        std::cout << "Arena: " << arena_.used() << " / " << 4096 << " bytes used\n";
    }
};

void hybrid_demo() {
    std::cout << "\n=== Hybrid Allocator Demo ===\n\n";

    HybridAllocator alloc;

    // 分配小对象
    void* small1 = alloc.allocate(32, HybridAllocator::AllocationType::Small);
    void* small2 = alloc.allocate(64, HybridAllocator::AllocationType::Small);
    void* small3 = alloc.allocate(16, HybridAllocator::AllocationType::Small);

    // 分配大对象（使用arena）
    void* large1 = alloc.allocate(512, HybridAllocator::AllocationType::Large);
    void* large2 = alloc.allocate(1024, HybridAllocator::AllocationType::Large);

    alloc.print_stats();

    // 释放小对象
    alloc.deallocate(small1, HybridAllocator::AllocationType::Small);
    alloc.deallocate(small2, HybridAllocator::AllocationType::Small);

    std::cout << "\nAfter freeing small objects:\n";
    alloc.print_stats();

    // 重置arena
    alloc.reset_arena();
    std::cout << "\nAfter arena reset:\n";
    alloc.print_stats();

    // 清理
    alloc.deallocate(small3, HybridAllocator::AllocationType::Small);
}

int main() {
    slab_demo();
    arena_demo();
    hybrid_demo();

    std::cout << "\n=== Key Takeaways ===\n";
    std::cout << "1. Slab: Fixed-size objects, maintains free/partial/full lists\n";
    std::cout << "2. Arena: Linear allocation, fast, no individual free\n";
    std::cout << "3. Hybrid: Combine both for different allocation patterns\n";
    std::cout << "4. Slab good for: object caching, reuse, reducing construction cost\n";
    std::cout << "5. Arena good for: initialization, temporary objects, batch processing\n";

    return 0;
}
