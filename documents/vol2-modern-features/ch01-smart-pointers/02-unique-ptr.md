---
title: "unique_ptr 详解：独占所有权的零开销智能指针"
description: "深入 unique_ptr 的实现原理、用法与最佳实践"
chapter: 1
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - unique_ptr
  - 智能指针
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 18
prerequisites:
  - "Chapter 1: RAII 深入理解"
related:
  - "shared_ptr 详解"
  - "自定义删除器"
---

# unique_ptr 详解：独占所有权的零开销智能指针

在上一篇我们聊了 RAII——C++ 资源管理的基石。现在我们来看 RAII 思想在智能指针领域最直接的体现：`std::unique_ptr`。这个类的设计哲学可以用一句话概括：**一个对象，一个主人，零开销**。它不搞什么引用计数、不做原子操作、不分配额外的控制块——你给它一个对象，它替你管好；你离开作用域，它替你删掉。就这么简单。

但简单不代表肤浅。`unique_ptr` 背后涉及的所有权语义、移动语义、自定义删除器、空基类优化（EBO）等话题，每一条都值得深入理解。今天我们就把这些全部拆开来看。

## 独占所有权：为什么不能拷贝

`unique_ptr` 最核心的语义是"独占"——同一时刻，只有一个 `unique_ptr` 拥有对象的所有权。这意味着它不允许拷贝构造和拷贝赋值，只允许移动。这不是某种限制，而是设计上的精确表达：如果允许拷贝，两个 `unique_ptr` 都会认为自己拥有对象，离开作用域时两个都会尝试 delete——双重释放，直接导致未定义行为。

```cpp
#include <memory>
#include <iostream>

struct Widget {
    int value;
    explicit Widget(int v) : value(v) {
        std::cout << "Widget(" << value << ") 构造\n";
    }
    ~Widget() {
        std::cout << "~Widget(" << value << ") 析构\n";
    }
};

void ownership_demo() {
    auto p1 = std::make_unique<Widget>(42);
    // auto p2 = p1;              // 编译错误！unique_ptr 不可拷贝
    auto p2 = std::move(p1);      // OK：所有权从 p1 转移到 p2

    // 此时 p1 == nullptr，p2 拥有对象
    std::cout << "p1: " << p1.get() << "\n";  // 输出: 0 或 nullptr
    std::cout << "p2: " << p2.get() << "\n";  // 输出: 有效地址
    std::cout << "p2->value: " << p2->value << "\n";  // 输出: 42
}   // p2 析构，Widget 自动被 delete
```

运行结果：

```text
Widget(42) 构造
p1: 0
p2: 0x55a3c8f42eb0
p2->value: 42
~Widget(42) 析构
```

这个"不可拷贝、可移动"的设计完美映射了现实中的所有权转移——就像你把一把钥匙交给别人，你自己就不再拥有那把钥匙了。在代码层面，`std::move` 把 `p1` 内部的裸指针转移给了 `p2`，然后把 `p1` 置空。整个过程没有额外的内存分配，也没有引用计数的开销。

## make_unique vs new：为什么 C++14 要加这个函数

C++11 引入了 `std::unique_ptr` 但忘了提供 `std::make_unique`（这被普遍认为是一个疏忽），C++14 才补上。那么 `make_unique` 相比直接 `new` 有什么优势？

首先是**异常安全**。考虑下面这个函数调用：

```cpp
// 假设有这样一个函数签名
void process(std::unique_ptr<Widget> ptr, int computed_value);

// 危险写法（C++11 风格）
process(std::unique_ptr<Widget>(new Widget(42)), compute_something());

// 安全写法（C++14 风格）
process(std::make_unique<Widget>(42), compute_something());
```

在危险写法中，C++ 编译器需要在调用 `process` 之前依次完成：`new Widget(42)`、构造 `unique_ptr`、调用 `compute_something()`。但 C++ 标准并不规定这三步的精确顺序——编译器可能先 `new`，然后调用 `compute_something()`，最后构造 `unique_ptr`。如果 `compute_something()` 抛出异常，那个 `new` 出来的 `Widget` 就泄漏了——因为 `unique_ptr` 还没来得及接管它。

