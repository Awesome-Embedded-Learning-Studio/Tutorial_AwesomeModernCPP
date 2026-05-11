---
chapter: 12
cpp_standard:
- 11
- 14
- 17
- 20
description: In-depth Understanding of C++ Class Template Declaration, Instantiation,
  and Embedded Applications
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 12.1: 函数模板详解'
reading_time_minutes: 51
tags:
- cpp-modern
- host
- intermediate
title: Detailed Explanation of Class Templates
---
# Modern C++ for Embedded Systems Tutorial — A Deep Dive into Class Templates

Class templates are the core mechanism of C++ generic programming, allowing us to write type-agnostic class definitions. Standard library containers (such as `std::vector`, `std::array`), smart pointers (such as `std::unique_ptr`), and tuples (`std::tuple`) are all typical applications of class templates.

For embedded developers, class templates are especially important—they let us determine types and sizes at compile time, avoid heap allocation, and achieve zero-overhead abstraction.

This chapter will delve into the declaration and definition of class templates, member function templates, the limitations of virtual functions and templates, and ultimately implement a practical fixed-capacity ring buffer.

------

## Class Template Basic Syntax

### Basic Declaration Form

A class template begins with `template<...>`, followed by the class definition:

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

**Key points**:

- `template<typename T>` declares a type template parameter `T`
- When using a class template, we must explicitly specify the template arguments: `Stack<int>`
- Each different combination of template arguments generates an independent class

### Multiple Type Parameters

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

This multi-parameter design is highly practical in embedded systems, allowing us to determine container capacity at compile time.

### Non-Type Template Parameters

In addition to types, template parameters can also be compile-time constants:

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

**Restrictions on non-type template parameters**:

- Must be a compile-time constant
- Type restrictions: integers, enumerations, pointers, references, `std::nullptr_t`, and C++20 floating-point types and literal types
- For pointers/references, the address must be of an object with static storage duration

### Default Template Arguments

Class templates can specify default values for template parameters:

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

**Rules for default values**:

- Once a default value is set for a parameter, all subsequent parameters must also have default values
- Function template default arguments (introduced in C++11) follow the same rules

```cpp
template<typename T = int, std::size_t N = 10>  // OK
class Array;

template<std::size_t N = 10, typename T = int>  // ERROR: T没有默认值
class Array;
```

------

## Template Parameter Naming Conventions

### typename vs class

In template parameter declarations, `typename` and `class` are interchangeable:

```cpp
template<typename T> class Container1;  // 推荐
template<class T> class Container2;     // 也可以

template<typename T, class U> class Both;  // 混用
```

**We recommend using `typename`** because:

- The semantics are clearer (it is a type, not necessarily a class)
- It is consistent with the use of `typename` inside templates

### Common Naming Conventions

| Convention | Meaning | Example |
|------|------|------|
| `T` | Single type parameter | `template<typename T>` |
| `T, U` | Multiple type parameters | `template<typename T, typename U>` |
| `Key`, `Value` | Container-related | `template<typename Key, typename Value>` |
| `N`, `Size` | Size parameters | `template<std::size_t N>` |
| `Fn` | Function/callable object | `template<typename Fn>` |
| `IntType` | Integer type | `template<typename IntType>` |

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

## Member Function Templates

Member functions of a class template can themselves be templates:

### Member Function Template Basics

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

### Specialization of Member Function Templates

Member function templates can be fully specialized for specific types:

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

**Note**: Member function templates do not support partial specialization, only full specialization.

### Constructor Templates

Constructor templates are commonly used for type conversion:

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

**Important**: A constructor template does not prevent the compiler from generating a copy constructor!

```cpp
Box<int> box1(42);
Box<int> box2 = box1;  // 调用拷贝构造函数，而非模板构造函数
```

### Assignment Operator Templates

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

## Limitations of Virtual Functions and Templates

### Rule 1: Virtual functions cannot be function templates

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

**Reason**: Virtual functions are implemented through a virtual function table (vtable), where each virtual function occupies a fixed position. Template functions only exist after instantiation, so the compiler cannot determine the vtable layout at compile time.

### Rule 2: Class templates can have virtual functions

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

### Rule 3: Virtual member functions cannot have default template arguments

