// 侵入式智能指针示例
// 演示如何实现和使用侵入式引用计数智能指针

#include <cstdint>
#include <cstdio>

// ========== 侵入式引用计数基类 ==========

class intrusive_refcount {
public:
    void add_ref() noexcept {
        ++refcount_;
    }

    void release() noexcept {
        if (--refcount_ == 0) {
            destroy();
        }
    }

    uint32_t ref_count() const noexcept {
        return refcount_;
    }

protected:
    intrusive_refcount() = default;
    virtual ~intrusive_refcount() = default;

private:
    void destroy() noexcept {
        // 注意：在嵌入式语境下，这里只调用析构函数
        // 而不 delete this，因为内存可能来自静态区或内存池
        this->~intrusive_refcount();
    }

    uint32_t refcount_{0};
};

// ========== 侵入式智能指针实现 ==========

template<typename T>
class intrusive_ptr {
public:
    intrusive_ptr() noexcept = default;

    explicit intrusive_ptr(T* p) noexcept : ptr_(p) {
        if (ptr_) ptr_->add_ref();
    }

    intrusive_ptr(const intrusive_ptr& other) noexcept : ptr_(other.ptr_) {
        if (ptr_) ptr_->add_ref();
    }

    intrusive_ptr(intrusive_ptr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    ~intrusive_ptr() noexcept {
        reset();
    }

    intrusive_ptr& operator=(const intrusive_ptr& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            if (ptr_) ptr_->add_ref();
        }
        return *this;
    }

    intrusive_ptr& operator=(intrusive_ptr&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    T* get() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }

    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    void reset() noexcept {
        if (ptr_) {
            ptr_->release();
            ptr_ = nullptr;
        }
    }

private:
    T* ptr_{nullptr};
};

// ========== 使用示例 ==========

// 示例 1: 简单的可共享对象
class SharedBuffer : public intrusive_refcount {
public:
    static constexpr size_t SIZE = 256;

    explicit SharedBuffer(int id) : id_(id) {
        printf("SharedBuffer %d constructed\n", id_);
    }

    ~SharedBuffer() override {
        printf("SharedBuffer %d destroyed (refcount was %u)\n",
               id_, ref_count());
    }

    void write(const char* data) {
        snprintf(buffer_, SIZE, "%s", data);
    }

    const char* read() const {
        return buffer_;
    }

    int id() const { return id_; }

private:
    int id_;
    char buffer_[SIZE]{0};
};

// 示例 2: 非堆分配对象（静态存储）
class StaticDevice : public intrusive_refcount {
public:
    StaticDevice() {
        printf("StaticDevice constructed (static storage)\n");
    }

    ~StaticDevice() override {
        printf("StaticDevice destroyed (but memory not freed)\n");
    }

    void send(const char* msg) {
        printf("Device sending: %s\n", msg);
    }
};

// 静态存储的对象
alignas(StaticDevice) unsigned char device_storage[sizeof(StaticDevice)];

// ========== 测试函数 ==========

void basic_usage() {
    printf("=== Basic intrusive_ptr Usage ===\n");

    intrusive_ptr<SharedBuffer> buf1(new SharedBuffer(1));
    printf("buf1 created, refcount: %u\n", buf1->ref_count());

    {
        intrusive_ptr<SharedBuffer> buf2 = buf1;
        printf("After copy, buf1 refcount: %u\n", buf1->ref_count());
        printf("buf2 id: %d\n", buf2->id());
    }

    printf("After buf2 destroyed, buf1 refcount: %u\n", buf1->ref_count());
}

void move_semantics() {
    printf("\n=== Move Semantics ===\n");

    intrusive_ptr<SharedBuffer> buf1(new SharedBuffer(2));
    intrusive_ptr<SharedBuffer> buf2 = std::move(buf1);

    printf("After move:\n");
    printf("  buf1 is null: %s\n", !buf1 ? "yes" : "no");
    printf("  buf2 id: %d, refcount: %u\n", buf2->id(), buf2->ref_count());
}

void container_usage() {
    printf("\n=== Container Usage ===\n");

    // 注意：intrusive_ptr 的拷贝成本低（只有计数器操作）
    // 没有 shared_ptr 那样的原子操作开销
}

void static_storage_example() {
    printf("\n=== Static Storage Example ===\n");

    // 在静态存储区构造对象
    StaticDevice* dev = new(device_storage) StaticDevice();

    intrusive_ptr<StaticDevice> dev_ptr(dev);
    dev_ptr->send("Hello from static device");

    {
        intrusive_ptr<StaticDevice> dev_ptr2 = dev_ptr;
        dev_ptr2->send("Shared reference");
    }

    // 手动调用析构，但内存不释放
    dev_ptr.reset();
    printf("Static device destroyed but memory still allocated\n");
}

// 多态支持
class Animal : public intrusive_refcount {
public:
    virtual ~Animal() = default;
    virtual void speak() const = 0;
};

class Dog : public Animal {
public:
    void speak() const override {
        printf("Woof!\n");
    }
    ~Dog() override {
        printf("Dog destroyed\n");
    }
};

void polymorphism_example() {
    printf("\n=== Polymorphism Support ===\n");

    intrusive_ptr<Animal> pet(new Dog());
    pet->speak();

    intrusive_ptr<Animal> pet2 = pet;
    pet2->speak();
}

int main() {
    basic_usage();
    move_semantics();
    container_usage();
    static_storage_example();
    polymorphism_example();

    printf("\n=== All Examples Complete ===\n");
    printf("\nKey Advantages of Intrusive Pointers:\n");
    printf("- No separate control block allocation\n");
    printf("- Reference count is part of the object\n");
    printf("- Works with non-heap allocated objects\n");
    printf("- No atomic operations overhead (unless you add them)\n");

    return 0;
}