`make_unique` 把分配和构造包装在一个函数调用里，不存在这种"中间态"，因此是异常安全的。

其次是**代码简洁性**。`make_unique` 避免了在代码中出现裸 `new`，减少犯错的可能：

```cpp
// 对比
auto p1 = std::unique_ptr<Widget>(new Widget(42));  // 啰嗦，且容易忘写 unique_ptr
auto p2 = std::make_unique<Widget>(42);              // 简洁，不可能忘记管理
```

⚠️ `make_unique` 有一个限制：它不支持自定义删除器。如果你需要自定义删除器（比如管理 `FILE*` 或 `malloc` 分配的内存），就必须直接构造 `unique_ptr`。这个问题我们会在后面的"自定义删除器"章节详细讨论。

## 移动语义与 unique_ptr 的深层关系

`unique_ptr` 和移动语义的关系非常紧密。在 C++11 之前，C++ 只有拷贝语义——把一个对象"复制"一份。但对于 `unique_ptr` 来说，拷贝意味着"两个指针指向同一个对象"，这违背了独占所有权的语义。移动语义的引入恰好解决了这个问题：移动不是"复制"，而是"转移"——源对象放弃所有权，目标对象接管。

这使得 `unique_ptr` 可以放入标准容器中：

```cpp
#include <memory>
#include <vector>
#include <iostream>

struct Sensor {
    int id;
    explicit Sensor(int i) : id(i) {}
};

int main() {
    std::vector<std::unique_ptr<Sensor>> sensors;

    // push_back 需要移动，因为 unique_ptr 不可拷贝
    sensors.push_back(std::make_unique<Sensor>(1));
    sensors.push_back(std::make_unique<Sensor>(2));
    sensors.push_back(std::make_unique<Sensor>(3));

    // vector 扩容时，内部的 unique_ptr 会通过移动构造转移
    // 这也是为什么 unique_ptr 的移动操作标记为 noexcept
    for (const auto& s : sensors) {
        std::cout << "Sensor id: " << s->id << "\n";
    }

    // 从函数返回 unique_ptr 也是通过移动（或 RVO）
    auto make_sensor = [](int id) -> std::unique_ptr<Sensor> {
        return std::make_unique<Sensor>(id);
    };

    auto s = make_sensor(99);
    std::cout << "Created sensor " << s->id << "\n";
}
```

这里有一个重要的细节：`unique_ptr` 的移动构造函数和移动赋值运算符都标记为 `noexcept`。这对 `std::vector` 的行为有直接影响——当 vector 扩容时，如果元素的移动构造是 `noexcept` 的，vector 会优先使用移动；否则会退化为拷贝（但 `unique_ptr` 不可拷贝，所以必须移动）。因此 `noexcept` 的移动操作是 `unique_ptr` 能够安全存入容器的关键保证。

## unique_ptr<T[]>：数组版本

`unique_ptr` 有一个针对数组的偏特化版本 `unique_ptr<T[]>`，它在析构时会调用 `delete[]` 而不是 `delete`。

```cpp
auto arr = std::make_unique<int[]>(64);  // 分配 64 个 int
arr[0] = 42;
arr[1] = 17;
// 析构时自动 delete[]
```

不过说实话，在 C++ 中需要手动管理动态数组的场景已经非常少了。如果你需要一个固定大小的数组，用 `std::array` 或 `std::vector` 几乎总是更好的选择。`unique_ptr<T[]>` 主要用于对接那些返回动态分配数组的 C API，比如：

```cpp
// 假设某个 C API 返回 malloc 分配的数组
extern "C" int* create_buffer(size_t size);
extern "C" void free_buffer(int* buf);

auto buffer = std::unique_ptr<int[], void(*)(int*)>(
    create_buffer(1024),
    [](int* p) { free_buffer(p); }
);
buffer[0] = 42;
```

