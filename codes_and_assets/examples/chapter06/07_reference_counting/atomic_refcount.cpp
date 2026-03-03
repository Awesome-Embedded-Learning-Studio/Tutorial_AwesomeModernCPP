// 线程安全引用计数示例
// 演示多线程/中断环境下的引用计数实现

#include <cstdint>
#include <cstdio>
#include <atomic>
#include <thread>
#include <vector>

// ========== 线程安全侵入式引用计数基类 ==========

class AtomicRefBase {
protected:
    std::atomic<uint32_t> ref_count_{1};

    virtual ~AtomicRefBase() = default;

public:
    void retain() noexcept {
        // 使用 relaxed 内存序，因为只是增加计数
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void release() noexcept {
        // 使用 acq_rel 内存序确保可见性
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // 最后一个引用，确保所有之前的修改都可见
            std::atomic_thread_fence(std::memory_order_acquire);
            delete this;
        }
    }

    uint32_t ref_count() const noexcept {
        return ref_count_.load(std::memory_order_relaxed);
    }

    bool is_lock_free() const noexcept {
        return ref_count_.is_always_lock_free;
    }
};

// ========== 线程安全智能指针 ==========

template <typename T>
class AtomicPtr {
    T* p_;

public:
    AtomicPtr(T* p = nullptr) : p_(p) {}

    AtomicPtr(const AtomicPtr& o) : p_(o.p_) {
        if (p_) p_->retain();
    }

    AtomicPtr(AtomicPtr&& o) noexcept : p_(o.p_) {
        o.p_ = nullptr;
    }

    ~AtomicPtr() {
        if (p_) p_->release();
    }

    AtomicPtr& operator=(const AtomicPtr& o) {
        if (o.p_) o.p_->retain();
        if (p_) p_->release();
        p_ = o.p_;
        return *this;
    }

    AtomicPtr& operator=(AtomicPtr&& o) noexcept {
        if (this != &o) {
            if (p_) p_->release();
            p_ = o.p_;
            o.p_ = nullptr;
        }
        return *this;
    }

    T* get() const noexcept { return p_; }
    T& operator*() const noexcept { return *p_; }
    T* operator->() const noexcept { return p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }

    uint32_t use_count() const noexcept {
        return p_ ? p_->ref_count() : 0;
    }
};

// ========== 使用示例 ==========

class ThreadSafeResource : public AtomicRefBase {
public:
    int id;
    int value;

    ThreadSafeResource(int i, int v) : id(i), value(v) {
        printf("Resource %d created (value=%d)\n", id, value);
    }

    ~ThreadSafeResource() override {
        printf("Resource %d destroyed (final refcount: %u)\n",
               id, ref_count());
    }

    void increment() {
        ++value;
    }
};

// ========== 多线程测试 ==========

void multi_thread_test() {
    printf("=== Multi-Thread Reference Counting Test ===\n");

    AtomicPtr<ThreadSafeResource> shared(new ThreadSafeResource(1, 0));

    printf("Initial refcount: %u\n", shared.use_count());
    printf("Atomic is lock-free: %s\n",
           shared->is_lock_free() ? "yes" : "no");

    const int NUM_THREADS = 4;
    const int OPERATIONS = 10000;

    std::vector<std::thread> threads;

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&shared, OPERATIONS]() {
            // 每个线程持有自己的拷贝
            AtomicPtr<ThreadSafeResource> local = shared;

            for (int j = 0; j < OPERATIONS; ++j) {
                local->increment();

                // 偶尔创建额外的拷贝增加引用计数
                if (j % 100 == 0) {
                    AtomicPtr<ThreadSafeResource> temp = local;
                    (void)temp;  // 避免未使用警告
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    printf("\nAfter all threads complete:\n");
    printf("  Final refcount: %u\n", shared.use_count());
    printf("  Final value: %d (expected: %d)\n",
           shared->value, NUM_THREADS * OPERATIONS);
}

// ========== 性能对比测试 ==========

void performance_comparison() {
    printf("\n=== Performance Comparison ===\n");

    const int N = 100000;

    // 测试原子引用计数的开销
    auto start = std::chrono::steady_clock::now();
    {
        AtomicPtr<ThreadSafeResource> ptr(new ThreadSafeResource(2, 0));
        for (int i = 0; i < N; ++i) {
            AtomicPtr<ThreadSafeResource> temp = ptr;
            // temp 离开作用域
        }
    }
    auto end = std::chrono::steady_clock::now();
    auto atomic_time = std::chrono::duration_cast<std::chrono::microseconds>(
        end - start).count();

    printf("Atomic refcount (%d cycles): %ld us\n", N, atomic_time);
    printf("Average per copy/destroy: %.3f us\n",
           atomic_time / (2.0 * N));
}

// ========== 内存序说明 ==========

void memory_order_explanation() {
    printf("\n=== Memory Order Explanation ===\n");
    printf("\nWhy relaxed for increment?\n");
    printf("  - We're just tracking how many references exist\n");
    printf("  - No other memory needs to be synchronized yet\n");
    printf("\nWhy acq_rel for decrement to zero?\n");
    printf("  - When reaching zero, we delete the object\n");
    printf("  - Must ensure all prior writes to the object are visible\n");
    printf("\nWhy acquire fence before delete?\n");
    printf("  - Extra safety to guarantee the deleting thread sees everything\n");
}

int main() {
    multi_thread_test();
    performance_comparison();
    memory_order_explanation();

    printf("\n=== All Examples Complete ===\n");

    return 0;
}
