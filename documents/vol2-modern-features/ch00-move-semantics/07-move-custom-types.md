---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: 规则五的落地写法、= default 的适用边界、禁拷贝启移动的设计，以及嵌入式资源句柄的移动实战
difficulty: intermediate
order: 7
platform: host
prerequisites:
- 'Chapter 0: 移动构造与移动赋值'
- 'Chapter 0: 移动语义实战：标准库容器与性能实测'
reading_time_minutes: 12
related:
- 完美转发
title: 移动语义实战：自定义类型与资源句柄
tags:
- host
- cpp-modern
- intermediate
- 移动语义
---
# 移动语义实战：自定义类型与资源句柄

上一篇看完标准库容器的收益和实测数字，这篇轮到您自己的类型。三件事：规则五在自己的类上怎么落地，什么类型该用 `= default`、什么类型该禁拷贝只留移动，以及资源句柄在嵌入式场景里怎么安全转手。篇末有一个动手练习，把整章的内容串一遍。

## 自定义类型的移动最佳实践

把您学到的移动语义知识应用到自己的类上，这里有几条经过实战验证的最佳实践。

对于管理了动态资源的类（持有 `new` 出来的内存、`fopen` 打开的文件、或者类似的资源句柄），应该实现完整的规则五：自定义析构函数、拷贝构造、移动构造、拷贝赋值、移动赋值<RefLink :id="1" preview="C++ Core Guidelines C.21 — if you define or delete any copy, move, or destructor function, define or delete them all" />。移动构造和移动赋值中要把源对象的资源指针置空，确保源对象析构时不会释放已转移的资源。只要移动操作保证不抛出异常，就应该标记 `noexcept`（绝大多数情况下移动操作只是指针复制，不会抛出异常）。

这五个成员各自管什么？咱们拿篇末练习要写的 SimpleVector 当例子，画成一张图：

![SimpleVector 的规则五：五个特殊成员的职责与 = default 对照](./07-move-custom-types-rulefive.drawio)

对于只持有基本类型和标准库容器的类，通常可以用 `= default` 让编译器生成移动操作。`std::string`、`std::vector`、`std::map` 这些标准库组件都有高效的移动语义，编译器自动生成的移动构造函数会按照成员声明顺序逐个调用成员的移动构造函数（对类成员）或直接复制（对标量成员）。这符合 C++ 标准的规定（参见 C++17 [class.copy.ctor]）<RefLink :id="2" preview="cppreference The rule of three / five / zero — implicitly-declared and defaulted move operations" />。

```cpp
struct UserProfile
{
    std::string name;
    std::string email;
    std::vector<std::string> permissions;
    int level = 0;

    // 编译器生成的移动操作已经足够好
    // 因为 std::string 和 std::vector 都有 noexcept 移动
    ~UserProfile() = default;
    UserProfile(const UserProfile&) = default;
    UserProfile(UserProfile&&) noexcept = default;
    UserProfile& operator=(const UserProfile&) = default;
    UserProfile& operator=(UserProfile&&) noexcept = default;
};
```

对于封装了独占资源的类（文件句柄、网络连接、锁），应该**禁用拷贝、启用移动**。拷贝没有意义——您不能"复制"一个 TCP 连接或一个互斥锁。但移动是合理的——您可以把连接的控制权从一个对象转移到另一个对象。

```cpp
class NetworkConnection
{
    int socket_fd_;

public:
    explicit NetworkConnection(const char* host, int port);
    ~NetworkConnection() { if (socket_fd_ >= 0) close_socket(socket_fd_); }

    // 禁止拷贝
    NetworkConnection(const NetworkConnection&) = delete;
    NetworkConnection& operator=(const NetworkConnection&) = delete;

    // 允许移动
    NetworkConnection(NetworkConnection&& other) noexcept
        : socket_fd_(other.socket_fd_)
    {
        other.socket_fd_ = -1;  // 标记为已转移
    }

    NetworkConnection& operator=(NetworkConnection&& other) noexcept
    {
        if (this != &other) {
            if (socket_fd_ >= 0) close_socket(socket_fd_);
            socket_fd_ = other.socket_fd_;
            other.socket_fd_ = -1;
        }
        return *this;
    }
};
```

## 嵌入式实战应用——资源句柄的移动

