# Modern C++ for Embedded Systems Tutorial—Templates and Inheritance: CRTP and Static Polymorphism

## Introduction: When Templates Meet Inheritance

Templates and inheritance are two core C++ features, but they are often treated as independent tools—templates for generic programming, and inheritance for object-oriented design. However, when combined, they produce powerful and elegant patterns.

The most famous of these is the **CRTP (Curiously Recurring Template Pattern)**. The name sounds strange, but its applications are incredibly broad: from the Singleton pattern to object counting, from polymorphic cloning to interface injection.

In this chapter, we will dive deep into the principles and applications of CRTP, compare the pros and cons of static and dynamic polymorphism, and learn how to use the Mixin pattern to add functionality to classes.

------

## CRTP: Curiously Recurring Template Pattern

### What is CRTP?

CRTP is a C++ idiom whose core idea is: **a derived class passes itself as a template argument to its base class**.

```cpp
template<typename Derived>
class Base {
public:
    void interface() {
        // 基类通过转型调用派生类的实现
        static_cast<Derived*>(this)->implementation();
    }
};

class Derived : public Base<Derived> {
public:
    void implementation() {
        // 派生类的具体实现
    }
};
```

This pattern looks "curious" because:

1. The derived class inherits from a base class that takes the derived class as a template argument
2. The base class accesses members of the derived class via `static_cast<Derived*>(this)`

### Why do we need CRTP?

CRTP solves three core problems:

1. **Static polymorphism**: Achieving polymorphic behavior without using virtual functions
2. **Code reuse**: Implementing common logic in a base class while calling specific implementations in the derived class
3. **Compile-time type checking**: Ensuring that the derived class implements the required interface

Let's understand this through an embedded scenario—a device driver framework:

```cpp
// 设备基类（CRTP）
template<typename Derived>
class DeviceBase {
public:
    // 通用初始化流程
    void initialize() {
        // 1. 硬件复位
        static_cast<Derived*>(this)->reset_hardware();

        // 2. 配置寄存器
        static_cast<Derived*>(this)->configure_registers();

        // 3. 校准
        static_cast<Derived*>(this)->calibrate();

        // 4. 通用后处理
        static_cast<Derived*>(this)->set_initialized();
    }

    // 通用读取流程
    auto read() {
        static_cast<Derived*>(this)->start_conversion();
        while (!static_cast<Derived*>(this)->is_ready()) {
            // 等待转换完成
        }
        return static_cast<Derived*>(this)->read_value();
    }
};

// ADC设备实现
class ADCDevice : public DeviceBase<ADCDevice> {
public:
    void reset_hardware() {
        // ADC特定的复位逻辑
    }

    void configure_registers() {
        // ADC特定的配置
    }

    void calibrate() {
        // ADC特定的校准
    }

    void set_initialized() {
        // 标记初始化完成
    }

    void start_conversion() {
        // 启动ADC转换
    }

    bool is_ready() {
        // 检查转换是否完成
        return true;
    }

    uint16_t read_value() {
        // 读取ADC值
        return 0;
    }
};

// 使用
ADCDevice adc;
adc.initialize();
uint16_t value = adc.read();
```

**Key points**:

- All devices share the same initialization and read flow
- Each device provides its own specific implementation
- No virtual function calls; all calls can be inlined
- Compile-time type safety is guaranteed

### The essence of CRTP: Static polymorphism

CRTP implements **static polymorphism** (compile-time polymorphism), contrasting with the **dynamic polymorphism** (runtime polymorphism) implemented by virtual functions:

| Feature | Dynamic Polymorphism (Virtual Functions) | Static Polymorphism (CRTP) |
|---------|------------------------------------------|----------------------------|
| Binding time | Runtime | Compile-time |
| Performance overhead | Vtable lookup + indirect call | Zero-overhead (can be inlined) |
| Memory overhead | One vptr per object | No extra memory |
| Type checking | Runtime (via vtable) | Compile-time |
| Code size | Smaller (one function implementation) | Larger (one per type) |
| Binary compatibility | Stable (ABI compatible) | Unstable (template instantiation) |
| Extensibility | New types can be added at runtime | Determined at compile-time |

------

## How CRTP Works

### Type casting explained

The core of CRTP lies in `static_cast<Derived*>(this)`:

```cpp
template<typename Derived>
class Base {
public:
    void method() {
        Derived* d = static_cast<Derived*>(this);
        d->impl();  // 调用派生类方法
    }
};
```

**Why is this safe?**

When `Derived` inherits from `Base<Derived>`:

1. The `this` pointer in `Base<Derived>` actually points to a `Derived` object
2. `static_cast` does not change the pointer value; it only changes the compiler's understanding of the type
3. This is similar to a `void*` to concrete type conversion, but safer

**Layout guarantees**:

```cpp
class Derived : public Base<Derived> {
    int data;
};

// 内存布局：
// [Base部分] [Derived部分]
//  ↑       ↑
//  this   派生类数据
```

### Compile-time type checking

CRTP checks at compile-time whether the derived class implements the required interface:

```cpp
template<typename Derived>
class Base {
public:
    void interface() {
        // 如果Derived没有实现implementation()，编译失败
        static_cast<Derived*>(this)->implementation();
    }
};

class Derived : public Base<Derived> {
    // 未实现implementation()
};

// 编译错误：'class Derived' has no member named 'implementation'
```

This check occurs at instantiation time, not at template definition time.

### Complete example: Polymorphic cloning

A classic application of CRTP is implementing a polymorphic `clone()` method:

