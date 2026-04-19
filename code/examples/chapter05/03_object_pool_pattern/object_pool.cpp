#include <iostream>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <cassert>

// 对象池实现 - 文章中的完整代码

// 简单断言（可替换为项目断言）
#ifndef EP_ASSERT
#define EP_ASSERT(x) assert(x)
#endif

// ========== 同步策略接口 ==========

struct NoLockPolicy {
    static void lock() {}
    static void unlock() {}
};

// 关中断保护（伪代码，需由平台实现）
struct InterruptLockPolicy {
    static inline unsigned primask_save() {
        unsigned p = 0;
        // 实际实现需要读取 PRIMASK 寄存器
        return p;
    }
    static inline void primask_restore(unsigned p) {
        // 实际实现需要写 PRIMASK 寄存器
    }
    unsigned state;
    InterruptLockPolicy() : state(primask_save()) {}
    ~InterruptLockPolicy() { primask_restore(state); }
};

// 基于 mutex 的保护（RTOS）
struct MutexLockPolicy {
    static void lock() {
        // 在平台文件中实现
        // xSemaphoreTake(mutex, portMAX_DELAY);
    }
    static void unlock() {
        // xSemaphoreGive(mutex);
    }
};

// ========== 对象池实现 ==========

template<typename T, size_t N, typename Sync = NoLockPolicy>
class ObjectPool {
public:
    static_assert(N > 0, "Pool size must be > 0");
    static_assert(std::is_default_constructible<T>::value || std::is_trivially_default_constructible<T>::value,
                  "T must be default constructible or trivially default constructible");

    ObjectPool() {
        for (size_t i = 0; i < N; ++i) {
            next_idx_[i] = (i + 1 < N) ? i + 1 : kInvalidIndex;
        }
        free_head_ = 0;
    }

    // 非阻塞借出，耗尽返回 nullptr
    T* try_acquire() {
        Sync::lock();
        if (free_head_ == kInvalidIndex) {
            Sync::unlock();
            return nullptr;
        }
        size_t idx = free_head_;
        free_head_ = next_idx_[idx];
        used_count_++;
        Sync::unlock();

        T* obj = reinterpret_cast<T*>(&storage_[idx]);
        // placement-new 初始化
        new (obj) T();
        return obj;
    }

    // 阻塞式借出（断言版本）
    T* acquire() {
        T* obj = try_acquire();
        EP_ASSERT(obj != nullptr && "Pool exhausted");
        return obj;
    }

    // 归还对象（必须来自本池）
    void release(T* obj) {
        EP_ASSERT(obj != nullptr);
        size_t idx = ptr_to_index(obj);
        EP_ASSERT(idx < N);

        // 调用析构
        obj->~T();

        Sync::lock();
        next_idx_[idx] = free_head_;
        free_head_ = idx;
        used_count_--;
        Sync::unlock();
    }

    // 获取当前空闲/已用数量
    size_t free_count() const {
        return N - used_count_;
    }
    size_t used_count() const { return used_count_; }
    size_t capacity() const { return N; }

private:
    static constexpr size_t kInvalidIndex = static_cast<size_t>(-1);
    // 未初始化的原始存储
    typename std::aligned_storage<sizeof(T), alignof(T)>::type storage_[N];
    size_t next_idx_[N];
    size_t free_head_ = kInvalidIndex;
    size_t used_count_ = 0;

    size_t ptr_to_index(T* ptr) {
        uintptr_t base = reinterpret_cast<uintptr_t>(&storage_[0]);
        uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
        EP_ASSERT(p >= base);
        size_t offset = (p - base) / sizeof(storage_[0]);
        return offset;
    }
};

// ========== 使用示例 ==========

// 网络包缓冲对象
struct Packet {
    uint8_t buf[256];
    size_t len;

    Packet() : len(0) {
        std::cout << "Packet constructed\n";
    }

    ~Packet() {
        std::cout << "Packet destructed\n";
    }

    void init() {
        len = 0;
        for (size_t i = 0; i < sizeof(buf); ++i) {
            buf[i] = 0;
        }
    }

    void append(const char* data, size_t n) {
        size_t copy_len = (len + n <= sizeof(buf)) ? n : sizeof(buf) - len;
        std::memcpy(buf + len, data, copy_len);
        len += copy_len;
    }

    void print() const {
        std::cout << "Packet[len=" << len << "] ";
        for (size_t i = 0; i < len && i < 20; ++i) {
            std::cout << static_cast<char>(buf[i]);
        }
        if (len > 20) std::cout << "...";
        std::cout << "\n";
    }
};