虽然本系列教程以通用 C++ 为主，但移动语义在嵌入式开发中也有非常实际的应用场景。在资源受限的嵌入式系统上，避免不必要的拷贝不仅能提升性能，有时甚至是功能正确性的保证——比如 DMA 缓冲区的所有权必须唯一、外设的访问权限不可共享。

下面是一个简化但真实的 DMA 缓冲区管理类，展示了移动语义如何确保资源所有权的唯一性：

```cpp
#include <cstddef>
#include <cstring>
#include <utility>
#include <iostream>

/// @brief 模拟的 DMA 缓冲区管理
/// 在真实嵌入式项目中，allocate_dma_buffer 和 free_dma_buffer
/// 会对接到实际的内存管理单元或内存池
class DMABuffer
{
    void* buffer_;       // 指向 DMA 缓冲区
    std::size_t size_;   // 缓冲区大小

public:
    explicit DMABuffer(std::size_t size)
        : buffer_(::operator new(size))
        , size_(size)
    {
        std::memset(buffer_, 0, size_);
        std::cout << "  [DMA] 分配 " << size << " 字节\n";
    }

    ~DMABuffer()
    {
        if (buffer_) {
            ::operator delete(buffer_);
            std::cout << "  [DMA] 释放 " << size_ << " 字节\n";
        }
    }

    // 禁止拷贝：DMA 缓冲区不能有两份
    DMABuffer(const DMABuffer&) = delete;
    DMABuffer& operator=(const DMABuffer&) = delete;

    // 允许移动：所有权可以转移
    DMABuffer(DMABuffer&& other) noexcept
        : buffer_(other.buffer_)
        , size_(other.size_)
    {
        other.buffer_ = nullptr;
        other.size_ = 0;
        std::cout << "  [DMA] 所有权转移（移动构造）\n";
    }

    DMABuffer& operator=(DMABuffer&& other) noexcept
    {
        if (this != &other) {
            if (buffer_) {
                ::operator delete(buffer_);
            }
            buffer_ = other.buffer_;
            size_ = other.size_;
            other.buffer_ = nullptr;
            other.size_ = 0;
            std::cout << "  [DMA] 所有权转移（移动赋值）\n";
        }
        return *this;
    }

    void* data() { return buffer_; }
    const void* data() const { return buffer_; }
    std::size_t size() const { return size_; }
};

/// @brief 模拟从 DMA 接收数据
DMABuffer receive_dma(std::size_t expected_size)
{
    DMABuffer buf(expected_size);
    // 在真实系统中，这里会触发 DMA 传输并等待完成
    // buf.data() 指向的内存由 DMA 控制器直接写入
    char msg[] = "DMA data received";
    std::memcpy(buf.data(), msg, sizeof(msg));
    return buf;  // NRVO 或移动语义确保零拷贝返回
}

int main()
{
    std::cout << "=== 嵌入式 DMA 缓冲区管理 ===\n\n";

    // 从 DMA 接收数据——缓冲区所有权从函数转移到 main
    auto rx_buf = receive_dma(1024);
    std::cout << "  接收到: " << static_cast<const char*>(rx_buf.data()) << "\n\n";

    // 把缓冲区转移到处理队列（模拟）
    std::cout << "=== 转移到处理队列 ===\n";
    DMABuffer process_buf = std::move(rx_buf);
    std::cout << "  rx_buf 大小: " << rx_buf.size() << "\n";
    std::cout << "  process_buf 大小: " << process_buf.size() << "\n\n";

    std::cout << "=== 程序结束，资源自动释放 ===\n";
    return 0;
}
```

这份示例就在下面，点「动手试一试」直接跑：

<OnlineCompilerDemo
  title="动手验证：dma_buffer_move.cpp"
  source-path="code/examples/vol2/19_dma_buffer_move.cpp"
  description="在线验证 DMA 缓冲区的所有权转移。输出里只有一次分配、一次释放——缓冲区全程只有一份在流转。"
  run-options="-std=c++17"
  allow-run
/>

注意整个生命周期中只分配了一次 1024 字节的缓冲区——从 `receive_dma` 内部创建，到 `main` 中的 `rx_buf`（通过 NRVO 或移动），再到 `process_buf`（通过移动构造），始终只有一份缓冲区在流转。没有多余的内存分配，没有数据拷贝，更不会出现两个对象同时操作同一个 DMA 缓冲区的情况——因为拷贝被 `= delete` 禁止了。