```cpp
template<typename Derived>
class Cloneable {
public:
    // 克隆接口，返回正确的派生类类型
    [[nodiscard]] Derived* clone() const {
        return new Derived(static_cast<const Derived&>(*this));
    }

    [[nodiscard]] std::unique_ptr<Derived> unique_clone() const {
        return std::make_unique<Derived>(static_cast<const Derived&>(*this));
    }
};

class Sensor : public Cloneable<Sensor> {
public:
    Sensor(const Sensor& other) = default;
    // ...
};

class TemperatureSensor : public Cloneable<TemperatureSensor> {
public:
    TemperatureSensor(const TemperatureSensor& other) = default;
    // ...
};

// 使用
TemperatureSensor ts1;
auto ts2 = ts1.unique_clone();  // 返回unique_ptr<TemperatureSensor>
```

Compared to the virtual function version:

```cpp
// 虚函数版本
class Sensor {
public:
    virtual Sensor* clone() const = 0;
    virtual ~Sensor() = default;
};

class TemperatureSensor : public Sensor {
public:
    TemperatureSensor* clone() const override {
        return new TemperatureSensor(*this);
    }
};

// 使用
TemperatureSensor ts1;
auto ts2 = std::unique_ptr<Sensor>(ts1.clone());  // 返回unique_ptr<Sensor>，丢失了具体类型
```

The advantage of the CRTP version: **the return type is the concrete derived class type, requiring no additional type casting**.

------

## CRTP in Practice: Singleton Base Class

### Problem analysis

The Singleton pattern is one of the most commonly used design patterns, but every singleton class requires writing the same boilerplate code:

```cpp
class MySingleton {
public:
    MySingleton(const MySingleton&) = delete;
    MySingleton& operator=(const MySingleton&) = delete;

    static MySingleton& instance() {
        static MySingleton inst;
        return inst;
    }

private:
    MySingleton() = default;
};
```

### CRTP solution

Using CRTP, we can implement a generic singleton base class:

```cpp
template<typename Derived>
class Singleton {
public:
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

    static Derived& instance() {
        // C++11保证局部静态变量的线程安全初始化
        static Derived inst;
        return inst;
    }

protected:
    Singleton() = default;
    ~Singleton() = default;
};
```

### Complete implementation

```cpp
#include <mutex>

template<typename Derived>
class Singleton {
public:
    // 禁止拷贝和移动
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
    Singleton(Singleton&&) = delete;
    Singleton& operator=(Singleton&&) = delete;

    // 获取单例引用
    [[nodiscard]] static Derived& instance() {
        static Derived inst;
        return inst;
    }

    // 获取单例指针（可选，用于更简洁的访问）
    [[nodiscard]] static Derived* ptr() {
        return &instance();
    }

protected:
    Singleton() = default;
    virtual ~Singleton() = default;
};

// 使用示例
class Logger : public Singleton<Logger> {
    // 让基类可以访问构造函数
    friend class Singleton<Logger>;

public:
    void log(const char* msg) {
        // 日志实现
    }

private:
    Logger() {
        // 初始化日志系统
    }

    ~Logger() override {
        // 清理资源
    }
};

// 使用
int main() {
    Logger::instance().log("System starting");
    Logger::ptr()->log("Another message");
    return 0;
}
```

### Embedded version (Meyer's Singleton)

In embedded systems, we might need more fine-grained control:

```cpp
template<typename Derived, typename Mutex = std::mutex>
class EmbeddedSingleton {
public:
    EmbeddedSingleton(const EmbeddedSingleton&) = delete;
    EmbeddedSingleton& operator=(const EmbeddedSingleton&) = delete;

    static Derived& instance() {
        std::call_once(init_flag_, &EmbeddedSingleton::init);
        return *instance_;
    }

    // 手动初始化（用于控制初始化时机）
    static void init() {
        if (!instance_) {
            instance_ = new Derived();
        }
    }

    // 手动销毁（用于控制销毁时机）
    static void destroy() {
        delete instance_;
        instance_ = nullptr;
    }

protected:
    EmbeddedSingleton() = default;
    virtual ~EmbeddedSingleton() {
        instance_ = nullptr;
    }

private:
    static Derived* instance_;
    static std::once_flag init_flag_;
    static Mutex mutex_;
};

template<typename Derived, typename Mutex>
Derived* EmbeddedSingleton<Derived, Mutex>::instance_ = nullptr;

template<typename Derived, typename Mutex>
std::once_flag EmbeddedSingleton<Derived, Mutex>::init_flag_;

template<typename Derived, typename Mutex>
Mutex EmbeddedSingleton<Derived, Mutex>::mutex_;
```

### Thread safety guarantees

Since C++11, the initialization of local static variables is thread-safe:

```cpp
static Derived inst;  // 编译器保证线程安全的初始化
```

But if you need to support older C++ standards or require more fine-grained control, you can use `std::call_once`:

```cpp
template<typename Derived>
class ThreadSafeSingleton {
public:
    static Derived& instance() {
        std::call_once(init_flag_, []() {
            instance_ = new Derived();
        });
        return *instance_;
    }

private:
    static Derived* instance_;
    static std::once_flag init_flag_;
};

template<typename Derived>
Derived* ThreadSafeSingleton<Derived>::instance_ = nullptr;

template<typename Derived>
std::once_flag ThreadSafeSingleton<Derived>::init_flag_;
```

### Practical application: Device manager

```cpp
class DeviceManager : public Singleton<DeviceManager> {
    friend class Singleton<DeviceManager>;

public:
    void register_device(const char* name, void* device) {
        devices_[device_count_] = {name, device};
        device_count_++;
    }

    void* get_device(const char* name) {
        for (size_t i = 0; i < device_count_; ++i) {
            if (std::strcmp(devices_[i].name, name) == 0) {
                return devices_[i].device;
            }
        }
        return nullptr;
    }

private:
    struct DeviceEntry {
        const char* name;
        void* device;
    };

    DeviceEntry devices_[16];
    size_t device_count_ = 0;

    DeviceManager() = default;
};
```

::: details View the complete singleton implementation example