```cpp
template<typename T>
class Base {
public:
    // ❌ 错误：虚函数模板参数默认值
    template<typename U = T>
    virtual void func(U arg) = 0;
};
```

### Design suggestion: Use type erasure instead of virtual function templates

If we need the effect of a "virtual function template," we can use type erasure:

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

### Performance Trade-offs Between Virtual Functions and Templates

| Feature | Virtual Functions | Templates |
|------|--------|------|
| Polymorphism type | Runtime | Compile time |
| Code size | Small (shared code) | Large (generates code per instance) |
| Call overhead | Indirect call (vtable lookup) | Direct call (can be inlined) |
| Type safety | Restricted by base class interface | Fully type-safe |
| Binary compatibility | Good (stable ABI) | Poor (requires recompilation) |

**Embedded suggestions**:

- Performance-critical paths: Use templates (compile-time polymorphism)
- Runtime configuration/plugin architectures: Use virtual functions (runtime polymorphism)
- Combining both: Use templates to generate concrete types, and manage them uniformly through a base class interface

------

## Class Template Instantiation

### Implicit Instantiation

When we use a class template, the compiler automatically instantiates the required member functions:

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

**Practical tip**: We can use this feature to implement conditional compilation:

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

### Explicit Instantiation

We can explicitly tell the compiler to instantiate a specific type:

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

**Benefits**:

- Reduces compilation time
- Hides implementation details (template implementations can be placed in cpp files)
- Controls code bloat

### extern template Declaration (C++11)

Tells the compiler "this template has been instantiated elsewhere":

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

**Use case**: Reducing compilation time and code bloat in large projects.

------

## Hands-on: Fixed-Capacity Ring Buffer

Let's implement a practical, fixed-capacity ring buffer—a data structure commonly used in embedded systems for serial communication, sensor data acquisition, and other scenarios.

### Design Goals

1. **Zero heap allocation**: All storage is on the stack or in static storage
2. **Compile-time size determination**: Using non-type template parameters
3. **Type safety**: Supporting arbitrary element types
4. **Thread safety (optional)**: Providing configuration options for atomic operations
5. **Graceful full/empty handling**: Distinguishing between full and empty states

### Basic Implementation

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

### Thread-Safe Version

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

### Embedded-Optimized Version

For resource-constrained embedded environments, we can optimize further:

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

### Complete Implementation Example

::: details Click to expand the complete RingBuffer implementation (with iterator support)

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

:::

### Usage Examples

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

## Class Template Specialization

### Full Specialization

Providing a completely different implementation for specific template arguments:

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

### Partial Specialization (Allowed for Class Templates)

Specializing only a portion of the template parameters:

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

**Important**: Function templates do not support partial specialization; only class templates do.

### Conditional Specialization Using SFINAE

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

### Simplifying Specialization with C++17 using Declarations

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

## Debugging Tips: Understanding Template Instantiation Error Messages

Template compilation errors are notorious for "information explosion." A simple mistake can generate hundreds of lines of error messages. Let's learn how to quickly pinpoint the problem.

### Typical Error Message Structure

```text
error: no matching function for call to 'foo'
    foo(42);
        ^~~
note: candidate template ignored: deduced conflicting types for parameter 'T' ('int' vs 'double')
    template<typename T>
    void foo(T a, T b);
         ^
```

Key information:

1. **Error location**: The line where `foo(42)` is located
2. **Failure reason**: `deduced conflicting types` (type conflict)
3. **Candidate declarations**: The template declarations that were considered

### Common Template Error Patterns

#### Pattern 1: Type Deduction Failure

```cpp
template<typename T>
void process(T& a, T& b);

int x;
double y;

process(x, y);  // 错误：T无法同时推导为int和double
```

**Error message characteristics**:

```text
note: template argument deduction/substitution failed:
note: couldn't deduce template parameter 'T'
```

**Solution**:

```cpp
// 方案1：显式指定类型
process<double>(x, y);

// 方案2：两个模板参数
template<typename T, typename U>
void process(T& a, U& b);
```

#### Pattern 2: Member Function Access Failure

```cpp
template<typename T>
void process(T& value) {
    value.some_method();  // T可能没有some_method
}
```

**Error message characteristics**:

```text
error: no member named 'some_method' in 'X'
```

**Solution**: Use constraints or if constexpr

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

