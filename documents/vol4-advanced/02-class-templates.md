---
title: "类模板详解"
description: "深入理解C++类模板的声明、实例化与嵌入式应用"
chapter: 12
order: 2
tags:
  - cpp-modern
  - host
  - intermediate
difficulty: intermediate
reading_time_minutes: 40
prerequisites:
  - "Chapter 12.1: 函数模板详解"
cpp_standard: [11, 14, 17, 20]
platform: host
---

# 嵌入式现代C++教程——类模板详解

类模板是C++泛型编程的核心机制，它允许你编写与类型无关的类定义。标准库中的容器（如`std::vector`、`std::array`）、智能指针（如`std::unique_ptr`）、元组（`std::tuple`）都是类模板的典型应用。

对于嵌入式开发者来说，类模板尤其重要——它让你能在编译期确定类型和大小，避免堆分配，实现零开销的抽象。

本章将深入探讨类模板的声明与定义、成员函数模板、虚函数与模板的限制，并最终实现一个实用的固定容量环形缓冲区。

------

## 类模板基础语法

### 基本声明形式

类模板以`template<...>`开头，后跟类定义：

```cpp
template<typename T>
class Stack {
private:
    T data[100];
    std::size_t top_index = 0;
public:
    void push(const T& item) {
        data[top_index++] = item;
    }

    T pop() {
        return data[--top_index];
    }

    bool empty() const {
        return top_index == 0;
    }
};

// 使用
Stack<int> int_stack;
Stack<float> float_stack;

int_stack.push(42);
float_stack.push(3.14f);
```

**关键点**：

- `template<typename T>`声明了一个类型模板参数`T`
- 使用类模板时必须显式指定模板参数：`Stack<int>`
- 每个不同的模板参数组合都会生成独立的类

### 多个类型参数

```cpp
template<typename Key, typename Value, std::size_t Capacity>
class StaticMap {
private:
    struct Entry {
        Key key;
        Value value;
        bool occupied = false;
    };
    Entry entries[Capacity];
public:
    bool insert(const Key& key, const Value& value) {
        for (auto& entry : entries) {
            if (!entry.occupied) {
                entry.key = key;
                entry.value = value;
                entry.occupied = true;
                return true;
            }
        }
        return false; // 已满
    }

    bool find(const Key& key, Value& out_value) const {
        for (const auto& entry : entries) {
            if (entry.occupied && entry.key == key) {
                out_value = entry.value;
                return true;
            }
        }
        return false;
    }
};

// 使用
StaticMap<const char*, int, 16> sensor_readings;
sensor_readings.insert("temperature", 25);
sensor_readings.insert("humidity", 60);
```

这种多参数设计在嵌入式中非常实用，可以在编译期确定容器容量。

### 非类型模板参数

除了类型，模板参数还可以是编译期常量：

```cpp
template<typename T, std::size_t N>
class FixedArray {
private:
    T data_[N];
public:
    constexpr std::size_t size() const { return N; }

    T& operator[](std::size_t index) {
        return data_[index];
    }

    const T& operator[](std::size_t index) const {
        return data_[index];
    }
};

// 使用
FixedArray<int, 8> channel_values;
FixedArray<uint16_t, 100> adc_buffer;
```

**非类型模板参数的限制**：

- 必须是编译期常量
- 类型限制：整型、枚举、指针、引用、`std::nullptr_t`、C++20的浮点型和字面类型
- 对于指针/引用，必须是静态存储期的对象地址

### 模板参数默认值

类模板可以为模板参数指定默认值：

```cpp
template<typename T, std::size_t N = 16>
class RingBuffer {
private:
    T data_[N];
    std::size_t read_pos_ = 0;
    std::size_t write_pos_ = 0;
public:
    // ...
};

// 使用
RingBuffer<uint8_t, 32> buffer1;  // 容量32
RingBuffer<uint8_t> buffer2;       // 容量16（使用默认值）
```

**默认值规则**：

- 一旦为某个参数设置默认值，其后的所有参数都必须有默认值
- 函数模板的模板参数默认值（C++11引入）规则相同

```cpp
template<typename T = int, std::size_t N = 10>  // OK
class Array;

template<std::size_t N = 10, typename T = int>  // ERROR: T没有默认值
class Array;
```

------

## 模板参数的命名惯例

### typename vs class

在模板参数声明中，`typename`和`class`可以互换：

```cpp
template<typename T> class Container1;  // 推荐
template<class T> class Container2;     // 也可以

template<typename T, class U> class Both;  // 混用
```

**推荐使用`typename`**，原因：

- 语义更清晰（是类型，不一定是类）
- 与`typename`在模板内部的使用一致

### 常见命名惯例

| 惯例 | 含义 | 示例 |
|------|------|------|
| `T` | 单个类型参数 | `template<typename T>` |
| `T, U` | 多个类型参数 | `template<typename T, typename U>` |
| `Key`, `Value` | 容器相关 | `template<typename Key, typename Value>` |
| `N`, `Size` | 大小参数 | `template<std::size_t N>` |
| `Fn` | 函数/可调用对象 | `template<typename Fn>` |
| `IntType` | 整数类型 | `template<typename IntType>` |

```cpp
// 良好的命名示例
template<typename Key, typename Value, std::size_t Capacity>
class StaticMap { };

template<typename IntType = int>
class AtomicInt { };

template<typename ElementType, std::size_t Extent>
class Span { };
```

------

## 成员函数模板

类模板的成员函数本身也可以是模板：