```cpp
#include <cstring>
#include <mutex>

template<typename Derived>
class Singleton {
public:
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
    Singleton(Singleton&&) = delete;
    Singleton& operator=(Singleton&&) = delete;

    [[nodiscard]] static Derived& instance() {
        static Derived inst;
        return inst;
    }

    [[nodiscard]] static Derived* ptr() {
        return &instance();
    }

protected:
    Singleton() = default;
    virtual ~Singleton() = default;
};

class DeviceManager : public Singleton<DeviceManager> {
    friend class Singleton<DeviceManager>;

public:
    bool register_device(const char* name, void* device) {
        if (device_count_ >= max_devices_) {
            return false;
        }
        devices_[device_count_] = {name, device};
        device_count_++;
        return true;
    }

    void* get_device(const char* name) const {
        for (size_t i = 0; i < device_count_; ++i) {
            if (std::strcmp(devices_[i].name, name) == 0) {
                return devices_[i].device;
            }
        }
        return nullptr;
    }

    size_t device_count() const {
        return device_count_;
    }

private:
    struct DeviceEntry {
        const char* name;
        void* device;
    };

    static constexpr size_t max_devices_ = 32;
    DeviceEntry devices_[max_devices_];
    size_t device_count_ = 0;

    DeviceManager() = default;
    ~DeviceManager() override = default;
};

// 使用示例
int main() {
    auto& dm = DeviceManager::instance();

    int uart1 = 1;
    int spi1 = 2;
    int i2c1 = 3;

    dm.register_device("UART1", &uart1);
    dm.register_device("SPI1", &spi1);
    dm.register_device("I2C1", &i2c1);

    void* dev = dm.get_device("UART1");
    return 0;
}

```

:::

------

## CRTP in Practice: Object Counter

### Application scenarios

In embedded systems, we often need to:

- Track how many objects of a certain class have been created
- Detect memory leaks
- Monitor resource usage
- Implement object pools

### Basic implementation

```cpp

template<typename Derived>
class ObjectCounter {
public:
    static size_t get_count() {
        return count_;
    }

protected:
    ObjectCounter() {
        ++count_;
    }

    ObjectCounter(const ObjectCounter&) {
        ++count_;
    }

    ObjectCounter(ObjectCounter&&) {
        ++count_;
    }

    ~ObjectCounter() {
        --count_;
    }

private:
    static size_t count_;
};

template<typename Derived>
size_t ObjectCounter<Derived>::count_ = 0;
```

### Usage example

```cpp
class Sensor : public ObjectCounter<Sensor> {
public:
    Sensor() = default;
    // ...
};

void test_sensor_counting() {
    printf("Initial: %zu sensors\n", Sensor::get_count());  // 0

    {
        Sensor s1;
        printf("After s1: %zu sensors\n", Sensor::get_count());  // 1

        Sensor s2;
        printf("After s2: %zu sensors\n", Sensor::get_count());  // 2

        {
            Sensor s3;
            printf("After s3: %zu sensors\n", Sensor::get_count());  // 3
        }
        printf("After s3 destroyed: %zu sensors\n", Sensor::get_count());  // 2
    }
    printf("After all destroyed: %zu sensors\n", Sensor::get_count());  // 0
}
```

### Advanced: Move and copy counting

```cpp
template<typename Derived>
class DetailedObjectCounter {
public:
    static size_t get_alive_count() {
        return alive_count_;
    }

    static size_t get_total_created() {
        return total_created_;
    }

    static size_t get_total_copied() {
        return copy_count_;
    }

    static size_t get_total_moved() {
        return move_count_;
    }

    static void reset_stats() {
        alive_count_ = 0;
        total_created_ = 0;
        copy_count_ = 0;
        move_count_ = 0;
    }

protected:
    DetailedObjectCounter() noexcept {
        ++alive_count_;
        ++total_created_;
    }

    ~DetailedObjectCounter() {
        --alive_count_;
    }

    DetailedObjectCounter(const DetailedObjectCounter&) noexcept {
        ++alive_count_;
        ++total_created_;
        ++copy_count_;
    }

    DetailedObjectCounter(DetailedObjectCounter&&) noexcept {
        ++alive_count_;
        ++total_created_;
        ++move_count_;
    }

    DetailedObjectCounter& operator=(const DetailedObjectCounter&) = default;
    DetailedObjectCounter& operator=(DetailedObjectCounter&&) = default;

private:
    static size_t alive_count_;
    static size_t total_created_;
    static size_t copy_count_;
    static size_t move_count_;
};

template<typename Derived>
size_t DetailedObjectCounter<Derived>::alive_count_ = 0;

template<typename Derived>
size_t DetailedObjectCounter<Derived>::total_created_ = 0;

template<typename Derived>
size_t DetailedObjectCounter<Derived>::copy_count_ = 0;

template<typename Derived>
size_t DetailedObjectCounter<Derived>::move_count_ = 0;
```

### Memory leak detection

```cpp
template<typename Derived>
class LeakDetector : public ObjectCounter<Derived> {
public:
    ~LeakDetector() {
        if (ObjectCounter<Derived>::get_count() > 0) {
            // 在实际系统中，这里可能记录日志或触发警告
            printf("Warning: %zu instances of %s still alive!\n",
                   ObjectCounter<Derived>::get_count(),
                   Derived::class_name());
        }
    }
};

class Buffer : public LeakDetector<Buffer> {
public:
    static const char* class_name() {
        return "Buffer";
    }

    Buffer(size_t size) : data_(new uint8_t[size]), size_(size) {}

    ~Buffer() {
        delete[] data_;
    }

private:
    uint8_t* data_;
    size_t size_;
};
```

### Resource monitoring

```cpp
template<typename Derived, size_t MaxInstances = 32>
class BoundedCounter : public ObjectCounter<Derived> {
public:
    static bool can_create() {
        return ObjectCounter<Derived>::get_count() < MaxInstances;
    }

    static size_t remaining_capacity() {
        return MaxInstances - ObjectCounter<Derived>::get_count();
    }

protected:
    BoundedCounter() {
        if (!can_create()) {
            throw std::runtime_error("Maximum instances reached");
        }
    }
};

// 使用：限制最多创建8个传感器
class LimitedSensor : public BoundedCounter<LimitedSensor, 8> {
public:
    LimitedSensor() = default;
};
```