## 练习——实现一个支持移动的动态数组

理论看得再多不如动手写一遍。这个练习要求您实现一个简化版的动态数组类，支持拷贝语义和移动语义。这个类不需要像 `std::vector` 那么复杂，但需要正确处理资源管理。

要求如下：类名 `SimpleVector`，内部用 `new[]` 分配的 `int` 数组存储数据。支持 `push_back(int)` 添加元素，必要时扩容（可以简单地按 2 倍增长）。实现完整的规则五。移动操作标记 `noexcept`。实现 `size()` 和 `operator[]`。写一段测试代码验证拷贝和移动的行为。

以下是参考实现框架：

```cpp
// simple_vector.cpp -- 练习：支持移动的动态数组
// Standard: C++17

#include <iostream>
#include <algorithm>
#include <utility>

class SimpleVector
{
    int* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    SimpleVector() : data_(nullptr), size_(0), capacity_(0) {}

    explicit SimpleVector(std::size_t cap)
        : data_(new int[cap])
        , size_(0)
        , capacity_(cap)
    {
    }

    // TODO: 实现析构函数
    // TODO: 实现拷贝构造函数（深拷贝）
    // TODO: 实现移动构造函数（指针转移 + 源对象置空）
    // TODO: 实现拷贝赋值运算符
    // TODO: 实现移动赋值运算符

    void push_back(int value)
    {
        if (size_ >= capacity_) {
            std::size_t new_cap = capacity_ == 0 ? 4 : capacity_ * 2;
            int* new_data = new int[new_cap];
            std::copy(data_, data_ + size_, new_data);
            delete[] data_;
            data_ = new_data;
            capacity_ = new_cap;
        }
        data_[size_++] = value;
    }

    std::size_t size() const { return size_; }
    std::size_t capacity() const { return capacity_; }

    int& operator[](std::size_t i) { return data_[i]; }
    const int& operator[](std::size_t i) const { return data_[i]; }
};

int main()
{
    // 测试代码
    SimpleVector a;
    for (int i = 0; i < 10; ++i) {
        a.push_back(i * i);
    }

    std::cout << "a: ";
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::cout << a[i] << " ";
    }
    std::cout << "\n";

    // 测试拷贝构造
    SimpleVector b = a;
    std::cout << "b (拷贝): ";
    for (std::size_t i = 0; i < b.size(); ++i) {
        std::cout << b[i] << " ";
    }
    std::cout << "\n";

    // 测试移动构造
    SimpleVector c = std::move(a);
    std::cout << "c (移动): ";
    for (std::size_t i = 0; i < c.size(); ++i) {
        std::cout << c[i] << " ";
    }
    std::cout << "\n";
    std::cout << "a 移动后: size=" << a.size()
              << ", capacity=" << a.capacity() << "\n";

    return 0;
}
```

如果您卡住了，可以参考 [移动构造与移动赋值](02-move-semantics.md) 里 `Buffer` 类的实现——逻辑几乎完全一样。关键点是：析构函数里 `delete[] data_`，移动构造里转移指针并置空源对象的指针，拷贝构造里分配新内存并复制数据，移动赋值里先 `delete[]` 当前数据再接管新数据。

完整的参考实现：