void packet_pool_demo() {
    std::cout << "=== Packet Pool Demo ===\n\n";

    // 全局或模块静态区分配池
    static ObjectPool<Packet, 4, NoLockPolicy> pktPool;

    std::cout << "Pool capacity: " << pktPool.capacity() << "\n";
    std::cout << "Free slots: " << pktPool.free_count() << "\n\n";

    std::cout << "--- Acquiring packets ---\n";
    Packet* p1 = pktPool.try_acquire();
    Packet* p2 = pktPool.try_acquire();
    Packet* p3 = pktPool.try_acquire();
    Packet* p4 = pktPool.try_acquire();

    std::cout << "Free slots after acquiring 4: " << pktPool.free_count() << "\n";

    std::cout << "\n--- Trying to acquire from empty pool ---\n";
    Packet* p5 = pktPool.try_acquire();
    if (!p5) {
        std::cout << "Pool exhausted (expected)\n";
    }

    std::cout << "\n--- Using packets ---\n";
    p1->init();
    p1->append("Hello, ", 7);
    p1->append("World!", 6);
    p1->print();

    p2->init();
    p2->append("Packet 2", 8);
    p2->print();

    std::cout << "\n--- Releasing packets ---\n";
    pktPool.release(p1);
    std::cout << "After releasing p1, free slots: " << pktPool.free_count() << "\n";

    pktPool.release(p2);
    std::cout << "After releasing p2, free slots: " << pktPool.free_count() << "\n";

    std::cout << "\n--- Acquire again ---\n";
    Packet* p6 = pktPool.try_acquire();
    if (p6) {
        p6->init();
        p6->append("Reused packet", 13);
        p6->print();
    }

    // 清理
    pktPool.release(p3);
    pktPool.release(p4);
    pktPool.release(p6);
}

// 演示自定义同步策略
void thread_safe_pool_demo() {
    std::cout << "\n=== Thread-Safe Pool Demo ===\n\n";

    // 使用 MutexLockPolicy 的池（RTOS环境）
    // static ObjectPool<Packet, 8, MutexLockPolicy> threadSafePool;

    // 使用 InterruptLockPolicy 的池（ISR环境）
    static ObjectPool<Packet, 8, InterruptLockPolicy> isrSafePool;

    std::cout << "ISR-safe pool capacity: " << isrSafePool.capacity() << "\n";
    std::cout << "Free slots: " << isrSafePool.free_count() << "\n";
}

// RAII包装器
template<typename T, typename PoolType>
class PooledPtr {
    T* ptr_;
    PoolType* pool_;

public:
    PooledPtr(T* ptr, PoolType* pool) : ptr_(ptr), pool_(pool) {}

    ~PooledPtr() {
        if (ptr_) {
            pool_->release(ptr_);
        }
    }

    // 禁止拷贝
    PooledPtr(const PooledPtr&) = delete;
    PooledPtr& operator=(const PooledPtr&) = delete;

    // 移动支持
    PooledPtr(PooledPtr&& other) noexcept : ptr_(other.ptr_), pool_(other.pool_) {
        other.ptr_ = nullptr;
    }

    T* operator->() { return ptr_; }
    T& operator*() { return *ptr_; }
    T* get() { return ptr_; }

    explicit operator bool() const { return ptr_ != nullptr; }
};

void raii_pool_demo() {
    std::cout << "\n=== RAII Pool Wrapper Demo ===\n\n";

    static ObjectPool<Packet, 4, NoLockPolicy> pool;

    {
        auto p1 = PooledPtr<Packet, decltype(pool)>(pool.try_acquire(), &pool);
        auto p2 = PooledPtr<Packet, decltype(pool)>(pool.try_acquire(), &pool);

        if (p1) {
            p1->init();
            p1->append("RAII packet", 11);
            p1->print();
        }

        std::cout << "Free slots inside scope: " << pool.free_count() << "\n";
        // 自动释放
    }

    std::cout << "Free slots after scope: " << pool.free_count() << "\n";
}

int main() {
    packet_pool_demo();
    thread_safe_pool_demo();
    raii_pool_demo();

    std::cout << "\n=== Key Takeaways ===\n";
    std::cout << "1. Object pool provides deterministic memory management\n";
    std::cout << "2. Use try_acquire() for non-blocking, acquire() for assert-on-fail\n";
    std::cout << "3. Always release objects back to the pool\n";
    std::cout << "4. Use RAII wrapper for exception safety\n";
    std::cout << "5. Choose sync policy based on context (none, ISR, RTOS)\n";

    return 0;
}