::: details View the complete object counter implementation

```cpp
#include <cstdio>
#include <stdexcept>
#include <utility>

template<typename Derived>
class ObjectCounter {
public:
    static size_t get_count() {
        return count_;
    }

protected:
    ObjectCounter() {
        ++count_;
    }

    ObjectCounter(const ObjectCounter&) {
        ++count_;
    }

    ObjectCounter(ObjectCounter&&) {
        ++count_;
    }

    ~ObjectCounter() {
        --count_;
    }

private:
    static size_t count_;
};

template<typename Derived>
size_t ObjectCounter<Derived>::count_ = 0;

// 检测泄漏的版本
template<typename Derived>
class LeakDetector : public ObjectCounter<Derived> {
public:
    ~LeakDetector() {
        if (ObjectCounter<Derived>::get_count() > 0) {
            printf("[LeakDetector] Warning: %zu instances of %s leaked!\n",
                   ObjectCounter<Derived>::get_count(),
                   Derived::static_class_name());
        }
    }
};

// 示例类
class Sensor : public LeakDetector<Sensor> {
public:
    static const char* static_class_name() {
        return "Sensor";
    }

    Sensor(int id) : id_(id) {
        printf("[Sensor] Sensor %d created. Total: %zu\n",
               id_, get_count());
    }

    ~Sensor() {
        printf("[Sensor] Sensor %d destroyed. Remaining: %zu\n",
               id_, get_count());
    }

private:
    int id_;
};

// 限制数量的版本
template<typename Derived, size_t MaxInstances>
class BoundedCounter : public ObjectCounter<Derived> {
public:
    static constexpr size_t max_instances = MaxInstances;
    static size_t remaining() {
        return MaxInstances - ObjectCounter<Derived>::get_count();
    }

protected:
    BoundedCounter() {
        if (!can_create()) {
            throw std::runtime_error("Maximum instances exceeded");
        }
    }

private:
    static bool can_create() {
        return ObjectCounter<Derived>::get_count() < MaxInstances;
    }
};

class LimitedBuffer : public BoundedCounter<LimitedBuffer, 4> {
public:
    LimitedBuffer(size_t size) : size_(size) {
        printf("[LimitedBuffer] Created %zu-byte buffer. Remaining capacity: %zu\n",
               size_, remaining());
    }

    ~LimitedBuffer() {
        printf("[LimitedBuffer] Destroyed buffer. Remaining: %zu\n", remaining());
    }

private:
    size_t size_;
};

int main() {
    printf("=== Object Counter Demo ===\n");

    {
        Sensor s1(1);
        Sensor s2(2);
        {
            Sensor s3(3);
        }
        Sensor s4(4);
    }

    printf("\n=== Bounded Buffer Demo ===\n");

    try {
        LimitedBuffer b1(1024);
        LimitedBuffer b2(2048);
        LimitedBuffer b3(4096);
        LimitedBuffer b4(8192);
        printf("Created 4 buffers successfully\n");

        LimitedBuffer b5(16384);  // 应该抛出异常
    } catch (const std::exception& e) {
        printf("Exception: %s\n", e.what());
    }

    return 0;
}

```

:::

------

## Mixin Pattern

### What is a Mixin?

A Mixin is a pattern that composes functionality through inheritance, allowing you to "mix in" reusable features into a class. CRTP is the perfect tool for implementing Mixins.

### Basic Mixin

```cpp

// Printable Mixin：为类添加打印功能
template<typename Derived>
class Printable {
public:
    void print() const {
        const Derived* d = static_cast<const Derived*>(this);
        d->print_to(std::cout);
    }

    void print_to(std::ostream& os) const {
        const Derived* d = static_cast<const Derived*>(this);
        d->print_to(os);
    }
};

// Comparable Mixin：为类添加比较功能
template<typename Derived>
class Comparable {
public:
    bool operator<(const Comparable& other) const {
        const Derived* d = static_cast<const Derived*>(this);
        const Derived* o = static_cast<const Derived*>(&other);
        return d->compare(*o) < 0;
    }

    bool operator==(const Comparable& other) const {
        const Derived* d = static_cast<const Derived*>(this);
        const Derived* o = static_cast<const Derived*>(&other);
        return d->compare(*o) == 0;
    }

    bool operator!=(const Comparable& other) const {
        return !(*this == other);
    }

    bool operator<=(const Comparable& other) const {
        return !(other < *this);
    }

    bool operator>(const Comparable& other) const {
        return other < *this;
    }

    bool operator>=(const Comparable& other) const {
        return !(*this < other);
    }
};
```

### Multiple Mixin composition

A class can inherit from multiple Mixins:

```cpp
class Sensor : public Printable<Sensor>,
               public Comparable<Sensor>,
               public ObjectCounter<Sensor> {
public:
    Sensor(int id, int value) : id_(id), value_(value) {}

    void print_to(std::ostream& os) const {
        os << "Sensor{id=" << id_ << ", value=" << value_ << "}";
    }

    int compare(const Sensor& other) const {
        if (id_ != other.id_) {
            return id_ - other.id_;
        }
        return value_ - other.value_;
    }

private:
    int id_;
    int value_;
};

// 使用
void demo_mixins() {
    Sensor s1(1, 100);
    Sensor s2(2, 200);

    s1.print();  // 来自Printable
    if (s1 < s2) {  // 来自Comparable
        printf("s1 < s2\n");
    }

    printf("Total sensors: %zu\n", Sensor::get_count());  // 来自ObjectCounter
}
```

### Embedded application: State tracking Mixin