⚠️ 笔者强烈建议：不要用 `unique_ptr<T[]>` 来替代 `std::vector`。`vector` 提供了 `size()`、迭代器、边界检查（通过 `at()`）等能力，而 `unique_ptr<T[]>` 除了自动释放之外什么都没有。

## 自定义删除器基础

`unique_ptr` 的第二个模板参数就是删除器的类型。默认是 `std::default_delete<T>`，内部就是简单的 `delete ptr`。但你可以替换为任何可调用对象——函数指针、lambda、函数对象，只要是 `void operator()(T*)` 的签名就行。

最常见的场景是管理 C API 返回的资源：

```cpp
#include <cstdio>
#include <memory>

// 函数指针作为删除器
using FilePtr = std::unique_ptr<FILE, decltype(&std::fclose)>;

FilePtr open_file(const char* path, const char* mode) {
    FILE* f = std::fopen(path, mode);
    return FilePtr(f, &std::fclose);
}

// lambda 作为删除器（无捕获 → 无状态 → 零开销）
auto make_closer = []() {
    auto deleter = [](FILE* f) noexcept { if (f) std::fclose(f); };
    return std::unique_ptr<FILE, decltype(deleter)>(std::fopen("/tmp/log", "w"), deleter);
};
```

函数对象（functor）作为删除器也是常见的选择，尤其是当你想让删除器类型有名字的时候：

```cpp
struct FreeDeleter {
    void operator()(void* p) noexcept {
        std::free(p);
    }
};

// 管理 malloc 分配的内存
auto buf = std::unique_ptr<char, FreeDeleter>(
    static_cast<char*>(std::malloc(256))
);
```

关于自定义删除器的更深入讨论（有状态删除器、EBO 优化、`shared_ptr` 中的删除器等），我们会在"自定义删除器与侵入式引用计数"那篇中专门展开。

## 零开销证明：sizeof 与汇编分析

`unique_ptr` 常被宣传为"零开销抽象"，但这不是营销口号——我们可以用实际代码来验证。首先是 `sizeof` 对比：

```cpp
#include <memory>
#include <iostream>

struct EmptyDeleter {
    void operator()(int* p) noexcept { delete p; }
};

int main() {
    std::cout << "sizeof(int*):                  " << sizeof(int*) << "\n";
    std::cout << "sizeof(unique_ptr<int>):        " << sizeof(std::unique_ptr<int>) << "\n";
    std::cout << "sizeof(unique_ptr<int, EmptyDeleter>): "
              << sizeof(std::unique_ptr<int, EmptyDeleter>) << "\n";

    // 函数指针作为删除器——有额外开销
    std::cout << "sizeof(unique_ptr<int, void(*)(int*)>): "
              << sizeof(std::unique_ptr<int, void(*)(int*)>) << "\n";
}
```

在 64 位平台上的典型输出：

```text
sizeof(int*):                  8
sizeof(unique_ptr<int>):        8
sizeof(unique_ptr<int, EmptyDeleter>): 8
sizeof(unique_ptr<int, void(*)(int*)>): 16
```

默认删除器和无状态函数对象的 `unique_ptr` 和裸指针大小完全一样——8 字节。这就是空基类优化（EBO）的功劳：`unique_ptr` 内部通常继承自删除器类型，当删除器是空类（没有数据成员）时，编译器会把它的大小优化为 0，因此 `unique_ptr` 只需要存储那一个裸指针。

而使用函数指针作为删除器时，`unique_ptr` 需要额外存储一个函数指针，所以大小翻倍——16 字节。这就是"零开销"的前提条件：**删除器必须是无状态的**。

我们再从汇编的角度验证。下面是一个简单的例子：

```cpp
// 用 unique_ptr 管理 int
int use_unique_ptr() {
    auto p = std::make_unique<int>(42);
    return *p;
}

// 等价的裸指针版本
int use_raw_ptr() {
    int* p = new int(42);
    int v = *p;
    delete p;
    return v;
}
```