### 成员函数模板基础

```cpp
template<typename T>
class Container {
private:
    T data_;

public:
    // 普通构造函数
    explicit Container(const T& data) : data_(data) {}

    // 成员函数模板
    template<typename U>
    U convert_to() const {
        return static_cast<U>(data_);
    }

    // 成员函数模板（多个模板参数）
    template<typename U, typename Fn>
    U transform_with(Fn fn) const {
        return fn(data_);
    }
};

// 使用
Container<int> c(42);
double d = c.convert_to<double>();        // U推导为double
std::string s = c.transform_with<std::string>(
    [](int x) { return std::to_string(x); }
);
```

### 成员函数模板的特化

成员函数模板可以针对特定类型进行特化：

```cpp
template<typename T>
class Printer {
public:
    void print(const T& value) {
        std::cout << value << '\n';
    }

    // 成员函数模板特化（C++17之前需要全特化）
    template<typename U>
    void print_special(const U& value);

    // 针对bool的特化
    template<>
    void print_special<bool>(bool value) {
        std::cout << (value ? "true" : "false") << '\n';
    }
};

// 使用
Printer<int> p;
p.print(42);
p.print_special(true);  // 输出 "true"
```

**注意**：成员函数模板不支持偏特化，只能全特化。

### 构造函数模板

构造函数模板常用于类型转换：

```cpp
template<typename T>
class Box {
private:
    T value_;

public:
    // 普通构造函数
    explicit Box(const T& value) : value_(value) {}

    // 构造函数模板（允许从其他类型构造）
    template<typename U>
    explicit Box(const Box<U>& other) : value_(other.get()) {}

    T get() const { return value_; }
};

// 使用
Box<int> int_box(42);
Box<double> double_box = int_box;  // 使用构造函数模板
```

**重要**：构造函数模板不会阻止编译器生成拷贝构造函数！

```cpp
Box<int> box1(42);
Box<int> box2 = box1;  // 调用拷贝构造函数，而非模板构造函数
```

### 赋值运算符模板

```cpp
template<typename T>
class Optional {
private:
    T* value_ = nullptr;

public:
    // 赋值运算符模板
    template<typename U>
    Optional& operator=(const Optional<U>& other) {
        if (other.value_) {
            if (!value_) value_ = new T;
            *value_ = static_cast<T>(*other.value_);
        } else {
            delete value_;
            value_ = nullptr;
        }
        return *this;
    }

    ~Optional() { delete value_; }
};
```

------

## 虚函数与模板的限制

### 规则1：虚函数不能是模板函数

```cpp
class Base {
public:
    // ❌ 错误：虚函数不能是模板
    template<typename T>
    virtual void process(T value) {}

    // ✅ 正确：普通虚函数
    virtual void process(int value) {}
};
```

**原因**：虚函数通过虚函数表（vtable）实现，每个虚函数在vtable中占据固定位置。模板函数只有在实例化后才存在，编译期无法确定vtable布局。

### 规则2：类模板可以有虚函数

```cpp
template<typename T>
class Sensor {
public:
    virtual ~Sensor() = default;
    virtual T read() = 0;
    virtual void calibrate() = 0;
};

// 使用
class TemperatureSensor : public Sensor<float> {
public:
    float read() override { return 25.0f; }
    void calibrate() override { }
};
```

### 规则3：虚成员函数不能有默认模板参数

```cpp
template<typename T>
class Base {
public:
    // ❌ 错误：虚函数模板参数默认值
    template<typename U = T>
    virtual void func(U arg) = 0;
};
```

### 设计建议：使用类型擦除替代虚函数模板

如果需要"虚函数模板"的效果，可以使用类型擦除：

```cpp
// 方案1：使用std::function（动态类型擦除）
class Processor {
private:
    std::function<void(int)> int_processor_;
    std::function<void(double)> double_processor_;
    std::function<void(const std::string&)> string_processor_;

public:
    template<typename T>
    void set_processor() {
        if constexpr (std::is_same_v<T, int>) {
            int_processor_ = [](int x) { std::cout << "int: " << x << '\n'; };
        } else if constexpr (std::is_same_v<T, double>) {
            double_processor_ = [](double x) { std::cout << "double: " << x << '\n'; };
        }
    }

    template<typename T>
    void process(T value) {
        if constexpr (std::is_same_v<T, int>) {
            if (int_processor_) int_processor_(value);
        } else if constexpr (std::is_same_v<T, double>) {
            if (double_processor_) double_processor_(value);
        }
    }
};

// 方案2：使用variant（C++17静态多态）
#include <variant>

template<typename... Ts>
struct Visitor : Ts... {
    using Ts::operator()...;
};

template<typename... Ts>
Visitor(Ts...) -> Visitor<Ts...>;

class DynamicProcessor {
private:
    using Value = std::variant<int, double, std::string>;
    std::vector<Value> data_;

public:
    template<typename T>
    void add(T value) {
        data_.push_back(value);
    }

    void process_all() {
        Visitor visitor{
            [](int x) { std::cout << "int: " << x << '\n'; },
            [](double x) { std::cout << "double: " << x << '\n'; },
            [](const std::string& s) { std::cout << "string: " << s << '\n'; }
        };
        for (auto& value : data_) {
            std::visit(visitor, value);
        }
    }
};
```

### 虚函数与模板的性能权衡