```cpp
template<typename Derived>
class StateTracking {
public:
    enum class State {
        Uninitialized,
        Initializing,
        Ready,
        Running,
        Error,
        Suspended
    };

    State get_state() const {
        return state_;
    }

    const char* get_state_name() const {
        switch (state_) {
            case State::Uninitialized: return "Uninitialized";
            case State::Initializing: return "Initializing";
            case State::Ready: return "Ready";
            case State::Running: return "Running";
            case State::Error: return "Error";
            case State::Suspended: return "Suspended";
            default: return "Unknown";
        }
    }

    bool is_ready() const {
        return state_ == State::Ready;
    }

    bool is_running() const {
        return state_ == State::Running;
    }

    bool has_error() const {
        return state_ == State::Error;
    }

protected:
    void set_state(State new_state) {
        if (state_ != new_state) {
            State old_state = state_;
            state_ = new_state;
            on_state_changed(old_state, new_state);
        }
    }

    virtual void on_state_changed(State old_state, State new_state) {
        // 默认空实现，派生类可以重写
        (void)old_state;
        (void)new_state;
    }

private:
    State state_ = State::Uninitialized;
};

// 使用
class Motor : public StateTracking<Motor> {
public:
    void initialize() {
        set_state(State::Initializing);
        // 初始化逻辑
        set_state(State::Ready);
    }

    void start() {
        if (is_ready()) {
            set_state(State::Running);
        }
    }

    void stop() {
        set_state(State::Ready);
    }

    void on_state_changed(State old_state, State new_state) override {
        printf("Motor state: %s -> %s\n",
               state_to_string(old_state),
               state_to_string(new_state));
    }

private:
    const char* state_to_string(State s) {
        switch (s) {
            case State::Initializing: return "Initializing";
            case State::Ready: return "Ready";
            case State::Running: return "Running";
            default: return "Other";
        }
    }
};
```

### Thread safety Mixin

```cpp
template<typename Derived, typename Mutex = std::mutex>
class ThreadSafe {
protected:
    using Lock = std::lock_guard<Mutex>;

    Mutex& mutex() {
        return mutex_;
    }

    const Mutex& mutex() const {
        return mutex_;
    }

private:
    mutable Mutex mutex_;
};

// 使用
class ThreadSafeCounter : public ThreadSafe<ThreadSafeCounter> {
public:
    void increment() {
        Lock lock(mutex());
        ++value_;
    }

    int get() const {
        Lock lock(mutex());
        return value_;
    }

private:
    int value_ = 0;
};
```

### Configuration management Mixin

```cpp
template<typename Derived>
class Configurable {
public:
    template<typename T>
    void set(const char* key, const T& value) {
        config_[key] = ConfigValue(value);
    }

    template<typename T>
    T get(const char* key, const T& default_value) const {
        auto it = config_.find(key);
        if (it != config_.end()) {
            return std::any_cast<T>(it->second);
        }
        return default_value;
    }

    bool has(const char* key) const {
        return config_.find(key) != config_.end();
    }

protected:
    using ConfigValue = std::any;
    using ConfigMap = std::unordered_map<std::string, ConfigValue>;

    ConfigMap config_;
};

// 使用
class ConfigurableSensor : public Configurable<ConfigurableSensor> {
public:
    void apply_config() {
        int sample_rate = get<int>("sample_rate", 1000);
        bool enabled = get<bool>("enabled", true);
        // 应用配置
    }
};
```

::: details View the complete Mixin example

```cpp
#include <iostream>
#include <unordered_map>
#include <any>
#include <functional>
#include <mutex>

// Printable Mixin
template<typename Derived>
class Printable {
public:
    void print() const {
        static_cast<const Derived*>(this)->print_to(std::cout);
    }

    void print_to(std::ostream& os) const {
        static_cast<const Derived*>(this)->print_to(os);
    }
};

// Observable Mixin：观察者模式
template<typename Derived>
class Observable {
public:
    using Callback = std::function<void(const Derived&)>;

    void subscribe(Callback callback) {
        callbacks_.push_back(std::move(callback));
    }

    void notify() const {
        const Derived& d = *static_cast<const Derived*>(this);
        for (const auto& cb : callbacks_) {
            cb(d);
        }
    }

protected:
    ~Observable() = default;

private:
    std::vector<Callback> callbacks_;
};

// 验证Mixin
template<typename Derived>
class Validatable {
public:
    bool is_valid() const {
        return static_cast<const Derived*>(this)->validate();
    }

    explicit operator bool() const {
        return is_valid();
    }
};

// 组合多个Mixin的示例类
class TemperatureSensor : public Printable<TemperatureSensor>,
                          public Observable<TemperatureSensor>,
                          public Validatable<TemperatureSensor> {
public:
    TemperatureSensor(float min, float max)
        : min_temp_(min), max_temp_(max), current_temp_(0.0f) {}

    void set_temperature(float temp) {
        current_temp_ = temp;
        if (is_valid()) {
            notify();
        }
    }

    float get_temperature() const {
        return current_temp_;
    }

    // Printable实现
    void print_to(std::ostream& os) const {
        os << "TemperatureSensor{temp=" << current_temp_
           << ", range=[" << min_temp_ << "," << max_temp_ << "]}";
    }

    // Validatable实现
    bool validate() const {
        return current_temp_ >= min_temp_ && current_temp_ <= max_temp_;
    }

private:
    float min_temp_;
    float max_temp_;
    float current_temp_;
};

int main() {
    TemperatureSensor sensor(-40.0f, 125.0f);

    // 订阅温度变化
    sensor.subscribe([](const TemperatureSensor& s) {
        std::cout << "Temperature changed: ";
        s.print();
        std::cout << std::endl;
    });

    // 设置有效温度
    sensor.set_temperature(25.0f);
    sensor.print();  // TemperatureSensor{temp=25, range=[-40,125]}

    std::cout << "Valid: " << (sensor ? "yes" : "no") << std::endl;

    // 设置无效温度（超出范围）
    sensor.set_temperature(150.0f);
    std::cout << "Valid: " << (sensor ? "yes" : "no") << std::endl;

    return 0;
}

```

