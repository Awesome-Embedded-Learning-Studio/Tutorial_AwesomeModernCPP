#include <iostream>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <new>

// Bump/线性分配器 + placement new

class BumpAllocator {
    uint8_t* base_;
    size_t capacity_;
    size_t offset_;

public:
    BumpAllocator(void* mem, size_t cap)
        : base_(static_cast<uint8_t*>(mem)), capacity_(cap), offset_(0) {}

    // 返回已对齐的指针，或 nullptr（失败）
    void* allocate(size_t n, size_t align = alignof(std::max_align_t)) {
        uintptr_t cur = reinterpret_cast<uintptr_t>(base_ + offset_);
        uintptr_t aligned = (cur + align - 1) & ~(align - 1);
        size_t aligned_offset = aligned - reinterpret_cast<uintptr_t>(base_);

        if (aligned_offset + n > capacity_) {
            return nullptr;  // 溢出
        }

        offset_ = aligned_offset + n;
        return reinterpret_cast<void*>(aligned);
    }

    void reset() {
        offset_ = 0;
    }

    size_t used() const { return offset_; }
    size_t available() const { return capacity_ - offset_; }
    size_t capacity() const { return capacity_; }

    // 获取当前分配位置（用于调试）
    void* current_position() const {
        return base_ + offset_;
    }
};

struct Widget {
    int id;
    char name[32];
    double value;

    Widget(int i, const char* n, double v) : id(i), value(v) {
        std::strncpy(name, n, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        std::cout << "Widget " << id << " (" << name << ") constructed\n";
    }

    ~Widget() {
        std::cout << "Widget " << id << " (" << name << ") destructed\n";
    }

    void print() const {
        std::cout << "Widget{id=" << id << ", name=" << name
                  << ", value=" << value << "}\n";
    }
};

void basic_bump_demo() {
    std::cout << "=== Basic Bump Allocator Demo ===\n\n";

    // 静态缓冲区作为arena
    static uint8_t arena_mem[4096];
    BumpAllocator arena(arena_mem, sizeof(arena_mem));

    std::cout << "Arena: " << arena.capacity() << " bytes\n";
    std::cout << "Initial available: " << arena.available() << " bytes\n\n";

    // 分配对象
    std::cout << "--- Allocating objects ---\n";
    void* p1 = arena.allocate(sizeof(Widget), alignof(Widget));
    if (p1) {
        Widget* w1 = new (p1) Widget(1, "First", 3.14);
        w1->print();
    }

    void* p2 = arena.allocate(sizeof(Widget), alignof(Widget));
    if (p2) {
        Widget* w2 = new (p2) Widget(2, "Second", 2.71);
        w2->print();
    }

    std::cout << "\nUsed: " << arena.used() << " bytes\n";
    std::cout << "Available: " << arena.available() << " bytes\n";

    // 手动析构
    std::cout << "\n--- Manual destruction ---\n";
    if (p1) {
        static_cast<Widget*>(p1)->~Widget();
    }
    if (p2) {
        static_cast<Widget*>(p2)->~Widget();
    }

    // 重置arena
    std::cout << "\n--- Resetting arena ---\n";
    arena.reset();
    std::cout << "After reset - Used: " << arena.used() << " bytes\n";
    std::cout << "After reset - Available: " << arena.available() << " bytes\n";
}

// 混合大小分配
void mixed_size_demo() {
    std::cout << "\n=== Mixed Size Allocation Demo ===\n\n";

    static uint8_t arena_mem[2048];
    BumpAllocator arena(arena_mem, sizeof(arena_mem));

    // 分配不同大小的对象
    struct Small { char data[16]; };
    struct Medium { char data[64]; };
    struct Large { char data[256]; };

    void* p1 = arena.allocate(sizeof(Small), alignof(Small));
    void* p2 = arena.allocate(sizeof(Medium), alignof(Medium));
    void* p3 = arena.allocate(sizeof(Large), alignof(Large));
    void* p4 = arena.allocate(sizeof(Small), alignof(Small));

    std::cout << "Allocations:\n";
    std::cout << "  Small:  " << p1 << "\n";
    std::cout << "  Medium: " << p2 << "\n";
    std::cout << "  Large:  " << p3 << "\n";
    std::cout << "  Small:  " << p4 << "\n";

    std::cout << "\nUsed: " << arena.used() << " bytes\n";

    // 尝试分配太大的对象
    void* p5 = arena.allocate(4096, 8);
    std::cout << "\nTrying to allocate 4096 bytes: " << (p5 ? "succeeded" : "failed (expected)") << "\n";
}

// Arena生命周期管理
class ScopedArena {
    BumpAllocator& arena_;
    size_t initial_offset_;

public:
    explicit ScopedArena(BumpAllocator& arena)
        : arena_(arena), initial_offset_(arena.used()) {}