| 特性 | 虚函数 | 模板 |
|------|--------|------|
| 多态类型 | 运行时 | 编译期 |
| 代码大小 | 小（共享代码） | 大（每个实例生成代码） |
| 调用开销 | 间接调用（vtable查找） | 直接调用（可内联） |
| 类型安全 | 基类接口限制 | 完全类型安全 |
| 二进制兼容 | 好（ABI稳定） | 差（重新编译） |

**嵌入式建议**：

- 性能关键路径：使用模板（编译期多态）
- 运行时配置/插件架构：使用虚函数（运行时多态）
- 两者结合：模板生成具体类型，通过基类接口统一管理

------

## 类模板的实例化

### 隐式实例化

当使用类模板时，编译器自动实例化需要的成员函数：

```cpp
template<typename T>
class Counter {
private:
    T count_ = 0;

public:
    void increment() { ++count_; }
    void decrement() { --count_; }
    T get() const { return count_; }
};

// 只使用了 increment 和 get
Counter<int> c;
c.increment();
auto value = c.get();

// decrement 不会被实例化（未被使用）
// 这意味着即使 decrement 有编译错误，也不会触发
```

**实用技巧**：可以用这个特性实现条件编译：

```cpp
template<typename T>
class Container {
public:
    void process() {
        if constexpr (std::is_integral_v<T>) {
            // 整数特有操作
        } else {
            // 其他类型操作
        }
    }

    // 这个函数只对整数类型有效
    template<typename U = T>
    std::enable_if_t<std::is_integral_v<U>, U> get_bitmask() {
        return static_cast<U>(0xFF);
    }
};
```

### 显式实例化

可以显式告诉编译器实例化特定类型：

```cpp
// header.h
template<typename T>
class Vector {
    // ... 大量代码
};

// header.cpp
// 显式实例化（在cpp文件中）
template class Vector<int>;
template class Vector<float>;
template class Vector<double>;

// 使用时，这些类型的定义已经存在，不需要重新实例化
```

**好处**：

- 减少编译时间
- 隐藏实现细节（模板实现放在cpp文件中）
- 控制代码膨胀

### extern template声明（C++11）

告诉编译器"这个模板别处已经实例化了"：

```cpp
// my_vector.h
template<typename T>
class Vector {
    // ... 完整定义
};

// my_vector.cpp
template class Vector<int>;
template class Vector<float>;

// main.cpp
extern template class Vector<int>;  // 声明：Vector<int>在别处实例化
extern template class Vector<float>;

void use_vector() {
    Vector<int> v;  // 使用已实例化的版本
}
```

**使用场景**：大型项目中减少编译时间和代码膨胀。

------

## 实战：固定容量环形缓冲区

让我们实现一个实用的、固定容量的环形缓冲区——嵌入式中常用的数据结构，用于串口通信、传感器数据采集等场景。

### 设计目标

1. **零堆分配**：所有存储在栈上或静态存储区
2. **编译期确定大小**：使用非类型模板参数
3. **类型安全**：支持任意元素类型
4. **线程安全（可选）**：提供原子操作的配置选项
5. **优雅处理满/空状态**：区分满和空

### 基础实现

```cpp
#include <array>
#include <atomic>
#include <cstddef>

template<typename T, std::size_t N>
class RingBuffer {
private:
    std::array<T, N> data_;
    std::size_t read_pos_ = 0;
    std::size_t write_pos_ = 0;
    bool full_ = false;

public:
    static constexpr std::size_t capacity = N;

    // 默认构造
    RingBuffer() = default;

    // 检查状态
    bool empty() const {
        return !full_ && (read_pos_ == write_pos_);
    }

    bool full() const {
        return full_;
    }

    std::size_t size() const {
        if (full_) return N;
        if (write_pos_ >= read_pos_) return write_pos_ - read_pos_;
        return N + write_pos_ - read_pos_;
    }

    // 写入元素
    bool push(const T& item) {
        if (full_) {
            return false; // 缓冲区满
        }

        data_[write_pos_] = item;
        write_pos_ = (write_pos_ + 1) % N;

        if (write_pos_ == read_pos_) {
            full_ = true;
        }

        return true;
    }

    bool push(T&& item) {
        if (full_) {
            return false;
        }

        data_[write_pos_] = std::move(item);
        write_pos_ = (write_pos_ + 1) % N;

        if (write_pos_ == read_pos_) {
            full_ = true;
        }

        return true;
    }

    // 读取元素
    bool pop(T& item) {
        if (empty()) {
            return false; // 缓冲区空
        }

        item = data_[read_pos_];
        read_pos_ = (read_pos_ + 1) % N;
        full_ = false;

        return true;
    }

    // 查看首元素（不移除）
    bool peek(T& item) const {
        if (empty()) {
            return false;
        }

        item = data_[read_pos_];
        return true;
    }

    // 清空缓冲区
    void clear() {
        read_pos_ = 0;
        write_pos_ = 0;
        full_ = false;
    }
};
```

### 线程安全版本