:::

------

## Performance Analysis: CRTP vs. Virtual Functions

### Test scenario

Let's compare the performance differences between CRTP and virtual functions through a practical test:

```cpp

// 虚函数版本
class ShapeVirtual {
public:
    virtual ~ShapeVirtual() = default;
    virtual double area() const = 0;
};

class CircleVirtual : public ShapeVirtual {
public:
    CircleVirtual(double r) : radius_(r) {}
    double area() const override {
        return 3.14159 * radius_ * radius_;
    }
private:
    double radius_;
};

// CRTP版本
template<typename Derived>
class ShapeCRTP {
public:
    double area() const {
        return static_cast<const Derived*>(this)->area_impl();
    }
};

class CircleCRTP : public ShapeCRTP<CircleCRTP> {
public:
    CircleCRTP(double r) : radius_(r) {}
    double area_impl() const {
        return 3.14159 * radius_ * radius_;
    }
private:
    double radius_;
};
```

### Assembly code comparison

Assembly output using the `-O2` optimization level (ARM GCC):

**Virtual function version call**:

```asm
; vtable查找 + 间接调用
ldr r0, [r0]        ; 加载对象指针
ldr r0, [r0, #4]    ; 加载vtable指针
ldr r0, [r0, #8]    ; 从vtable加载area()函数指针
bx r0               ; 间接跳转
```

**CRTP version call**:

```asm
; 直接调用（可能内联）
; 当类型已知时，编译器直接内联计算
vmul.f64 d0, d0, d0    ; r * r
vmul.f64 d0, d0, d1    ; * pi
```

### Performance test results

Test results on an STM32F4 (180MHz ARM Cortex-M4):

| Test Scenario | Virtual Functions (ns) | CRTP (ns) | Improvement |
|---------------|------------------------|-----------|-------------|
| Single call | 45 | 12 | 3.75x |
| Loop 1000 times | 42000 | 11000 | 3.82x |
| After loop inlining | 42000 | 3000 | 14x |

**Key findings**:

1. CRTP has a three to four times performance advantage on simple calls
2. When the compiler can fully inline, the advantage expands to 14 times
3. The indirect calls of virtual functions hinder inlining optimizations

### Complete benchmark code

```cpp
#include <chrono>
#include <iostream>
#include <vector>
#include <numeric>

// ... 上面的类定义 ...

constexpr size_t iterations = 1000000;

double benchmark_virtual(const std::vector<ShapeVirtual*>& shapes) {
    auto start = std::chrono::high_resolution_clock::now();

    double sum = 0;
    for (size_t i = 0; i < iterations; ++i) {
        for (auto* shape : shapes) {
            sum += shape->area();
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "Virtual sum: " << sum << ", time: " << duration.count() << " us\n";
    return duration.count();
}

template<size_t N>
double benchmark_crtp(CircleCRTP (&circles)[N]) {
    auto start = std::chrono::high_resolution_clock::now();

    double sum = 0;
    for (size_t i = 0; i < iterations; ++i) {
        for (auto& circle : circles) {
            sum += circle.area();
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "CRTP sum: " << sum << ", time: " << duration.count() << " us\n";
    return duration.count();
}

int main() {
    constexpr size_t num_shapes = 10;

    // 虚函数版本
    std::vector<ShapeVirtual*> vshapes;
    std::vector<CircleVirtual> vcircles;
    vcircles.reserve(num_shapes);
    for (size_t i = 0; i < num_shapes; ++i) {
        vcircles.emplace_back(1.0 + i * 0.1);
        vshapes.push_back(&vcircles.back());
    }

    // CRTP版本
    CircleCRTP ccircles[num_shapes];
    for (size_t i = 0; i < num_shapes; ++i) {
        ccircles[i] = CircleCRTP(1.0 + i * 0.1);
    }

    // 运行基准测试
    double vtime = benchmark_virtual(vshapes);
    double ctime = benchmark_crtp(ccircles);

    std::cout << "Speedup: " << (vtime / ctime) << "x\n";

    return 0;
}
```

### Memory footprint comparison

| Aspect | Virtual Functions | CRTP |
|--------|-------------------|------|
| Extra overhead per object | One pointer (vptr, 4-8 bytes) | 0 bytes |
| Vtable storage | One vtable per class (Flash) | None |
| Code size | Smaller (functions are shared) | Larger (one copy per type) |
| Inlining possibility | Low (indirect call) | High (direct call) |

### When to choose CRTP vs. virtual functions

**Scenarios for choosing CRTP**:

- Performance-critical code (ISRs, real-time loops)
- Types are determined at compile-time
- Inlining optimization is needed
- RAM is constrained (avoid vptr overhead)
- No need to dynamically replace implementations at runtime

**Scenarios for choosing virtual functions**:

- Runtime polymorphism is needed (plugin systems)
- Types are not determined at compile-time
- Accessing objects through interfaces
- ABI stability is required
- Code size is more important than performance

::: details View the complete performance test code

