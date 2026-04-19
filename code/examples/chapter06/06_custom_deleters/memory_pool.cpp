// 自定义删除器示例 - 内存池管理
// 演示如何使用自定义删除器配合内存池使用智能指针

#include <memory>
#include <cstdio>
#include <cstdint>
#include <cstring>

// ========== 简单的内存池实现 ==========

class MemoryPool {
public:
    static constexpr size_t POOL_SIZE = 4096;
    static constexpr size_t MAX_ALLOCATIONS = 64;

    struct Allocation {
        void* ptr;
        size_t size;
        bool in_use;
    };

    MemoryPool() : offset_(0), allocation_count_(0) {
        printf("[Pool] Memory pool created, size=%zu\n", POOL_SIZE);
    }

    ~MemoryPool() {
        printf("[Pool] Memory pool destroyed\n");
        printf("[Pool] Remaining allocations: %zu\n", allocation_count_);
    }

    void* allocate(size_t size) {
        if (allocation_count_ >= MAX_ALLOCATIONS) {
            printf("[Pool] ERROR: Max allocations reached\n");
            return nullptr;
        }

        if (offset_ + size > POOL_SIZE) {
            printf("[Pool] ERROR: Out of memory (need %zu, have %zu)\n",
                   size, POOL_SIZE - offset_);
            return nullptr;
        }

        void* ptr = pool_ + offset_;
        offset_ += size;

        // 记录分配
        allocations_[allocation_count_].ptr = ptr;
        allocations_[allocation_count_].size = size;
        allocations_[allocation_count_].in_use = true;
        allocation_count_++;

        printf("[Pool] Allocated %zu bytes at %p (total: %zu/%zu)\n",
               size, ptr, offset_, POOL_SIZE);
        return ptr;
    }

    void deallocate(void* ptr) {
        if (!ptr) return;

        for (size_t i = 0; i < allocation_count_; ++i) {
            if (allocations_[i].ptr == ptr && allocations_[i].in_use) {
                allocations_[i].in_use = false;
                printf("[Pool] Deallocated %p (size: %zu)\n",
                       ptr, allocations_[i].size);
                return;
            }
        }
        printf("[Pool] WARNING: Pointer %p not found in pool\n", ptr);
    }

    size_t get_used() const { return offset_; }
    size_t get_available() const { return POOL_SIZE - offset_; }
    size_t get_allocation_count() const { return allocation_count_; }

private:
    char pool_[POOL_SIZE];
    size_t offset_;
    size_t allocation_count_;
    Allocation allocations_[MAX_ALLOCATIONS];
};

// ========== 全局内存池 ==========
MemoryPool g_pool;

// ========== 内存池删除器 ==========

template<typename Pool>
struct PoolDeleter {
    Pool* pool;

    void operator()(void* p) noexcept {
        if (pool && p) {
            pool->deallocate(p);
        }
    }
};

// ========== 便捷别名 ==========

template<typename T>
using PoolPtr = std::unique_ptr<T, PoolDeleter<MemoryPool>>;

// ========== 使用示例 ==========

struct SensorData {
    int sensor_id;
    float value;
    uint32_t timestamp;

    SensorData(int id, float v, uint32_t ts)
        : sensor_id(id), value(v), timestamp(ts) {
        printf("SensorData constructed: id=%d, value=%.2f\n", id, v);
    }

    SensorData() : sensor_id(0), value(0.0f), timestamp(0) {}

    ~SensorData() {
        printf("SensorData destroyed: id=%d\n", sensor_id);
    }

    void print() const {
        printf("  [Sensor %d] value=%.2f, ts=%u\n",
               sensor_id, value, timestamp);
    }
};

// 从池中分配对象
template<typename T, typename... Args>
PoolPtr<T> make_pooled(Args&&... args) {
    void* mem = g_pool.allocate(sizeof(T));
    if (!mem) {
        throw std::bad_alloc();
    }

    T* obj = new(mem) T(std::forward<Args>(args)...);
    return PoolPtr<T>(obj, PoolDeleter<MemoryPool>{&g_pool});
}

void basic_pool_usage() {
    printf("=== Basic Pool Usage ===\n");

    {
        auto data1 = make_pooled<SensorData>(1, 23.5f, 1000);
        auto data2 = make_pooled<SensorData>(2, 45.2f, 1001);

        data1->print();
        data2->print();

        printf("\nPool status: used=%zu, avail=%zu\n",
               g_pool.get_used(), g_pool.get_available());
    }

    printf("\nAfter scope:\n");
    printf("Pool status: used=%zu, avail=%zu\n",
           g_pool.get_used(), g_pool.get_available());
}

// 数组分配
template<typename T>
PoolPtr<T> make_pooled_array(size_t count) {
    void* mem = g_pool.allocate(sizeof(T) * count);
    if (!mem) {
        throw std::bad_alloc();
    }

    T* arr = new(mem) T[count];
    return PoolPtr<T>(arr, PoolDeleter<MemoryPool>{&g_pool});
}

void array_pool_usage() {
    printf("\n=== Array Pool Usage ===\n");

    {
        auto buffer = make_pooled_array<uint8_t>(128);
        printf("Allocated 128 bytes array in pool\n");

        // 使用 buffer
        std::memset(buffer.get(), 0xAA, 128);
        printf("Buffer[0] = 0x%02X\n", buffer.get()[0]);
    }

    printf("Array deallocated, back to pool\n");
}

// shared_ptr 与内存池
void shared_ptr_pool_usage() {
    printf("\n=== shared_ptr with Pool ===\n");

    // shared_ptr 也支持自定义删除器
    // 但注意控制块仍会从堆分配
    auto data = std::shared_ptr<SensorData>(
        new(g_pool.allocate(sizeof(SensorData))) SensorData(99, 100.0f, 2000),
        [](SensorData* p) {
            p->~SensorData();
            g_pool.deallocate(p);
        }
    );

    data->print();
    printf("use_count: %ld\n", data.use_count());
}

// 对象池模式（预构造对象）
template<typename T, size_t N>
class ObjectPool {
public:
    ObjectPool() {
        for (size_t i = 0; i < N; ++i) {
            free_list_[i] = &objects_[i];
        }
        printf("[ObjectPool] Created pool of %zu objects\n", N);
    }

    template<typename... Args>
    T* acquire(Args&&... args) {
        // 简化实现：总是返回第一个可用对象
        for (size_t i = 0; i < N; ++i) {
            if (!in_use_[i]) {
                in_use_[i] = true;
                return &objects_[i];
            }
        }
        return nullptr;
    }

    void release(T* obj) {
        for (size_t i = 0; i < N; ++i) {
            if (&objects_[i] == obj) {
                in_use_[i] = false;
                return;
            }
        }
    }

private:
    T objects_[N];
    bool in_use_[N] = {false};
    T* free_list_[N];
};

void object_pool_example() {
    printf("\n=== Object Pool Example ===\n");

    ObjectPool<SensorData, 4> pool;

    SensorData* s1 = pool.acquire(10, 1.0f, 0);
    SensorData* s2 = pool.acquire(20, 2.0f, 0);

    if (s1 && s2) {
        s1->print();
        s2->print();
    }

    pool.release(s1);
    pool.release(s2);

    printf("Objects returned to pool\n");
}

int main() {
    basic_pool_usage();
    array_pool_usage();
    shared_ptr_pool_usage();
    object_pool_example();

    printf("\n=== All Examples Complete ===\n");

    return 0;
}