在开启优化（`-O2`）后，这两个函数生成的汇编代码几乎完全相同——编译器把 `unique_ptr` 的构造和析构直接内联为 `new` 和 `delete`，没有任何额外的指令开销。这就是 C++ 抽象的威力：你在源码层面获得了安全性和可读性，但在机器码层面没有付出任何代价。

## PIMPL 惯用法：隐藏实现细节

PIMPL（Pointer to Implementation）是 C++ 中减少编译依赖的经典手法。`unique_ptr` 对不完整类型的支持使它成为实现 PIMPL 的最佳工具。

头文件 `widget.h`：

```cpp
#pragma once
#include <memory>

class Widget {
public:
    Widget();
    ~Widget();  // 必须声明，在实现文件中定义

    Widget(Widget&&) noexcept;
    Widget& operator=(Widget&&) noexcept;

    // 禁止拷贝（或自行实现深拷贝）
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    void do_something();

private:
    struct Impl;                  // 前向声明，不完整类型
    std::unique_ptr<Impl> impl_;  // unique_ptr 支持不完整类型
};
```

实现文件 `widget.cpp`：

```cpp
#include "widget.h"
#include <iostream>
#include <string>

// 真正的实现在这里定义——头文件的包含者完全看不到这些细节
struct Widget::Impl {
    std::string name;
    int count;

    Impl() : name("default"), count(0) {}

    void do_work() {
        ++count;
        std::cout << name << " working (count=" << count << ")\n";
    }
};

Widget::Widget() : impl_(std::make_unique<Impl>()) {}

Widget::~Widget() = default;  // 在这里 Impl 是完整类型，delete 能正确执行

Widget::Widget(Widget&&) noexcept = default;
Widget& Widget::operator=(Widget&&) noexcept = default;

void Widget::do_something() {
    impl_->do_work();
}
```

PIMPL 的好处是显而易见的：修改 `Impl` 的定义（比如添加成员、修改方法）只需要重新编译 `widget.cpp`，所有包含 `widget.h` 的文件都不需要重新编译。对于大型项目来说，这能显著缩短编译时间。

⚠️ PIMPL 使用 `unique_ptr` 时有几个注意点。首先，`~Widget()` 必须在实现文件中定义——因为析构时需要 `Impl` 是完整类型，而头文件中只有前向声明。其次，移动构造和移动赋值也应该在实现文件中 `= default`，原因相同。如果你在头文件中 `= default` 它们，编译器会尝试在头文件中实例化 `unique_ptr<Impl>` 的析构，而此时 `Impl` 不完整，会导致编译错误。

## 工厂函数返回 unique_ptr

工厂函数返回 `unique_ptr` 是一种非常常见的模式。它不仅安全（调用者不可能忘记释放），而且表达了清晰的所有权语义：工厂创建对象，调用者独占拥有。

```cpp
#include <memory>
#include <string>

class Logger {
public:
    virtual ~Logger() = default;
    virtual void log(const std::string& msg) = 0;
};

class ConsoleLogger : public Logger {
public:
    void log(const std::string& msg) override {
        std::cout << "[LOG] " << msg << "\n";
    }
};

class FileLogger : public Logger {
public:
    explicit FileLogger(const std::string& path) : path_(path) {}
    void log(const std::string& msg) override {
        // 写入文件（省略具体实现）
    }
private:
    std::string path_;
};

// 工厂函数：返回 unique_ptr<Logger>
std::unique_ptr<Logger> create_logger(bool use_file, const std::string& path = "") {
    if (use_file) {
        return std::make_unique<FileLogger>(path);
    }
    return std::make_unique<ConsoleLogger>();
}

// 使用
void application() {
    auto logger = create_logger(true, "/tmp/app.log");
    logger->log("Application started");

    // 也可以通过移动把所有权传递给其他组件
    // set_global_logger(std::move(logger));
}
```

这种模式还有一个妙处：工厂函数返回 `unique_ptr<Logger>`（基类指针），但实际创建的是 `ConsoleLogger` 或 `FileLogger`（派生类对象）。只要 `Logger` 有虚析构函数（我们确实声明了 `virtual ~Logger() = default`），多态析构就是安全的。

