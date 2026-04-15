#include <iostream>
#include <cstddef>
#include <cstdint>
#include <new>
#include <cstring>

// 专用对象池示例 - 针对特定类型优化

// ========== 固定大小整数池 ==========
// 用于存储消息ID、句柄等小对象

template<size_t N>
class UInt32Pool {
    uint32_t storage_[N];
    bool used_[N];
    size_t free_head_;

public:
    UInt32Pool() {
        std::memset(used_, 0, sizeof(used_));
        free_head_ = 0;
    }

    // 分配一个索引，返回uint32_t
    bool allocate(size_t& out_index) {
        for (size_t i = 0; i < N; ++i) {
            if (!used_[free_head_]) {
                used_[free_head_] = true;
                out_index = free_head_;
                // 循环查找下一个空闲
                size_t next = (free_head_ + 1) % N;
                while (next != free_head_ && used_[next]) {
                    next = (next + 1) % N;
                }
                free_head_ = next;
                return true;
            }
        }
        return false;
    }

    void free(size_t index) {
        if (index < N && used_[index]) {
            used_[index] = false;
            if (index < free_head_ || free_head_ == N) {
                free_head_ = index;
            }
        }
    }

    uint32_t& operator[](size_t index) { return storage_[index]; }
    const uint32_t& operator[](size_t index) const { return storage_[index]; }
};

// ========== 位图对象池 ==========
// 使用位图跟踪状态，内存效率高

template<typename T, size_t N>
class BitmapObjectPool {
    alignas(T) uint8_t storage_[N * sizeof(T)];
    uint32_t bitmap_[ (N + 31) / 32 ];  // 每位跟踪一个槽
    size_t last_hint_;

    static constexpr size_t bitmap_size = (N + 31) / 32;

public:
    BitmapObjectPool() : last_hint_(0) {
        std::memset(bitmap_, 0xFF, sizeof(bitmap_));  // 初始全部空闲（1=空闲）
    }

    T* allocate() {
        // 从hint开始搜索
        for (size_t word = 0; word < bitmap_size; ++word) {
            size_t w = (last_hint_ / 32 + word) % bitmap_size;
            if (bitmap_[w] != 0) {  // 有空闲位
                uint32_t bits = bitmap_[w];
                for (size_t bit = 0; bit < 32; ++bit) {
                    if (bits & (1U << bit)) {
                        size_t idx = w * 32 + bit;
                        if (idx < N) {
                            bitmap_[w] &= ~(1U << bit);  // 标记为使用
                            last_hint_ = idx;
                            return reinterpret_cast<T*>(&storage_[idx * sizeof(T)]);
                        }
                    }
                }
            }
        }
        return nullptr;  // 池已满
    }

    void free(T* ptr) {
        auto addr = reinterpret_cast<uint8_t*>(ptr);
        size_t idx = (addr - storage_) / sizeof(T);
        if (idx < N) {
            bitmap_[idx / 32] |= (1U << (idx % 32));  // 标记为空闲
        }
    }

    size_t used_count() const {
        size_t count = 0;
        for (size_t w = 0; w < bitmap_size; ++w) {
            count += __builtin_popcount(~bitmap_[w]);
        }
        return count;
    }

    size_t free_count() const { return N - used_count(); }
};

// ========== 按类型分层对象池 ==========
// 为不同类型提供不同大小和同步策略

struct SmallObject { int data; };
struct MediumObject { int data[16]; };
struct LargeObject { int data[64]; };

template<typename T>
struct PoolTraits;

template<>
struct PoolTraits<SmallObject> {
    static constexpr size_t capacity = 32;
    using SyncPolicy = struct { static void lock() {} static void unlock() {} };
};

template<>
struct PoolTraits<MediumObject> {
    static constexpr size_t capacity = 16;
    using SyncPolicy = struct { static void lock() {} static void unlock() {} };
};

template<>
struct PoolTraits<LargeObject> {
    static constexpr size_t capacity = 8;
    using SyncPolicy = struct { static void lock() {} static void unlock() {} };
};

template<typename T>
class TypedObjectPool {
    alignas(T) uint8_t storage_[PoolTraits<T>::capacity * sizeof(T)];
    uint32_t free_list_[PoolTraits<T>::capacity];
    size_t free_head_;
    size_t used_count_;

    using Sync = typename PoolTraits<T>::SyncPolicy;

public:
    TypedObjectPool() : used_count_(0) {
        for (size_t i = 0; i < PoolTraits<T>::capacity; ++i) {
            free_list_[i] = i + 1;
        }
        free_list_[PoolTraits<T>::capacity - 1] = static_cast<size_t>(-1);
        free_head_ = 0;
    }