```cpp
template<typename T, std::size_t N, bool ThreadSafe = false>
class RingBufferTS;

// 非线程安全版本
template<typename T, std::size_t N>
class RingBufferTS<T, N, false> : public RingBuffer<T, N> {
    using Base = RingBuffer<T, N>;
public:
    using Base::Base;
};

// 线程安全版本
template<typename T, std::size_t N>
class RingBufferTS<T, N, true> {
private:
    std::array<T, N> data_;
    std::atomic<std::size_t> read_pos_{0};
    std::atomic<std::size_t> write_pos_{0};
    std::atomic<bool> full_{false};

public:
    static constexpr std::size_t capacity = N;

    bool empty() const {
        return !full_.load(std::memory_order_acquire) &&
               (read_pos_.load(std::memory_order_acquire) ==
                write_pos_.load(std::memory_order_acquire));
    }

    bool full() const {
        return full_.load(std::memory_order_acquire);
    }

    std::size_t size() const {
        const auto rp = read_pos_.load(std::memory_order_acquire);
        const auto wp = write_pos_.load(std::memory_order_acquire);
        const bool f = full_.load(std::memory_order_acquire);

        if (f) return N;
        if (wp >= rp) return wp - rp;
        return N + wp - rp;
    }

    bool push(const T& item) {
        const auto wp = write_pos_.load(std::memory_order_relaxed);
        const auto next_wp = (wp + 1) % N;
        const auto rp = read_pos_.load(std::memory_order_acquire);

        if (next_wp == rp && full_.load(std::memory_order_acquire)) {
            return false; // 满
        }

        data_[wp] = item;
        write_pos_.store(next_wp, std::memory_order_release);

        if (next_wp == rp) {
            full_.store(true, std::memory_order_release);
        }

        return true;
    }

    bool pop(T& item) {
        const auto rp = read_pos_.load(std::memory_order_relaxed);
        const auto wp = write_pos_.load(std::memory_order_acquire);

        if (rp == wp && !full_.load(std::memory_order_acquire)) {
            return false; // 空
        }

        item = data_[rp];
        const auto next_rp = (rp + 1) % N;
        read_pos_.store(next_rp, std::memory_order_release);
        full_.store(false, std::memory_order_release);

        return true;
    }
};
```

### 嵌入式优化版本

针对资源受限的嵌入式环境，我们可以进一步优化：

```cpp
template<typename T, std::size_t N>
class CompactRingBuffer {
private:
    // 使用位域优化：假设容量不超过255
    static_assert(N <= 256, "Capacity too large for compact storage");

    T data_[N];
    uint8_t read_pos_ = 0;
    uint8_t write_pos_ = 0;
    uint8_t count_ = 0;  // 使用计数而非full标志

public:
    static constexpr std::size_t capacity = N;

    // 内联所有函数（嵌入式常用）
    [[gnu::always_inline]] bool empty() const {
        return count_ == 0;
    }

    [[gnu::always_inline]] bool full() const {
        return count_ == N;
    }

    [[gnu::always_inline]] std::size_t size() const {
        return count_;
    }

    // 批量写入（DMA友好）
    std::size_t push_batch(const T* items, std::size_t count) {
        std::size_t written = 0;

        for (std::size_t i = 0; i < count && !full(); ++i) {
            data_[write_pos_] = items[i];
            write_pos_ = static_cast<uint8_t>((write_pos_ + 1) % N);
            ++count_;
            ++written;
        }

        return written;
    }

    // 批量读取（DMA友好）
    std::size_t pop_batch(T* items, std::size_t count) {
        std::size_t read = 0;

        for (std::size_t i = 0; i < count && !empty(); ++i) {
            items[i] = data_[read_pos_];
            read_pos_ = static_cast<uint8_t>((read_pos_ + 1) % N);
            --count_;
            ++read;
        }

        return read;
    }

    // 直接访问底层存储（用于DMA传输）
    T* data() { return data_; }
    const T* data() const { return data_; }

    // 获取连续空间大小（用于DMA）
    std::size_t contiguous_write_space() const {
        if (write_pos_ >= read_pos_) {
            return N - write_pos_;
        }
        return read_pos_ - write_pos_ - 1;
    }

    std::size_t contiguous_read_space() const {
        if (read_pos_ > write_pos_) {
            return N - read_pos_;
        }
        return write_pos_ - read_pos_;
    }
};
```

### 完整实现示例

<details>
<summary>点击展开完整的RingBuffer实现（含迭代器支持）</summary>

