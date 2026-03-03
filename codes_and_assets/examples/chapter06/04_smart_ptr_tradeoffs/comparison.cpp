// 智能指针取舍对比示例
// 演示 unique_ptr 和 shared_ptr 在嵌入式环境下的权衡

#include <memory>
#include <cstdio>
#include <cstdint>

// ========== 外设句柄示例 ============

struct GpioHandle {
    int id;
    explicit GpioHandle(int i) : id(i) {
        printf("GPIO %d initialized\n", id);
    }
    ~GpioHandle() {
        printf("GPIO %d released\n", id);
    }
    void write(bool level) const {
        printf("GPIO %d write %d\n", id, level);
    }
};

void gpio_release(GpioHandle* h) {
    if (h) {
        printf("GPIO release called\n");
        delete h;
    }
}

// 场景 A：外设句柄 - 使用 unique_ptr 更合适
void gpio_unique_ptr_example() {
    printf("=== Scenario A: GPIO with unique_ptr ===\n");

    // 明确的所有权，零额外开销
    using UniqueGpio = std::unique_ptr<GpioHandle, void(*)(GpioHandle*)>;
    UniqueGpio gpio(new GpioHandle(13), gpio_release);

    gpio->write(true);
    gpio->write(false);

    // 离开作用域自动释放
}

// ========== 共享配置示例 ============

struct Config {
    int baudrate;
    int timeout;
    Config() : baudrate(115200), timeout(100) {
        printf("Config created\n");
    }
    ~Config() {
        printf("Config destroyed\n");
    }
};

// 场景 B：共享配置 - 谨慎使用 shared_ptr
void config_shared_ptr_example() {
    printf("\n=== Scenario B: Shared Config with shared_ptr ===\n");

    auto cfg = std::make_shared<const Config>(Config());

    printf("use_count: %ld\n", cfg.use_count());

    // 多个任务共享读取
    auto task1_cfg = cfg;
    auto task2_cfg = cfg;

    printf("After sharing: use_count = %ld\n", cfg.use_count());
    printf("Baudrate: %d\n", cfg->baudrate);

    // 每次拷贝都有原子操作开销
}

// ========== 内存占用对比 ============

void memory_comparison() {
    printf("\n=== Memory Usage Comparison ===\n");

    struct SmallStruct {
        int data[4];
    };

    printf("sizeof(SmallStruct):              %zu bytes\n", sizeof(SmallStruct));
    printf("sizeof(SmallStruct*):             %zu bytes\n", sizeof(SmallStruct*));
    printf("sizeof(unique_ptr<SmallStruct>):  %zu bytes\n",
           sizeof(std::unique_ptr<SmallStruct>));
    printf("sizeof(shared_ptr<SmallStruct>):  %zu bytes\n",
           sizeof(std::shared_ptr<SmallStruct>));

    // 展示控制块的额外分配
    auto up = std::make_unique<SmallStruct>();
    auto sp = std::make_shared<SmallStruct>();

    printf("\nunique_ptr manages: %p (no control block)\n", up.get());
    printf("shared_ptr manages: %p (has separate control block)\n", sp.get());
}

// ========== 性能对比（简化版）==========

void performance_comparison() {
    printf("\n=== Performance Comparison ===\n");

    const int N = 100000;

    // unique_ptr: 只有移动操作
    auto start_unique = std::chrono::steady_clock::now();
    {
        auto p = std::make_unique<int>(42);
        for (int i = 0; i < N; ++i) {
            auto p2 = std::move(p);
            p = std::move(p2);
        }
    }
    auto end_unique = std::chrono::steady_clock::now();
    auto unique_time = std::chrono::duration_cast<std::chrono::microseconds>(
        end_unique - start_unique).count();

    // shared_ptr: 有引用计数操作
    auto start_shared = std::chrono::steady_clock::now();
    {
        auto p = std::make_shared<int>(42);
        for (int i = 0; i < N; ++i) {
            auto p2 = p;
            // p2 离开作用域，引用计数减少
        }
    }
    auto end_shared = std::chrono::steady_clock::now();
    auto shared_time = std::chrono::duration_cast<std::chrono::microseconds>(
        end_shared - start_shared).count();

    printf("unique_ptr move operations: %ld us\n", unique_time);
    printf("shared_ptr copy operations:  %ld us\n", shared_time);
    printf("Ratio: %.2fx\n", shared_time / (double)unique_time);
}

// ========== 侵入式引用计数示例 ============

struct IntrusiveBase {
    mutable uint16_t ref_count = 0;

    void add_ref() const noexcept { ++ref_count; }
    void release() const noexcept {
        if (--ref_count == 0) {
            delete this;
        }
    }

protected:
    virtual ~IntrusiveBase() = default;
};

template<typename T>
class IntrusivePtr {
    T* p = nullptr;
public:
    IntrusivePtr() noexcept = default;
    explicit IntrusivePtr(T* ptr) noexcept : p(ptr) {
        if (p) p->add_ref();
    }
    IntrusivePtr(const IntrusivePtr& o) noexcept : p(o.p) {
        if (p) p->add_ref();
    }
    IntrusivePtr(IntrusivePtr&& o) noexcept : p(o.p) { o.p = nullptr; }
    ~IntrusivePtr() noexcept { if (p) p->release(); }

    T* get() const noexcept { return p; }
    T* operator->() const noexcept { return p; }
    T& operator*() const noexcept { return *p; }

    IntrusivePtr& operator=(const IntrusivePtr& o) noexcept {
        if (o.p) o.p->add_ref();
        if (p) p->release();
        p = o.p;
        return *this;
    }
};

// 场景 C：内存紧张时使用侵入式计数
struct MyDevice : IntrusiveBase {
    int id;
    explicit MyDevice(int i) : id(i) {
        printf("Device %d created\n", id);
    }
    ~MyDevice() {
        printf("Device %d destroyed\n", id);
    }
};

void intrusive_example() {
    printf("\n=== Scenario C: Intrusive Reference Counting ===\n");

    IntrusivePtr<MyDevice> dev1(new MyDevice(1));
    IntrusivePtr<MyDevice> dev2 = dev1;  // 共享，无额外堆分配

    printf("Device %d shared without control block allocation\n", dev1->id);

    // 没有额外的控制块分配，计数器在对象内部
}

int main() {
    gpio_unique_ptr_example();
    config_shared_ptr_example();
    memory_comparison();
    performance_comparison();
    intrusive_example();

    printf("\n=== All Examples Complete ===\n");
    printf("\nKey Takeaways:\n");
    printf("- unique_ptr: Zero overhead, use for exclusive ownership\n");
    printf("- shared_ptr: Has overhead, use only when truly needed\n");
    printf("- Intrusive: Alternative when memory is constrained\n");

    return 0;
}