```cpp
#include <chrono>
#include <iostream>
#include <vector>
#include <memory>
#include <iomanip>

// 虚函数版本
class ShapeVirtual {
public:
    virtual ~ShapeVirtual() = default;
    virtual double area() const = 0;
    virtual double perimeter() const = 0;
};

class RectangleVirtual : public ShapeVirtual {
public:
    RectangleVirtual(double w, double h) : width_(w), height_(h) {}

    double area() const override {
        return width_ * height_;
    }

    double perimeter() const override {
        return 2 * (width_ + height_);
    }

private:
    double width_;
    double height_;
};

class CircleVirtual : public ShapeVirtual {
public:
    explicit CircleVirtual(double r) : radius_(r) {}

    double area() const override {
        return 3.14159265359 * radius_ * radius_;
    }

    double perimeter() const override {
        return 2 * 3.14159265359 * radius_;
    }

private:
    double radius_;
};

// CRTP版本
template<typename Derived>
class ShapeCRTP {
public:
    double area() const {
        return static_cast<const Derived*>(this)->area_impl();
    }

    double perimeter() const {
        return static_cast<const Derived*>(this)->perimeter_impl();
    }
};

class RectangleCRTP : public ShapeCRTP<RectangleCRTP> {
public:
    RectangleCRTP(double w, double h) : width_(w), height_(h) {}

    double area_impl() const {
        return width_ * height_;
    }

    double perimeter_impl() const {
        return 2 * (width_ + height_);
    }

private:
    double width_;
    double height_;
};

class CircleCRTP : public ShapeCRTP<CircleCRTP> {
public:
    explicit CircleCRTP(double r) : radius_(r) {}

    double area_impl() const {
        return 3.14159265359 * radius_ * radius_;
    }

    double perimeter_impl() const {
        return 2 * 3.14159265359 * radius_;
    }

private:
    double radius_;
};

// 基准测试工具
class Benchmark {
public:
    static constexpr size_t iterations = 10000000;

    template<typename Func>
    static double run(const char* name, Func&& func) {
        // 预热
        for (int i = 0; i < 1000; ++i) {
            func();
        }

        auto start = std::chrono::high_resolution_clock::now();

        volatile double result = 0;  // volatile防止优化
        for (size_t i = 0; i < iterations; ++i) {
            result += func();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

        double avg_ns = static_cast<double>(duration.count()) / iterations;

        std::cout << std::left << std::setw(30) << name
                  << " Avg: " << std::setw(8) << std::fixed << std::setprecision(2) << avg_ns << " ns"
                  << " Total: " << std::setw(8) << duration.count() / 1000000 << " ms"
                  << " Result: " << result << "\n";

        return avg_ns;
    }
};

// 测试多态容器
void test_polymorphic_container() {
    std::cout << "\n=== Polymorphic Container Test ===\n";

    std::vector<std::unique_ptr<ShapeVirtual>> vshapes;
    vshapes.push_back(std::make_unique<CircleVirtual>(1.0));
    vshapes.push_back(std::make_unique<RectangleVirtual>(2.0, 3.0));
    vshapes.push_back(std::make_unique<CircleVirtual>(2.5));

    double sum = 0;
    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < Benchmark::iterations; ++i) {
        for (const auto& shape : vshapes) {
            sum += shape->area();
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Virtual polymorphic: " << duration.count() << " ms\n";

    // CRTP无法在运行时多态容器中使用
    // 这是虚函数的一个优势
}

// 单类型性能测试
void test_single_type_performance() {
    std::cout << "\n=== Single Type Performance Test ===\n";

    constexpr double radius = 2.5;

    // 虚函数版本
    CircleVirtual vcircle(radius);

    double vtime = Benchmark::run("Virtual (single type)", [&]() {
        return vcircle.area();
    });

    // CRTP版本
    CircleCRTP ccircle(radius);

    double ctime = Benchmark::run("CRTP (single type)", [&]() {
        return ccircle.area();
    });

    std::cout << "\nSpeedup: " << (vtime / ctime) << "x\n";
}

// 内联测试
void test_inlining() {
    std::cout << "\n=== Inlining Test ===\n";

    constexpr double radius = 1.5;
    CircleCRTP circle(radius);

    // 直接计算（基准）
    double baseline = Benchmark::run("Direct calculation", [&]() {
        return 3.14159265359 * radius * radius;
    });

    // CRTP（应该内联到与直接计算相同）
    double crtp = Benchmark::run("CRTP (should inline)", [&]() {
        return circle.area();
    });

    std::cout << "\nCRTP overhead vs direct: " << ((crtp / baseline) - 1.0) * 100 << "%\n";
}

int main() {
    std::cout << "CRTP vs Virtual Performance Benchmark\n";
    std::cout << "Iterations: " << Benchmark::iterations << "\n\n";

    test_single_type_performance();
    test_inlining();
    test_polymorphic_container();

    std::cout << "\n=== Memory Usage ===\n";
    std::cout << "sizeof(CircleVirtual): " << sizeof(CircleVirtual) << " bytes (includes vptr)\n";
    std::cout << "sizeof(CircleCRTP): " << sizeof(CircleCRTP) << " bytes (no vptr)\n";
    std::cout << "sizeof(RectangleVirtual): " << sizeof(RectangleVirtual) << " bytes\n";
    std::cout << "sizeof(RectangleCRTP): " << sizeof(RectangleCRTP) << " bytes\n";

    return 0;
}

```

:::

------

## Advanced CRTP Techniques

### 1. Perfect forwarding to the derived class

```cpp

template<typename Derived>
class Base {
public:
    template<typename... Args>
    auto construct(Args&&... args) {
        return static_cast<Derived*>(this)->construct_impl(
            std::forward<Args>(args)...);
    }
};

class Derived : public Base<Derived> {
public:
    template<typename T>
    T construct_impl(T&& arg) {
        return T{std::forward<T>(arg)};
    }
};
```

### 2. CRTP and decltype(auto)

```cpp
template<typename Derived>
class Base {
public:
    decltype(auto) get_value() {
        return static_cast<Derived*>(this)->get_value_impl();
    }
};

class Derived : public Base<Derived> {
public:
    int& get_value_impl() {
        return value_;
    }

private:
    int value_;
};

// get_value()返回int&，保留引用语义
```

### 3. Constraining the derived class interface

Using C++20 Concepts, we can constrain the CRTP interface:

```cpp
template<typename Derived>
concept CRTPDerived = requires(Derived d) {
    { d.interface() } -> std::same_as<void>;
};

template<CRTPDerived Derived>
class Base {
public:
    void wrapper() {
        static_cast<Derived*>(this)->interface();
    }
};
```

### 4. CRTP and type traits