```cpp
#include <array>
#include <cstddef>
#include <iterator>
#include <algorithm>

template<typename T, std::size_t N>
class RingBuffer {
private:
    std::array<T, N> data_;
    std::size_t read_pos_ = 0;
    std::size_t write_pos_ = 0;
    bool full_ = false;

public:
    static constexpr std::size_t capacity = N;

    // 类型定义
    using value_type = T;
    using size_type = std::size_t;
    using reference = T&;
    using const_reference = const T&;

    // 构造函数
    RingBuffer() = default;

    // 迭代器支持
    class iterator {
    private:
        RingBuffer* buffer_;
        std::size_t pos_;
        bool ended_;

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        iterator(RingBuffer* buffer, std::size_t pos, bool ended)
            : buffer_(buffer), pos_(pos), ended_(ended) {}

        reference operator*() { return buffer_->data_[pos_]; }
        pointer operator->() { return &buffer_->data_[pos_]; }

        iterator& operator++() {
            if (pos_ == buffer_->write_pos_) {
                ended_ = true;
            } else {
                pos_ = (pos_ + 1) % N;
            }
            return *this;
        }

        iterator operator++(int) {
            iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        friend bool operator==(const iterator& a, const iterator& b) {
            return a.buffer_ == b.buffer_ &&
                   a.pos_ == b.pos_ &&
                   a.ended_ == b.ended_;
        }

        friend bool operator!=(const iterator& a, const iterator& b) {
            return !(a == b);
        }
    };

    class const_iterator {
    private:
        const RingBuffer* buffer_;
        std::size_t pos_;
        bool ended_;

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = const T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        const_iterator(const RingBuffer* buffer, std::size_t pos, bool ended)
            : buffer_(buffer), pos_(pos), ended_(ended) {}

        reference operator*() const { return buffer_->data_[pos_]; }
        pointer operator->() const { return &buffer_->data_[pos_]; }

        const_iterator& operator++() {
            if (pos_ == buffer_->write_pos_) {
                ended_ = true;
            } else {
                pos_ = (pos_ + 1) % N;
            }
            return *this;
        }

        const_iterator operator++(int) {
            const_iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        friend bool operator==(const const_iterator& a, const const_iterator& b) {
            return a.buffer_ == b.buffer_ &&
                   a.pos_ == b.pos_ &&
                   a.ended_ == b.ended_;
        }

        friend bool operator!=(const const_iterator& a, const const_iterator& b) {
            return !(a == b);
        }
    };

    iterator begin() { return empty() ? end() : iterator(this, read_pos_, false); }
    iterator end() { return iterator(this, write_pos_, true); }
    const_iterator begin() const { return cbegin(); }
    const_iterator end() const { return cend(); }
    const_iterator cbegin() const {
        return empty() ? cend() : const_iterator(this, read_pos_, false);
    }
    const_iterator cend() const { return const_iterator(this, write_pos_, true); }

    // 状态查询
    bool empty() const { return !full_ && (read_pos_ == write_pos_); }
    bool full() const { return full_; }
    std::size_t size() const {
        if (full_) return N;
        if (write_pos_ >= read_pos_) return write_pos_ - read_pos_;
        return N + write_pos_ - read_pos_;
    }
    static constexpr std::size_t capacity() { return N; }

    // 元素访问
    bool push(const T& item) {
        if (full_) return false;
        data_[write_pos_] = item;
        advance_write();
        return true;
    }

    bool push(T&& item) {
        if (full_) return false;
        data_[write_pos_] = std::move(item);
        advance_write();
        return true;
    }

    template<typename... Args>
    bool emplace(Args&&... args) {
        if (full_) return false;
        data_[write_pos_] = T(std::forward<Args>(args)...);
        advance_write();
        return true;
    }

    bool pop(T& item) {
        if (empty()) return false;
        item = std::move(data_[read_pos_]);
        advance_read();
        return true;
    }

    bool peek(T& item) const {
        if (empty()) return false;
        item = data_[read_pos_];
        return true;
    }

    // 批量操作
    template<typename InputIt>
    std::size_t push_range(InputIt first, InputIt last) {
        std::size_t count = 0;
        for (auto it = first; it != last && !full(); ++it) {
            data_[write_pos_] = *it;
            advance_write();
            ++count;
        }
        return count;
    }

    // 清空
    void clear() {
        read_pos_ = 0;
        write_pos_ = 0;
        full_ = false;
    }

private:
    void advance_write() {
        write_pos_ = (write_pos_ + 1) % N;
        if (write_pos_ == read_pos_) {
            full_ = true;
        }
    }

    void advance_read() {
        read_pos_ = (read_pos_ + 1) % N;
        full_ = false;
    }
};
```

</details>

### 使用示例

```cpp
// 串口接收缓冲区
RingBuffer<uint8_t, 256> uart_rx_buffer;

// 在串口中断中
void UART_IRQHandler() {
    if (UART->SR & UART_SR_RXNE) {
        uint8_t data = UART->DR;
        uart_rx_buffer.push(data);
    }
}

// 在主循环中处理
void process_uart_data() {
    uint8_t byte;
    while (uart_rx_buffer.pop(byte)) {
        // 处理接收到的数据
        protocol_parse(byte);
    }
}

// 传感器数据缓冲
struct SensorData {
    uint32_t timestamp;
    float value;
};

RingBuffer<SensorData, 16> sensor_buffer;

// 采集数据
void sample_sensor() {
    SensorData data{
        .timestamp = get_system_tick(),
        .value = read_adc()
    };

    if (!sensor_buffer.push(data)) {
        // 缓冲区满，记录错误
        error_count++;
    }
}

// 批量发送
void send_sensor_data() {
    SensorData data;
    while (sensor_buffer.pop(data)) {
        send_to_host(data);
    }
}
```

------

## 类模板特化

### 全特化

为特定模板参数提供完全不同的实现：

```cpp
// 主模板
template<typename T, std::size_t N>
class FixedVector {
private:
    T data_[N];
    std::size_t size_ = 0;
public:
    // 通用实现
};

// 针对bool的全特化（使用位压缩）
template<std::size_t N>
class FixedVector<bool, N> {
private:
    static constexpr std::size_t byte_count = (N + 7) / 8;
    uint8_t data_[byte_count];
    std::size_t size_ = 0;

public:
    void push_back(bool value) {
        if (size_ < N) {
            if (value) {
                data_[size_ / 8] |= (1 << (size_ % 8));
            }
            ++size_;
        }
    }

    bool get(std::size_t index) const {
        return data_[index / 8] & (1 << (index % 8));
    }

    std::size_t size() const { return size_; }
};

// 使用
FixedVector<int, 10> int_vec;    // 使用主模板
FixedVector<bool, 10> bool_vec;  // 使用特化版本（节省内存）
```

### 偏特化（C++允许）

只特化部分模板参数：