值得注意的是，返回 `unique_ptr` 并不会带来任何性能损失。在现代编译器中，返回值优化（RVO）或者移动语义会确保整个过程零拷贝——工厂函数中创建的 `unique_ptr` 直接"搬到"了调用者的变量中。

## release()、reset() 和 get()：三个关键操作

`unique_ptr` 提供了几个手动管理所有权的方法，理解它们的区别非常重要。

`get()` 返回内部裸指针但不转移所有权。这在你需要把指针传给某个只使用但不拥有的函数时很有用：

```cpp
void print_widget(const Widget* w);

auto p = std::make_unique<Widget>(42);
print_widget(p.get());  // 传给只读函数，p 仍然拥有对象
```

`release()` 放弃所有权并返回裸指针——`unique_ptr` 变空了，但对象不会被删除。这相当于"我把对象交给你了，你自己负责释放"：

```cpp
auto p = std::make_unique<Widget>(42);
Widget* raw = p.release();  // p 变为 nullptr，raw 指向对象
// ... 使用 raw ...
delete raw;  // 你必须手动释放
```

⚠️ `release()` 是一个需要谨慎使用的操作。一旦你调用了它，就回到了裸指针的世界——如果你忘记 `delete`，就会内存泄漏。大多数情况下，使用 `std::move()` 转移所有权给另一个 `unique_ptr` 是更好的选择。

`reset()` 替换当前管理的对象。如果不传参数，就简单地释放当前对象并置空：

```cpp
auto p = std::make_unique<Widget>(1);
p.reset(new Widget(2));  // 释放 Widget(1)，接管 Widget(2)
p.reset();               // 释放 Widget(2)，p 变为 nullptr
```

## 嵌入式实战：硬件句柄管理

在嵌入式开发中，`unique_ptr` 配合自定义删除器可以优雅地管理硬件资源。比如，管理一个通过 HAL 分配的 DMA 缓冲区：

```cpp
struct DmaBuffer {
    void* data;
    size_t size;
};

struct DmaDeleter {
    void operator()(DmaBuffer* buf) noexcept {
        if (buf) {
            hal_dma_free(buf->data);  // 释放 DMA 缓冲区
            delete buf;
        }
    }
};

using UniqueDmaBuffer = std::unique_ptr<DmaBuffer, DmaDeleter>;

UniqueDmaBuffer allocate_dma_buffer(size_t size) {
    void* data = hal_dma_alloc(size);
    if (!data) return nullptr;
    return UniqueDmaBuffer(new DmaBuffer{data, size});
}
```

这种写法的好处是，任何 return path——不管是正常返回、错误返回还是异常——都会正确释放 DMA 缓冲区。在复杂的驱动代码中，这种自动管理能显著降低 bug 率。

## 小结

`unique_ptr` 是现代 C++ 中表达独占所有权的首选工具。它的核心设计——不可拷贝、可移动、RAII 管理生命周期——精确地映射了"一个对象只有一个主人"的语义。通过空基类优化（EBO），默认删除器的 `unique_ptr` 在内存和运行时开销上与裸指针完全一致，是真正的零开销抽象。

我们今天覆盖了 `unique_ptr` 的核心用法：`make_unique` 的异常安全性、移动语义与容器兼容性、数组版本、自定义删除器基础、PIMPL 惯用法、工厂函数模式。这些都是日常工程中最高频的使用场景。

下一篇我们将转向 `shared_ptr`——另一种完全不同的所有权模型：共享所有权。准备好了吗？真正的复杂性才刚刚开始。

## 参考资源

- [cppreference: std::unique_ptr](https://en.cppreference.com/w/cpp/memory/unique_ptr)
- [cppreference: std::make_unique](https://en.cppreference.com/w/cpp/memory/unique_ptr/make_unique)
- [C++ Core Guidelines: R.20-24](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rr-smart)
- [Empty Base Optimization and unique_ptr](https://www.cppstories.com/2021/no-unique-address/)
- Herb Sutter, *GotW #89: Smart Pointers*