```cpp
// simple_vector_solution.cpp -- 练习参考答案
// Standard: C++17

#include <iostream>
#include <algorithm>
#include <utility>

class SimpleVector
{
    int* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    SimpleVector() : data_(nullptr), size_(0), capacity_(0) {}

    explicit SimpleVector(std::size_t cap)
        : data_(cap > 0 ? new int[cap] : nullptr)
        , size_(0)
        , capacity_(cap)
    {
    }

    ~SimpleVector()
    {
        delete[] data_;
    }

    // 拷贝构造：深拷贝
    SimpleVector(const SimpleVector& other)
        : data_(other.capacity_ > 0 ? new int[other.capacity_] : nullptr)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        if (data_) {
            std::copy(other.data_, other.data_ + other.size_, data_);
        }
    }

    // 移动构造：指针转移
    SimpleVector(SimpleVector&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    // 拷贝赋值
    SimpleVector& operator=(const SimpleVector& other)
    {
        if (this != &other) {
            delete[] data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            data_ = capacity_ > 0 ? new int[capacity_] : nullptr;
            if (data_) {
                std::copy(other.data_, other.data_ + size_, data_);
            }
        }
        return *this;
    }

    // 移动赋值
    SimpleVector& operator=(SimpleVector&& other) noexcept
    {
        if (this != &other) {
            delete[] data_;
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }

    void push_back(int value)
    {
        if (size_ >= capacity_) {
            std::size_t new_cap = capacity_ == 0 ? 4 : capacity_ * 2;
            int* new_data = new int[new_cap];
            std::copy(data_, data_ + size_, new_data);
            delete[] data_;
            data_ = new_data;
            capacity_ = new_cap;
        }
        data_[size_++] = value;
    }

    std::size_t size() const { return size_; }
    std::size_t capacity() const { return capacity_; }
    const int* data() const { return data_; }

    int& operator[](std::size_t i) { return data_[i]; }
    const int& operator[](std::size_t i) const { return data_[i]; }
};

int main()
{
    SimpleVector a;
    for (int i = 0; i < 10; ++i) {
        a.push_back(i * i);
    }

    std::cout << "a: ";
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::cout << a[i] << " ";
    }
    std::cout << "\n";
    std::cout << "  a.size()=" << a.size() << ", a.capacity()=" << a.capacity() << "\n\n";

    SimpleVector b = a;   // 拷贝构造
    std::cout << "b (拷贝构造): ";
    for (std::size_t i = 0; i < b.size(); ++i) {
        std::cout << b[i] << " ";
    }
    std::cout << "\n\n";

    SimpleVector c = std::move(a);  // 移动构造
    std::cout << "c (移动构造): ";
    for (std::size_t i = 0; i < c.size(); ++i) {
        std::cout << c[i] << " ";
    }
    std::cout << "\n";
    std::cout << "  a 移动后: size=" << a.size()
              << ", capacity=" << a.capacity() << "\n\n";

    // 验证移动后的 a 可以安全使用
    a = SimpleVector(5);  // 移动赋值一个新对象
    a.push_back(999);
    std::cout << "a 重新赋值后: ";
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::cout << a[i] << " ";
    }
    std::cout << "\n";

    return 0;
}
```

参考答案就在下面，点「动手试一试」直接跑（先自己写，卡住了再对照）：

<OnlineCompilerDemo
  title="动手验证：simple_vector_solution.cpp"
  source-path="code/examples/vol2/20_simple_vector_solution.cpp"
  description="在线运行练习参考答案。重点看移动构造之后那两行：a 的 size 和 capacity 都归零，重新赋值又能复活。"
  run-options="-std=c++17"
  allow-run
/>

拷贝构造后 `b` 拥有独立的数据副本，修改 `b` 不影响 `a`。移动构造后 `c` 接管了 `a` 的所有数据，`a` 变成空的状态（size=0, capacity=0）。之后 `a` 可以通过移动赋值重新获得一个有效的对象，证明移动后的对象确实处于"有效但未指定"的状态<RefLink :id="3" preview="cppreference std::move — Notes: moved-from objects are valid but unspecified" />——它可以被安全地赋新值、析构，但您不应该依赖它的当前值。

移动语义这一章到这里就讲完了。从右值引用的绑定规则，到移动构造的实现，再到 RVO/NRVO 和完美转发，最后落到这篇的性能实测——希望您以后看到 `std::move`，不再只是照抄，而是清楚它在做什么、为什么这么做。

顺着资源所有权这条线，下一章咱们聊智能指针：RAII 会把这一章里那些手动的 `delete`、所有权转移，变成编译器自动管的事。

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="Bjarne Stroustrup / Herb Sutter (eds.)"
    title="C++ Core Guidelines — C.21: If You Define or Delete Any Copy, Move, or Destructor Function, Define or Delete Them All"
    publisher="isocpp.org"
    url="https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#rc-five"
  />
  <ReferenceItem
    :id="2"
    author="cppreference.com"
    title="The Rule of Three / Five / Zero"
    chapter="Implicitly-declared and defaulted move operations"
    url="https://en.cppreference.com/w/cpp/language/rule_of_three"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="std::move"
    chapter="Notes: moved-from state"
    url="https://en.cppreference.com/w/cpp/utility/move"
  />
</ReferenceCard>