```cpp
// 主模板
template<typename T, typename Allocator>
class SmartPointer {
    // ...
};

// 偏特化：针对void类型
template<typename Allocator>
class SmartPointer<void, Allocator> {
    // void特有实现
};

// 主模板：两个类型参数
template<typename Key, typename Value, std::size_t N>
class StaticMap {
    // ...
};

// 偏特化：固定Value类型为int
template<typename Key, std::size_t N>
class StaticMap<Key, int, N> {
    // 针对int值的优化实现
};
```

**重要**：函数模板不支持偏特化，只有类模板支持。

### 使用SFINAE实现条件特化

```cpp
#include <type_traits>

// 主模板
template<typename T, std::size_t N, typename Enable = void>
class OptimizedBuffer {
private:
    T data_[N];
public:
    // 通用实现
    void process() {
        for (std::size_t i = 0; i < N; ++i) {
            data_[i] = T{};
        }
    }
};

// 针对算术类型的特化
template<typename T, std::size_t N>
class OptimizedBuffer<T, N, std::enable_if_t<std::is_arithmetic_v<T>>> {
private:
    T data_[N];
public:
    // 使用memset优化
    void process() {
        std::memset(data_, 0, sizeof(data_));
    }
};

// 使用
OptimizedBuffer<int, 100> int_buffer;    // 使用memset版本
OptimizedBuffer<std::string, 10> str_buffer;  // 使用循环版本
```

### C++17 using声明简化特化

```cpp
template<typename T>
using FastBuffer = RingBuffer<T, 32>;

template<typename T>
using LargeBuffer = RingBuffer<T, 256>;

// 使用
FastBuffer<uint8_t> uart_tx;
LargeBuffer<int> sensor_data;
```

------

## 调试技巧：理解模板实例化错误信息

模板编译错误以"信息爆炸"著称。一个简单的错误可能产生数百行错误信息。让我们学习如何快速定位问题。

### 典型错误信息结构

```text
error: no matching function for call to 'foo'
    foo(42);
        ^~~
note: candidate template ignored: deduced conflicting types for parameter 'T' ('int' vs 'double')
    template<typename T>
    void foo(T a, T b);
         ^
```

关键信息：

1. **错误位置**：`foo(42)`所在的行
2. **失败原因**：`deduced conflicting types`（类型冲突）
3. **候选声明**：被考虑的模板声明

### 常见模板错误模式

#### 模式1：类型推导失败

```cpp
template<typename T>
void process(T& a, T& b);

int x;
double y;

process(x, y);  // 错误：T无法同时推导为int和double
```

**错误信息特征**：

```text
note: template argument deduction/substitution failed:
note: couldn't deduce template parameter 'T'
```

**解决方法**：

```cpp
// 方案1：显式指定类型
process<double>(x, y);

// 方案2：两个模板参数
template<typename T, typename U>
void process(T& a, U& b);
```

#### 模式2：成员函数访问失败

```cpp
template<typename T>
void process(T& value) {
    value.some_method();  // T可能没有some_method
}
```

**错误信息特征**：

```text
error: no member named 'some_method' in 'X'
```

**解决方法**：使用约束或if constexpr

```cpp
template<typename T>
auto process(T& value) -> decltype(value.some_method(), void()) {
    value.some_method();
}

// 或C++17
template<typename T>
void process(T& value) {
    if constexpr (requires { value.some_method(); }) {
        value.some_method();
    }
}
```

#### 模式3：实例化深度过大

```cpp
template<int N>
struct Factorial {
    static constexpr int value = N * Factorial<N-1>::value;
};

Factorial<1000000> x;  // 递归太深
```

**错误信息特征**：

```text
fatal error: template instantiation depth exceeds maximum of 900
```

**解决方法**：使用constexpr函数

```cpp
constexpr int factorial(int n) {
    return n <= 1 ? 1 : n * factorial(n - 1);
}
```

### 调试技巧总结

| 技巧 | 说明 | 示例 |
|------|------|------|
| 使用`static_assert` | 在模板开头添加约束 | `static_assert(std::is_integral_v<T>)` |
| 查看错误位置 | 向上滚动找到首次错误位置 | 找到"instantiated from" |
| 使用`-fverbose-asm` | 查看生成的汇编 | 理解模板展开结果 |
| 使用`-ftime-report` | 分析编译时间 | 找出编译慢的模板 |
| 使用C++20 Concepts | 约束模板参数 | `template<std::integral T>` |

### 使用概念（C++20）简化错误信息

```cpp
// C++17之前：错误信息混乱
template<typename T>
void process(T t) {
    static_assert(std::is_integral_v<T>, "T must be integral");
    t += 1;
}

// C++20：清晰的错误信息
template<std::integral T>
void process(T t) {
    t += 1;
}

// 如果调用 process(3.14)，错误信息是：
// error: no matching function for call to 'process'
// note: candidate template ignored: constraints not satisfied
```

### 实用调试宏

```cpp
// 打印类型信息（用于调试）
template<typename T>
struct print_type;

// 使用时会导致编译错误，从而显示类型
print_type<decltype(some_expression)> x;

// 错误信息会显示：
// error: implicit instantiation of undefined template 'print_type<int &>'
```

```cpp
// 编译期类型检查
template<typename T, typename U>
struct check_same {
    static_assert(std::is_same_v<T, U>, "Types must be the same");
    using type = T;
};

// 使用
check_same<decltype(x), int>{};
```

------

## 嵌入式实战场景

### 场景1：寄存器映射模板

