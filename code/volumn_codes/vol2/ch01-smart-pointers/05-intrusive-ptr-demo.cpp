/**
 * @file 05-intrusive-ptr-demo.cpp
 * @brief 侵入式引用计数实现与使用示例
 * @details 实现 intrusive_ptr 并演示其与 shared_ptr 的区别
 * @compile g++ -std=c++17 -O2 -o 05-intrusive-ptr-demo 05-intrusive-ptr-demo.cpp
 * @run ./05-intrusive-ptr-demo
 */

#include <iostream>
#include <cstdint>
#include <atomic>

// 侵入式引用计数基类
class RefCounted {
public:
    void add_ref() noexcept {
        // 注意：多线程环境下需要用 std::atomic<uint32_t>
        ++ref_count_;
    }

    void release() noexcept {
        if (--ref_count_ == 0) {
            delete this;
        }
    }

    uint32_t ref_count() const noexcept { return ref_count_; }

protected:
    RefCounted() = default;
    virtual ~RefCounted() = default;

    // 禁止拷贝和移动
    RefCounted(const RefCounted&) = delete;
    RefCounted& operator=(const RefCounted&) = delete;

private:
    uint32_t ref_count_{1};  // 创建时默认持有一次引用
};

// 侵入式智能指针
template <typename T>
class IntrusivePtr {
public:
    IntrusivePtr() noexcept = default;

    explicit IntrusivePtr(T* p) noexcept : ptr_(p) {
        // 不调用 add_ref，因为 RefCounted 创建时 ref_count_ 已经是 1
    }

    IntrusivePtr(const IntrusivePtr& other) noexcept : ptr_(other.ptr_) {
        if (ptr_) {
            ptr_->add_ref();
        }
    }

    IntrusivePtr& operator=(const IntrusivePtr& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            if (ptr_) {
                ptr_->add_ref();
            }
        }
        return *this;
    }

    IntrusivePtr(IntrusivePtr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    IntrusivePtr& operator=(IntrusivePtr&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    ~IntrusivePtr() {
        reset();
    }

    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    T* get() const noexcept { return ptr_; }

    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    void reset() noexcept {
        if (ptr_) {
            ptr_->release();
            ptr_ = nullptr;
        }
    }

    uint32_t use_count() const noexcept {
        return ptr_ ? ptr_->ref_count() : 0;
    }

private:
    T* ptr_ = nullptr;
};

// 示例：共享缓冲区
class SharedBuffer : public RefCounted {
public:
    explicit SharedBuffer(size_t size) : size_(size), data_(new char[size]) {
        std::cout << "SharedBuffer 构造 (" << size << " bytes)\n";
    }

    ~SharedBuffer() override {
        std::cout << "SharedBuffer 析构\n";
        delete[] data_;
    }

    char* data() noexcept { return data_; }
    size_t size() const noexcept { return size_; }

private:
    size_t size_;
    char* data_;
};

void intrusive_demo() {
    std::cout << "=== 侵入式引用计数演示 ===\n\n";

    IntrusivePtr<SharedBuffer> buf(new SharedBuffer(1024));
    std::cout << "创建后引用计数: " << buf.use_count() << "\n";

    {
        auto buf2 = buf;
        std::cout << "拷贝后引用计数: " << buf.use_count() << "\n";
        std::cout << "使用缓冲区地址: " << static_cast<void*>(buf2->data()) << "\n";
    }

    std::cout << "buf2 离开作用域后引用计数: " << buf.use_count() << "\n";
    std::cout << "缓冲区仍然有效\n";
}

// 多线程版本的引用计数基类
class AtomicRefCounted {
public:
    void add_ref() noexcept {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void release() noexcept {
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }

    uint32_t ref_count() const noexcept {
        return ref_count_.load(std::memory_order_relaxed);
    }

protected:
    AtomicRefCounted() = default;
    virtual ~AtomicRefCounted() = default;

    AtomicRefCounted(const AtomicRefCounted&) = delete;
    AtomicRefCounted& operator=(const AtomicRefCounted&) = delete;

private:
    std::atomic<uint32_t> ref_count_{1};
};

int main() {
    intrusive_demo();

    std::cout << "\n=== 结论 ===\n";
    std::cout << "1. 侵入式引用计数无需额外堆分配（计数器在对象内）\n";
    std::cout << "2. 单线程环境：普通 uint32_t 计数器，无原子操作开销\n";
    std::cout << "3. 多线程环境：std::atomic<uint32_t> 计数器，有原子操作开销\n";
    std::cout << "4. 比 shared_ptr 更节省内存，但需要修改对象类型（侵入性）\n";

    return 0;
}