#### Pattern 3: Excessive Instantiation Depth

```cpp
template<int N>
struct Factorial {
    static constexpr int value = N * Factorial<N-1>::value;
};

Factorial<1000000> x;  // 递归太深
```

**Error message characteristics**:

```text
fatal error: template instantiation depth exceeds maximum of 900
```

**Solution**: Use constexpr functions

```cpp
constexpr int factorial(int n) {
    return n <= 1 ? 1 : n * factorial(n - 1);
}
```

### Debugging Tips Summary

| Tip | Description | Example |
|------|------|------|
| Use `static_assert` | Add constraints at the beginning of the template | `static_assert(std::is_integral_v<T>)` |
| Check the error location | Scroll up to find the first error location | Look for "instantiated from" |
| Use `-fverbose-asm` | View the generated assembly | Understand template expansion results |
| Use `-ftime-report` | Analyze compilation time | Identify slow-compiling templates |
| Use C++20 Concepts | Constrain template parameters | `template<std::integral T>` |

### Simplifying Error Messages with Concepts (C++20)

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

### Practical Debugging Macros

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

## Embedded Practical Scenarios

### Scenario 1: Register Mapping Template

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

### Scenario 2: DMA Descriptor Template

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

### Scenario 3: Delay Template (Compile-Time Calculation)

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

### Scenario 4: Type-Safe Peripheral IDs

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

## Common Pitfalls and Solutions

### Pitfall 1: Separating Template Definition and Declaration

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

**Problem**: The compiler needs to see the complete template definition at the point of use.

**Solution**:

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

### Pitfall 2: Name Lookup for Dependent Types

```cpp
template<typename T>
class MyClass {
    typedef int value_type;  // 或 using value_type = int;

    void func() {
        value_type x = 0;  // ❌ 错误：依赖类型需要typename
    }
};
```

**Solution**:

```cpp
template<typename T>
class MyClass {
    typedef typename T::value_type value_type;

    void func() {
        typename T::value_type x = 0;  // ✅ 使用typename
    }
};
```

### Pitfall 3: Dependency on the this Pointer

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

### Pitfall 4: Template Friends

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

### Pitfall 5: Mixing Virtual Functions and Templates

```cpp
template<typename T>
class Base {
public:
    // ❌ 错误：虚函数不能是模板
    template<typename U>
    virtual void func(U arg) {}
};
```

**Solution**: Use type erasure or overloading

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

## New Features in C++14/17/20

### C++14: Variable Templates

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

### C++14: Generic Lambdas

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

### C++17: Class Template Argument Deduction (CTAD)

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

### C++17: if constexpr

Simplifying conditional code inside templates:

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

### C++17: inline Variables

```cpp
template<typename T>
class Counter {
public:
    static inline std::size_t count = 0;  // C++17：头文件中定义
};

// 不需要在cpp文件中定义！
```

### C++17: Fold Expressions

Simplifying variadic templates:

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

### C++20: Concept Constraints

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

### C++20: requires Expressions

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

## Summary

Class templates are the core tool of C++ generic programming, and they are especially important in embedded development:

| Feature | Embedded Advantage | Application Scenario |
|------|-----------|----------|
| Compile-time size determination | Zero heap allocation | Fixed-capacity containers |
| Type parameterization | Code reuse | Communication protocols, data structures |
| Non-type parameters | Constant expressions | Register mapping, buffers |
| Compile-time polymorphism | Zero-overhead abstraction | Algorithm specialization, strategy pattern |
| Inline optimization | Reduced function calls | Hot path code |

**Practical advice**:

1. **Prefer class templates over macros**: They provide type safety and are debuggable
2. **Use non-type template parameters judiciously**: Determine sizes at compile time to avoid dynamic allocation
3. **Watch out for code bloat**: Control it through shared base classes and explicit instantiation
4. **Leverage C++17/20 features**: `if constexpr` and Concepts make template code clearer
5. **Keep virtual functions and templates separate**: Use virtual functions for runtime polymorphism, and templates for compile-time polymorphism

**In the next chapter**, we will explore **template metaprogramming**, learning advanced techniques like type traits, SFINAE (Substitution Failure Is Not An Error), and variadic templates to implement compile-time algorithm selection and type computation.