```cpp
template<typename RegisterType, RegisterType Address>
class Register {
public:
    // volatile确保读写不被优化
    inline RegisterType read() const {
        return *reinterpret_cast<volatile RegisterType*>(Address);
    }

    inline void write(RegisterType value) const {
        *reinterpret_cast<volatile RegisterType*>(Address) = value;
    }

    // 位操作
    void set_bits(RegisterType mask) const {
        write(read() | mask);
    }

    void clear_bits(RegisterType mask) const {
        write(read() & ~mask);
    }
};

// 使用
using GPIOA_ODR = Register<uint32_t, 0x40020014>;
using GPIOA_MODER = Register<uint32_t, 0x40020000>;

void led_on() {
    GPIOA_ODR od;
    od.set_bits(1 << 5);  // 设置PA5
}

void led_off() {
    GPIOA_ODR od;
    od.clear_bits(1 << 5);  // 清除PA5
}
```

### 场景2：DMA描述符模板

```cpp
template<typename T, std::size_t Count>
class DMADescriptor {
private:
    struct Descriptor {
        T* source;
        T* destination;
        std::size_t count;
        uint32_t config;
    };

    Descriptor desc_;

public:
    DMADescriptor(T* src, T* dst, uint32_t cfg = 0)
        : desc_{src, dst, Count, cfg} {}

    const Descriptor* get() const { return &desc_; }

    // 链式传输
    template<std::size_t NextCount>
    DMADescriptor<T, NextCount>* chain(DMADescriptor<T, NextCount>& next) {
        desc_.config |= reinterpret_cast<uint32_t>(next.get());
        return &next;
    }
};

// 使用
uint8_t rx_buffer[256];
uint8_t tx_buffer[256];
DMADescriptor<uint8_t, 256> rx_desc(nullptr, rx_buffer);
DMADescriptor<uint8_t, 256> tx_desc(tx_buffer, nullptr);

// 配置DMA
DMA->CH0.SAR = reinterpret_cast<uint32_t>(rx_desc.get());
```

### 场景3：延迟模板（编译期计算）

```cpp
template<typename Clock, std::size_t DelayMicroseconds>
class Delay {
    static constexpr std::size_t cycles_per_us = Clock::frequency / 1000000;
    static constexpr std::size_t total_cycles = DelayMicroseconds * cycles_per_us;

public:
    static inline void wait() {
        // 编译期展开的循环
        for (volatile std::size_t i = 0; i < total_cycles; ++i) {
            __NOP();
        }
    }
};

// 使用
struct SystemClock {
    static constexpr std::size_t frequency = 72000000;  // 72MHz
};

using Delay1ms = Delay<SystemClock, 1000>;
using Delay10us = Delay<SystemClock, 10>;

Delay1ms::wait();   // 编译期确定循环次数
```

### 场景4：类型安全的外设ID

```cpp
template<typename Periph, std::size_t Instance>
class PeripheralID {
private:
    static constexpr std::size_t base_address = get_base_address<Periph>(Instance);
    static constexpr std::size_t irq_number = get_irq_number<Periph>(Instance);

public:
    static constexpr std::size_t base() { return base_address; }
    static constexpr std::size_t irq() { return irq_number; }

    // 启用时钟
    static void enable_clock() {
        auto rcc = Register<uint32_t, RCC_BASE>{};
        rcc.set_bits(1 << Instance);
    }
};

// 使用
using UART1 = PeripheralID<UART, 1>;
using UART2 = PeripheralID<UART, 2>;

void init_uart() {
    UART1::enable_clock();
    auto uart_cr1 = Register<uint32_t, UART1::base() + 0x00>{};
    uart_cr1.write(0x0000);  // 配置UART
}
```

------

## 常见陷阱与解决方案

### 陷阱1：模板定义和声明分离

```cpp
// header.h
template<typename T>
class MyClass {
public:
    void func();  // 声明
};

// source.cpp
template<typename T>
void MyClass::func() {  // ❌ 错误：非内联成员函数
    // 实现
}
```

**问题**：编译器在使用点需要看到完整的模板定义。

**解决方案**：

```cpp
// 方案1：全部放在头文件
template<typename T>
class MyClass {
public:
    void func() {  // ✅ 内联定义
        // 实现
    }
};

// 方案2：显式实例化
// header.h
template<typename T>
class MyClass {
public:
    void func();
};

// source.cpp
#include "header.h"

template<typename T>
void MyClass::func() {
    // 实现
}

// 显式实例化常用类型
template class MyClass<int>;
template class MyClass<float>;
```

### 陷阱2：依赖类型的名称解析

```cpp
template<typename T>
class MyClass {
    typedef int value_type;  // 或 using value_type = int;

    void func() {
        value_type x = 0;  // ❌ 错误：依赖类型需要typename
    }
};
```

**解决方案**：

```cpp
template<typename T>
class MyClass {
    typedef typename T::value_type value_type;

    void func() {
        typename T::value_type x = 0;  // ✅ 使用typename
    }
};
```

### 陷阱3：this指针的依赖

```cpp
template<typename T>
class Base {
public:
    void func() {}
};

template<typename T>
class Derived : public Base<T> {
public:
    void method() {
        func();  // ❌ 可能找不到func
        this->func();  // ✅ 显式使用this
    }
};
```

### 陷阱4：模板友元

```cpp
template<typename T>
class MyClass {
    // 让所有MyClass<T>成为友元
    template<typename U>
    friend class MyClass;

private:
    T data_;
};

// 或者：让特定实例成为友元
template<typename T>
class Container {
    friend void special_func<>(Container<T>&);  // 函数模板友元
};
```