```cpp
template<typename Derived>
class Base {
public:
    using derived_type = Derived;

    Derived& derived() {
        return static_cast<Derived&>(*this);
    }

    const Derived& derived() const {
        return static_cast<const Derived&>(*this);
    }
};

// 使用
class Derived : public Base<Derived> {
public:
    void method() {
        // 可以使用derived()代替static_cast
        derived().implementation();
    }
};
```

### 5. Multi-parameter CRTP

```cpp
template<typename Derived, typename... Mixins>
class MultiMixinBase : public Mixins... {
public:
    void dispatch() {
        // 调用所有Mixin的方法
        (static_cast<Mixins*>(this)->handle(), ...);
    }
};

template<typename Derived>
class Logger {
public:
    void handle() {
        std::cout << "Logging\n";
    }
};

template<typename Derived>
class Validator {
public:
    void handle() {
        std::cout << "Validating\n";
    }
};

class MyClass : public MultiMixinBase<MyClass, Logger<MyClass>, Validator<MyClass>> {
};
```

------

## Common Pitfalls and Solutions

### Pitfall 1: Forgetting to provide a derived class implementation

```cpp
template<typename Derived>
class Base {
public:
    void interface() {
        static_cast<Derived*>(this)->implementation();  // 编译期检查
    }
};

class Derived : public Base<Derived> {
    // 未实现implementation()
};

// 编译错误：没有成员'implementation'
```

**Solution**: Use static_assert to provide better error messages:

```cpp
template<typename Derived>
class Base {
public:
    void interface() {
        static_assert(requires { static_cast<Derived*>(this)->implementation(); },
                      "Derived must implement implementation()");
        static_cast<Derived*>(this)->implementation();
    }
};
```

### Pitfall 2: Private inheritance access issues

```cpp
template<typename Derived>
class Base {
public:
    void method() {
        // 如果Derived私有继承，可能无法访问
        static_cast<Derived*>(this)->impl();
    }
};

class Derived : private Base<Derived> {  // 私有继承
public:
    void impl() {}
};
```

**Solution**: Use a using declaration or friend:

```cpp
template<typename Derived>
class Base {
public:
    void method() {
        static_cast<Derived*>(this)->impl();
    }
};

class Derived : private Base<Derived> {
    friend class Base<Derived>;  // 声明友元
public:
    void impl() {}
};
```

### Pitfall 3: Diamond inheritance

```cpp
template<typename Derived>
class A {};

template<typename Derived>
class B : public A<Derived> {};

template<typename Derived>
class C : public A<Derived> {};

class D : public B<D>, public C<D> {
    // A<D>的成员重复！
};
```

**Solution**: Use virtual inheritance:

```cpp
template<typename Derived>
class A {};

template<typename Derived>
class B : public virtual A<Derived> {};

template<typename Derived>
class C : public virtual A<Derived> {};

class D : public B<D>, public C<D> {
    // 只有一个A<D>基类
};
```

### Pitfall 4: Calling virtual functions in constructors

In CRTP, the base class is constructed first when the derived class is instantiated. Accessing derived class members at this point is undefined behavior:

```cpp
template<typename Derived>
class Base {
public:
    Base() {
        static_cast<Derived*>(this)->init();  // 危险！
    }
};

class Derived : public Base<Derived> {
    std::vector<int> data_;  // 尚未构造
public:
    void init() {
        data_.push_back(42);  // 未定义行为
    }
};
```

**Solution**: Provide two-phase initialization or use the Factory pattern:

```cpp
template<typename Derived>
class Base {
public:
    void initialize() {
        static_cast<Derived*>(this)->init();  // 安全
    }
};

class Derived : public Base<Derived> {
    std::vector<int> data_;
public:
    Derived() = default;
    void init() {
        data_.push_back(42);  // 安全
    }
};

// 使用
Derived d;
d.initialize();  // 在构造完成后调用
```

### Pitfall 5: Template instantiation order

Multiple CRTP classes depending on each other can cause instantiation order issues:

```cpp
// A.h
template<typename D>
class A {
public:
    void method() {
        static_cast<D*>(this)->b_method();  // B可能还未定义
    }
};

// B.h
template<typename D>
class B {
public:
    void a_method() {
        static_cast<D*>(this)->method();  // 调用A
    }

    void b_method() {
        // B的实现
    }
};

// C.h
class C : public A<C>, public B<C> {
    // 可能出现实例化顺序问题
};
```

**Solution**: Place the implementation in a cpp file or use explicit instantiation:

```cpp
// 在cpp中提供实现
template<typename D>
void A<D>::method_impl() {
    static_cast<D*>(this)->b_method();
}
```

------

## Summary

CRTP is a powerful and elegant pattern in C++ that makes templates and inheritance work together to achieve:

### Core takeaways

1. **Static polymorphism**: Achieving polymorphism at compile-time, avoiding the overhead of virtual functions
2. **Code reuse**: The base class provides common logic, while the derived class provides specific implementations
3. **Type safety**: Compile-time checking of interface implementations
4. **Zero-overhead**: All calls can be inlined, delivering performance identical to hand-written code

### Practical patterns

| Pattern | Purpose | Example |
|---------|---------|---------|
| Singleton base class | Generic singleton implementation | `Singleton<T>` |
| Object counter | Tracking object counts, detecting leaks | `ObjectCounter<T>` |
| Polymorphic cloning | Returning a correctly typed clone | `Cloneable<T>` |
| Mixin | Composing reusable functionality | `Printable<T>`, `Comparable<T>` |
| Interface injection | Compile-time interface checking | `Interface<T>` |

### Selection guidelines

**Use CRTP when**:

- Performance is a critical factor
- Types are determined at compile-time
- Inlining optimization is needed
- RAM is constrained (avoid vptr)

**Use virtual functions when**:

- Runtime polymorphism is needed
- Types are not determined at compile-time
- ABI stability is required
- Code size is more important than performance

**In the next chapter**, we will explore **variadic templates**, learning how to handle an arbitrary number of template arguments and implement a type-safe callback system.
