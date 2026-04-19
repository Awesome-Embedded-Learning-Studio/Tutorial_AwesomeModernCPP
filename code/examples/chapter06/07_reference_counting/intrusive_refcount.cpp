// 侵入式引用计数示例 - 单线程版本
// 演示适合嵌入式环境的轻量级引用计数实现

#include <cstdint>
#include <cstdio>
#include <utility>

// ========== 单线程侵入式引用计数基类 ==========

class IntrusiveRefBase {
protected:
    uint16_t ref_count_{1};  // 默认持有者为创建者

    virtual ~IntrusiveRefBase() = default;

public:
    void retain() noexcept {
        ++ref_count_;
    }

    void release() noexcept {
        if (--ref_count_ == 0) {
            delete this;
        }
    }

    uint16_t ref_count() const noexcept {
        return ref_count_;
    }
};

// ========== 单线程智能指针 ==========

template <typename T>
class IntrusivePtr {
    T* p_;

public:
    IntrusivePtr(T* p = nullptr) : p_(p) {}

    IntrusivePtr(const IntrusivePtr& o) : p_(o.p_) {
        if (p_) p_->retain();
    }

    IntrusivePtr(IntrusivePtr&& o) noexcept : p_(o.p_) {
        o.p_ = nullptr;
    }

    ~IntrusivePtr() {
        if (p_) p_->release();
    }

    IntrusivePtr& operator=(const IntrusivePtr& o) {
        if (o.p_) o.p_->retain();
        if (p_) p_->release();
        p_ = o.p_;
        return *this;
    }

    IntrusivePtr& operator=(IntrusivePtr&& o) noexcept {
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

    void reset() {
        if (p_) {
            p_->release();
            p_ = nullptr;
        }
    }

    uint16_t use_count() const noexcept {
        return p_ ? p_->ref_count() : 0;
    }
};

// ========== 使用示例 ==========

class SharedResource : public IntrusiveRefBase {
public:
    int id;
    int data[64];  // 模拟一些数据

    explicit SharedResource(int i) : id(i) {
        printf("SharedResource %d created\n", id);
    }

    ~SharedResource() override {
        printf("SharedResource %d destroyed (final refcount: %u)\n",
               id, ref_count());
    }

    void process() {
        printf("Processing resource %d\n", id);
    }
};

void basic_usage() {
    printf("=== Basic Intrusive Reference Counting ===\n");

    IntrusivePtr<SharedResource> res1(new SharedResource(1));
    printf("After creation: use_count = %u\n", res1.use_count());

    {
        IntrusivePtr<SharedResource> res2 = res1;
        printf("After copy: use_count = %u\n", res1.use_count());

        IntrusivePtr<SharedResource> res3 = res2;
        printf("After second copy: use_count = %u\n", res1.use_count());
    }

    printf("After inner scope: use_count = %u\n", res1.use_count());
}

void move_semantics() {
    printf("\n=== Move Semantics ===\n");

    IntrusivePtr<SharedResource> res1(new SharedResource(2));
    printf("Initial: res1.use_count = %u\n", res1.use_count());

    IntrusivePtr<SharedResource> res2 = std::move(res1);
    printf("After move:\n");
    printf("  res1 is null: %s\n", !res1 ? "yes" : "no");
    printf("  res2.use_count = %u\n", res2.use_count());
}

void container_usage() {
    printf("\n=== Container Usage ===\n");

    // 简单的固定容量容器
    struct ResourceHolder {
        IntrusivePtr<SharedResource> resources[4];
        size_t count = 0;

        void add(const IntrusivePtr<SharedResource>& res) {
            if (count < 4) {
                resources[count++] = res;
            }
        }

        void print_all() {
            for (size_t i = 0; i < count; ++i) {
                printf("  resources[%zu]: id=%d, refcount=%u\n",
                       i, resources[i]->id, resources[i].use_count());
            }
        }
    };

    ResourceHolder holder;
    IntrusivePtr<SharedResource> res(new SharedResource(3));

    printf("Original refcount: %u\n", res.use_count());

    holder.add(res);
    printf("After adding to holder: %u\n", res.use_count());

    holder.add(res);
    printf("After adding again: %u\n", res.use_count());

    holder.print_all();
}

// 多态示例
class Message : public IntrusiveRefBase {
public:
    virtual ~Message() = default;
    virtual void execute() = 0;
};

class PrintMessage : public Message {
public:
    const char* text;

    explicit PrintMessage(const char* t) : text(t) {
        printf("PrintMessage created: %s\n", text);
    }

    ~PrintMessage() override {
        printf("PrintMessage destroyed\n");
    }

    void execute() override {
        printf("Executing: %s\n", text);
    }
};

class BeepMessage : public Message {
public:
    int frequency;

    explicit BeepMessage(int f) : frequency(f) {
        printf("BeepMessage created: %dHz\n", f);
    }

    ~BeepMessage() override {
        printf("BeepMessage destroyed\n");
    }

    void execute() override {
        printf("Beeping at %dHz\n", frequency);
    }
};

void polymorphism_example() {
    printf("\n=== Polymorphism Example ===\n");

    IntrusivePtr<Message> msg1(new PrintMessage("Hello, World!"));
    IntrusivePtr<Message> msg2(new BeepMessage(440));

    msg1->execute();
    msg2->execute();
}

// 性能对比（概念性）
void performance_notes() {
    printf("\n=== Performance Notes ===\n");
    printf("Intrusive reference counting advantages:\n");
    printf("  - No separate control block allocation\n");
    printf("  - No atomic operations (in this single-threaded version)\n");
    printf("  - Better cache locality (counter is in the object)\n");
    printf("  - Predictable memory layout\n");
    printf("\nSize comparison:\n");
    printf("  sizeof(IntrusivePtr<SharedResource>): %zu\n",
           sizeof(IntrusivePtr<SharedResource>));
    printf("  sizeof(SharedResource*): %zu\n", sizeof(SharedResource*));
}

int main() {
    basic_usage();
    move_semantics();
    container_usage();
    polymorphism_example();
    performance_notes();

    printf("\n=== All Examples Complete ===\n");

    return 0;
}