### 陷阱5：虚函数与模板混用

```cpp
template<typename T>
class Base {
public:
    // ❌ 错误：虚函数不能是模板
    template<typename U>
    virtual void func(U arg) {}
};
```

**解决方案**：使用类型擦除或重载

```cpp
// 方案1：类型擦除
class Base {
public:
    virtual ~Base() = default;
    virtual void process_int(int) = 0;
    virtual void process_double(double) = 0;
};

// 方案2：使用std::function
class DynamicProcessor {
    std::function<void(int)> int_fn_;
    std::function<void(double)> double_fn_;
public:
    template<typename T>
    void register_handler() {
        if constexpr (std::is_same_v<T, int>) {
            int_fn_ = [](int x) { /* 处理 */ };
        }
    }
};
```

------

## C++14/17/20 新特性

### C++14：变量模板

```cpp
// 变量模板
template<typename T>
constexpr T pi = T(3.1415926535897932385);

// 使用
float f = pi<float>;
double d = pi<double>;

// 嵌入式应用：寄存器地址
template<typename T, std::size_t N>
constexpr std::size_t register_address = base_address<T> + N * sizeof(T);

// 使用
auto uart_dr = Register<uint32_t, register_address<UART, 0>>{};
```

### C++14：泛型lambda

```cpp
template<typename T>
void process_container(T& container) {
    // 使用auto&&参数的lambda
    auto for_each = [](auto&& fn) {
        return [&](auto&&... args) {
            fn(std::forward<decltype(args)>(args)...);
        };
    };
}
```

### C++17：类模板参数推导（CTAD）

```cpp
// 不再需要指定模板参数
std::pair p(42, "hello");  // 推导为pair<int, const char*>
std::vector v{1, 2, 3};     // 推导为vector<int>
std::array a{1, 2, 3, 4};   // 推导为array<int, 4>

// 自定义类型也可以
template<typename T, std::size_t N>
struct Buffer {
    Buffer(T (&arr)[N]) : /* ... */ {}
};

Buffer buf = {1, 2, 3};  // 推导为Buffer<int, 3>
```

### C++17：if constexpr

简化模板内的条件代码：

```cpp
template<typename T>
auto serialize(T value) {
    if constexpr (std::is_integral_v<T>) {
        return std::to_string(value);
    } else if constexpr (std::is_floating_point_v<T>) {
        return std::to_string(value);
    } else if constexpr (std::is_same_v<T, std::string>) {
        return value;
    } else {
        return std::string("[unknown]");
    }
}
```

### C++17：inline变量

```cpp
template<typename T>
class Counter {
public:
    static inline std::size_t count = 0;  // C++17：头文件中定义
};

// 不需要在cpp文件中定义！
```

### C++17：折叠表达式

简化可变参数模板：

```cpp
// 模板递归写法（C++14）
template<typename... Args>
auto sum(Args... args) {
    return (args + ...);  // C++17折叠表达式
}

// 嵌入式应用：多通道初始化
template<typename... Pins>
void init_pins(Pins... pins) {
    (init_pin(pins), ...);  // 对每个引脚调用init_pin
}

// 使用
init_pins(GPIOA_5, GPIOA_6, GPIOB_0);
```

### C++20：概念约束

```cpp
// 定义概念
template<typename T>
concept Integral = std::is_integral_v<T>;

template<typename T>
concept SignedIntegral = Integral<T> && std::is_signed_v<T>;

// 使用概念
template<Integral T>
T abs(T value) {
    return value < 0 ? -value : value;
}

// 缩写函数模板
void process(Integral auto value) {
    // ...
}

// 嵌入式应用：外设概念
template<typename T>
concept UART = requires(T t) {
    { t.read() } -> std::convertible_to<uint8_t>;
    { t.write(uint8_t{}) } -> std::same_as<void>;
};

template<UART U>
void send_string(U& uart, const char* str) {
    while (*str) uart.write(*str++);
}
```

### C++20：requires表达式

```cpp
template<typename T>
concept Serializable = requires(T t) {
    { t.serialize() } -> std::convertible_to<std::vector<uint8_t>>;
    { t.deserialize(std::declval<std::vector<uint8_t>>()) };
};

// 使用
template<Serializable T>
void send_object(T& obj) {
    auto data = obj.serialize();
    // 发送数据
}
```

------

## 小结

类模板是C++泛型编程的核心工具，在嵌入式开发中尤为重要：

| 特性 | 嵌入式优势 | 应用场景 |
|------|-----------|----------|
| 编译期确定大小 | 零堆分配 | 固定容量容器 |
| 类型参数化 | 代码复用 | 通信协议、数据结构 |
| 非类型参数 | 常量表达式 | 寄存器映射、缓冲区 |
| 编译期多态 | 零开销抽象 | 算法特化、策略模式 |
| 内联优化 | 减少函数调用 | 热路径代码 |

**实践建议**：

1. **优先使用类模板而非宏**：类型安全且可调试
2. **合理使用非类型模板参数**：编译期确定大小，避免动态分配
3. **注意代码膨胀**：通过共享基类、显式实例化控制
4. **善用C++17/20特性**：`if constexpr`、Concepts让模板代码更清晰
5. **虚函数与模板分开**：运行时多态用虚函数，编译期多态用模板

**下一章**，我们将探讨**模板元编程**，学习类型萃取、SFINAE、可变参数模板等高级技巧，实现编译期的算法选择和类型计算。