    T* allocate() {
        Sync::lock();
        if (free_head_ == static_cast<size_t>(-1)) {
            Sync::unlock();
            return nullptr;
        }
        size_t idx = free_head_;
        free_head_ = free_list_[idx];
        used_count_++;
        Sync::unlock();

        T* obj = reinterpret_cast<T*>(&storage_[idx * sizeof(T)]);
        new (obj) T();
        return obj;
    }

    void free(T* ptr) {
        if (!ptr) return;
        ptr->~T();

        size_t idx = (reinterpret_cast<uint8_t*>(ptr) - storage_) / sizeof(T);

        Sync::lock();
        free_list_[idx] = free_head_;
        free_head_ = idx;
        used_count_--;
        Sync::unlock();
    }

    size_t used() const { return used_count_; }
    size_t capacity() const { return PoolTraits<T>::capacity; }
};

// ========== 使用示例 ==========

void uint32_pool_demo() {
    std::cout << "=== UInt32 Pool Demo ===\n\n";

    UInt32Pool<16> pool;

    std::cout << "Allocating indices:\n";
    for (int i = 0; i < 10; ++i) {
        size_t idx;
        if (pool.allocate(idx)) {
            pool[idx] = i * 100;
            std::cout << "  Allocated index " << idx << " = " << pool[idx] << "\n";
        }
    }

    std::cout << "\nFreeing some indices:\n";
    pool.free(2);
    pool.free(5);
    pool.free(8);

    std::cout << "\nAllocating after free:\n";
    for (int i = 0; i < 3; ++i) {
        size_t idx;
        if (pool.allocate(idx)) {
            pool[idx] = 999;
            std::cout << "  Allocated index " << idx << " = " << pool[idx] << "\n";
        }
    }
}

struct MyData {
    int value;
    char name[32];

    MyData() : value(0) {
        std::strcpy(name, "default");
    }
};

void bitmap_pool_demo() {
    std::cout << "\n=== Bitmap Pool Demo ===\n\n";

    BitmapObjectPool<MyData, 64> pool;

    std::cout << "Pool capacity: " << 64 << "\n";
    std::cout << "Free slots: " << pool.free_count() << "\n";

    MyData* items[10];
    for (int i = 0; i < 10; ++i) {
        items[i] = pool.allocate();
        if (items[i]) {
            items[i]->value = i;
            std::snprintf(items[i]->name, sizeof(items[i]->name), "item%d", i);
        }
    }

    std::cout << "Used slots after allocating 10: " << pool.used_count() << "\n";

    for (int i = 0; i < 10; ++i) {
        if (items[i]) {
            std::cout << "  " << items[i]->name << " = " << items[i]->value << "\n";
        }
    }

    // 释放一些
    for (int i = 0; i < 5; ++i) {
        pool.free(items[i]);
    }

    std::cout << "Used slots after freeing 5: " << pool.used_count() << "\n";
}

void typed_pool_demo() {
    std::cout << "\n=== Typed Pool Demo ===\n\n";

    TypedObjectPool<SmallObject> small_pool;
    TypedObjectPool<MediumObject> medium_pool;
    TypedObjectPool<LargeObject> large_pool;

    std::cout << "SmallObject pool: " << small_pool.used() << "/" << small_pool.capacity() << "\n";
    std::cout << "MediumObject pool: " << medium_pool.used() << "/" << medium_pool.capacity() << "\n";
    std::cout << "LargeObject pool: " << large_pool.used() << "/" << large_pool.capacity() << "\n";

    SmallObject* s = small_pool.allocate();
    MediumObject* m = medium_pool.allocate();
    LargeObject* l = large_pool.allocate();

    if (s) s->data = 1;
    if (m) m->data[0] = 2;
    if (l) l->data[0] = 3;

    std::cout << "\nAfter allocation:\n";
    std::cout << "SmallObject pool: " << small_pool.used() << "/" << small_pool.capacity() << "\n";
    std::cout << "MediumObject pool: " << medium_pool.used() << "/" << medium_pool.capacity() << "\n";
    std::cout << "LargeObject pool: " << large_pool.used() << "/" << large_pool.capacity() << "\n";

    small_pool.free(s);
    medium_pool.free(m);
    large_pool.free(l);
}

int main() {
    uint32_pool_demo();
    bitmap_pool_demo();
    typed_pool_demo();

    std::cout << "\n=== Key Takeaways ===\n";
    std::cout << "1. Specialize pools for specific use cases\n";
    std::cout << "2. Bitmap pool saves memory for many small objects\n";
    std::cout << "3. Typed pool provides type safety with traits\n";
    std::cout << "4. Choose pool implementation based on object size and count\n";

    return 0;
}