    ~ScopedArena() {
        // 析构时回退到初始位置
        // 注意：这不会调用析构函数！
        arena_.reset();
        arena_ = BumpAllocator(
            reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&arena_) - initial_offset_),
            arena_.capacity()
        );
    }

    // 禁止拷贝和移动
    ScopedArena(const ScopedArena&) = delete;
    ScopedArena& operator=(const ScopedArena&) = delete;
};

void arena_region_demo() {
    std::cout << "\n=== Arena Region Demo ===\n\n";

    static uint8_t arena_mem[4096];
    BumpAllocator arena(arena_mem, sizeof(arena_mem));

    // 使用1：初始化阶段
    std::cout << "--- Initialization Phase ---\n";
    size_t init_start = arena.used();
    void* p1 = arena.allocate(sizeof(int), alignof(int));
    void* p2 = arena.allocate(sizeof(double), alignof(double));
    new (p1) int(42);
    new (p2) double(3.14);
    std::cout << "Init used: " << (arena.used() - init_start) << " bytes\n";

    // 使用2：运行时临时对象
    std::cout << "\n--- Runtime Phase ---\n";
    size_t runtime_start = arena.used();

    // 临时分配
    void* temp1 = arena.allocate(128, 8);
    void* temp2 = arena.allocate(64, 8);

    std::cout << "Runtime used: " << (arena.used() - runtime_start) << " bytes\n";

    // 运行时结束，回退
    std::cout << "\n--- Reset to init phase ---\n";
    // 保存init对象，回退runtime分配
    int* init_int = static_cast<int*>(p1);
    double* init_double = static_cast<double*>(p2);
    size_t init_used = arena.used() - runtime_start;

    // 这不是正确的做法，需要保存init对象数据
    // 实际应用中应该使用分层arena
}

// 分层Arena
class TieredArena {
    static uint8_t memory_[8192];
    BumpAllocator permanent_;   // 永久对象
    BumpAllocator frame_;        // 帧临时对象

public:
    TieredArena()
        : permanent_(memory_, 4096)
        , frame_(memory_ + 4096, 4096) {}

    void* allocate_permanent(size_t n, size_t align = alignof(std::max_align_t)) {
        return permanent_.allocate(n, align);
    }

    void* allocate_frame(size_t n, size_t align = alignof(std::max_align_t)) {
        return frame_.allocate(n, align);
    }

    void reset_frame() {
        frame_.reset();
    }

    size_t permanent_used() const { return permanent_.used(); }
    size_t frame_used() const { return frame_.used(); }
};

uint8_t TieredArena::memory_[8192];

void tiered_arena_demo() {
    std::cout << "\n=== Tiered Arena Demo ===\n\n";

    TieredArena arena;

    std::cout << "--- Permanent allocations ---\n";
    void* p1 = arena.allocate_permanent(sizeof(int), alignof(int));
    void* p2 = arena.allocate_permanent(sizeof(double), alignof(double));
    new (p1) int(100);
    new (p2) double(2.718);
    std::cout << "Permanent used: " << arena.permanent_used() << " bytes\n";

    std::cout << "\n--- Frame allocations ---\n";
    void* f1 = arena.allocate_frame(128, 8);
    void* f2 = arena.allocate_frame(256, 8);
    std::cout << "Frame used: " << arena.frame_used() << " bytes\n";

    std::cout << "\n--- Reset frame ---\n";
    arena.reset_frame();
    std::cout << "After reset - Frame used: " << arena.frame_used() << " bytes\n";
    std::cout << "Permanent still: " << arena.permanent_used() << " bytes\n";
}

int main() {
    basic_bump_demo();
    mixed_size_demo();
    arena_region_demo();
    tiered_arena_demo();

    std::cout << "\n=== Key Takeaways ===\n";
    std::cout << "1. Bump allocator is extremely fast (pointer arithmetic)\n";
    std::cout << "2. No fragmentation, but can't free individual objects\n";
    std::cout << "3. Perfect for initialization or frame-based allocation\n";
    std::cout << "4. reset() doesn't call destructors - manage manually\n";
    std::cout << "5. Use tiered arenas for mixed lifetime requirements\n";

    return 0;
}
