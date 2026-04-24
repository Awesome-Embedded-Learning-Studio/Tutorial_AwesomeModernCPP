---
title: "自定义删除器与侵入式引用计数"
description: "封装 C API、管理特殊资源，以及侵入式智能指针的实现"
chapter: 1
order: 5
tags:
  - host
  - cpp-modern
  - intermediate
  - 智能指针
  - intrusive_ptr
  - 引用计数
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 18
prerequisites:
  - "Chapter 1: unique_ptr 详解"
  - "Chapter 1: shared_ptr 详解"
related:
  - "scope_guard 与 defer"
---

# 自定义删除器与侵入式引用计数

到目前为止，我们讨论的智能指针都在管理"new 出来的对象"——析构时调用 `delete`，一切自然而然。但现实世界远比这复杂。你需要管理的资源可能是 `fopen()` 返回的 `FILE*`（要用 `fclose` 关闭），可能是 `malloc()` 分配的内存（要用 `free` 释放），可能是 POSIX 的文件描述符 `int`（要用 `close` 关闭），可能是 SDL 的窗口、OpenGL 的纹理、CUDA 的 stream——每种资源都有自己的释放函数。如果智能指针只能 `delete`，那它就太鸡肋了。

自定义删除器（custom deleter）就是让智能指针适配各种"非标准"资源的关键机制。而侵入式引用计数（intrusive reference counting）则是在性能和内存受限场景下替代 `shared_ptr` 的重要方案。今天我们把这两个话题放在一起讨论，因为它们都围绕着同一个核心问题：**如何让 C++ 的智能指针管理那些"不是 new 出来的"资源**。

## 删除器的三种形态

自定义删除器本质上就是一个"可调用对象"——在智能指针析构时被调用，负责释放资源。它可以是函数指针、lambda 表达式、或者函数对象（functor）。这三种形态各有特点，我们从最简单的开始逐一讲解。

### 函数指针：最直观的方式

函数指针是最容易理解的删除器形式。你传入一个函数的地址，智能指针在析构时调用它。但函数指针有一个缺点：它会增加 `unique_ptr` 的大小，因为 `unique_ptr` 需要额外存储这个函数指针。

```cpp
#include <cstdio>
#include <memory>
#include <iostream>

// 用函数指针管理 FILE*
void close_file(FILE* f) noexcept {
    if (f) {
        std::cout << "fclose called\n";
        std::fclose(f);
    }
}

void file_example() {
    // unique_ptr<FILE, 函数指针类型>
    std::unique_ptr<FILE, void(*)(FILE*)> fp(std::fopen("/tmp/test.txt", "w"), close_file);

    if (fp) {
        std::fprintf(fp.get(), "hello from unique_ptr with custom deleter\n");
    }

    // 离开作用域时自动调用 close_file(fp.get())
}
```

也可以用 `decltype` 简化类型声明，避免手写函数指针类型：

```cpp
using FilePtr = std::unique_ptr<FILE, decltype(&std::fclose)>;
FilePtr make_file(const char* path, const char* mode) {
    return FilePtr(std::fopen(path, mode), &std::fclose);
}
```

`sizeof` 对比——函数指针删除器会让 `unique_ptr` 翻倍：

```cpp
std::cout << sizeof(std::unique_ptr<int>) << "\n";                        // 8
std::cout << sizeof(std::unique_ptr<FILE, void(*)(FILE*)>) << "\n";       // 16
std::cout << sizeof(std::unique_ptr<FILE, decltype(&std::fclose)>) << "\n"; // 16
```

> **注意**：以上数值在 x86_64-linux-gnu 平台（g++ 15.2.1）测试得出。不同平台和编译器的实现可能略有差异。完整验证代码见 `code/volumn_codes/vol2/ch01-smart-pointers/05-custom-deleter-sizeof.cpp`。

### Lambda：灵活且现代

Lambda 是现代 C++ 中最常用的删除器形式。无捕获的 lambda 可以转换为函数指针，因此和函数指针的内存开销相同。但有捕获的 lambda 会成为有状态删除器，增加 `unique_ptr` 的大小。

```cpp
// 无捕获 lambda —— 等价于函数指针
auto file_closer = [](FILE* f) noexcept {
    if (f) std::fclose(f);
};
using LambdaFilePtr = std::unique_ptr<FILE, decltype(file_closer)>;

// sizeof(LambdaFilePtr) == sizeof(FILE*) == 8（EBO 优化）
// 验证：参见 code/volumn_codes/vol2/ch01-smart-pointers/05-custom-deleter-sizeof.cpp

// 有捕获 lambda —— 有状态，会增大 unique_ptr
void captured_lambda_example() {
    int log_fd = 42;  // 假设这是一个日志文件描述符

    auto logging_closer = [log_fd](FILE* f) noexcept {
        if (f) {
            // 可以在删除器中访问捕获的变量
            write_log(log_fd, "closing file");
            std::fclose(f);
        }
    };

    std::unique_ptr<FILE, decltype(logging_closer)> fp(
        std::fopen("/tmp/test.txt", "w"),
        logging_closer
    );
    // sizeof(fp) > sizeof(FILE*)，因为 lambda 捕获了 log_fd
    // 验证：参见 code/volumn_codes/vol2/ch01-smart-pointers/05-custom-deleter-sizeof.cpp
}
```

### 函数对象：最高效的方式

函数对象（functor）是无状态删除器的最佳选择——它既没有函数指针的存储开销，又比 lambda 更容易复用和命名。关键在于空基类优化（EBO）：如果一个类没有任何数据成员（空类），编译器可以把它的大小优化为 0。`unique_ptr` 通常通过继承删除器类型来实现 EBO，所以空删除器不会增加 `unique_ptr` 的大小。

```cpp
struct FreeDeleter {
    void operator()(void* p) noexcept {
        std::free(p);
    }
};

struct FcloseDeleter {
    void operator()(FILE* f) noexcept {
        if (f) std::fclose(f);
    }
};

void functor_example() {
    // 管理 malloc 分配的内存
    auto buf = std::unique_ptr<char, FreeDeleter>(
        static_cast<char*>(std::malloc(256))
    );
    std::strcpy(buf.get(), "hello");
    std::cout << buf.get() << "\n";  // hello
    // 析构时自动 free

    // sizeof 对比：EBO 生效，sizeof(buf) == sizeof(char*)
    std::cout << sizeof(buf) << "\n";  // 8（x86_64 平台）
}
```

## 无状态删除器的零开销：EBO 详解

"零开销"不是一句空话——空基类优化（Empty Base Optimization, EBO）是 C++ 编译器的一项优化技术：当一个空类（没有数据成员、没有虚函数）被用作基类时，编译器可以把它的大小优化为 0 字节，不需要占用额外的内存空间。`unique_ptr` 的典型实现会将删除器作为基类存储（通过继承），这样当删除器为空类时，整个 `unique_ptr` 就只包含一个裸指针。

我们来验证一下（在 x86_64-linux-gnu 平台，g++ 15.2.1）：

```cpp
#include <memory>
#include <iostream>

struct EmptyDeleter {
    void operator()(int* p) noexcept { delete p; }
};

struct StatefulDeleter {
    int extra_data = 0;
    void operator()(int* p) noexcept { delete p; }
};

int main() {
    std::cout << "sizeof(int*):                              "
              << sizeof(int*) << "\n";
    std::cout << "sizeof(unique_ptr<int>):                    "
              << sizeof(std::unique_ptr<int>) << "\n";
    std::cout << "sizeof(unique_ptr<int, EmptyDeleter>):      "
              << sizeof(std::unique_ptr<int, EmptyDeleter>) << "\n";
    std::cout << "sizeof(unique_ptr<int, StatefulDeleter>):   "
              << sizeof(std::unique_ptr<int, StatefulDeleter>) << "\n";
    std::cout << "sizeof(unique_ptr<int, void(*)(int*)>):     "
              << sizeof(std::unique_ptr<int, void(*)(int*)>) << "\n";
}
```

64 位平台上的典型输出（g++ 15.2.1，-O0）：

```text
sizeof(int*):                              8
sizeof(unique_ptr<int>):                    8
sizeof(unique_ptr<int, EmptyDeleter>):      8
sizeof(unique_ptr<int, StatefulDeleter>):   16
sizeof(unique_ptr<int, void(*)(int*)>):     16
```

完整验证代码见 `code/volumn_codes/vol2/ch01-smart-pointers/05-custom-deleter-sizeof.cpp`。

数据很清楚：空删除器（包括默认删除器和空的函数对象）不会增加 `unique_ptr` 的大小。只有有状态的删除器（比如捕获了变量的 lambda、包含数据成员的函数对象、函数指针）才会增加大小。

这也是为什么笔者推荐在性能敏感的场景下使用函数对象而不是函数指针——函数对象可以通过 EBO 实现零开销，而函数指针永远需要额外的存储空间。

## FILE* 管理、C API 封装实战

掌握了删除器的基本原理之后，我们来看几个实际的封装场景。第一个是最常见的 C API 封装：用 `unique_ptr` 管理 `FILE*`。

```cpp
#include <cstdio>
#include <memory>
#include <string>
#include <iostream>

struct FcloseDeleter {
    void operator()(FILE* f) noexcept {
        if (f) {
            std::fclose(f);
            std::cout << "文件已关闭\n";
        }
    }
};

using UniqueFile = std::unique_ptr<FILE, FcloseDeleter>;

UniqueFile open_for_write(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        throw std::runtime_error("无法打开文件: " + path);
    }
    return UniqueFile(f);
}

void write_config(const std::string& path) {
    auto file = open_for_write(path);
    std::fprintf(file.get(), "key=value\n");
    std::fprintf(file.get(), "port=8080\n");
    // 不需要手动 fclose——RAII 自动处理
}
```

第二个场景是封装 `malloc/free`：

```cpp
struct FreeDeleter {
    void operator()(void* p) noexcept {
        std::free(p);
    }
};

// 为 malloc 返回的内存创建类型安全的智能指针
template <typename T>
using MallocPtr = std::unique_ptr<T, FreeDeleter>;

template <typename T>
MallocPtr<T> malloc_array(size_t count) {
    void* mem = std::malloc(count * sizeof(T));
    if (!mem) throw std::bad_alloc();
    return MallocPtr<T>(static_cast<T*>(mem));
}
```

### SDL/OpenGL 资源管理示例

图形编程中充满了各种需要特定释放函数的资源。用自定义删除器的 `unique_ptr` 可以优雅地管理它们：

```cpp
// SDL 窗口管理
struct SdlWindowDeleter {
    void operator()(SDL_Window* w) noexcept {
        if (w) SDL_DestroyWindow(w);
    }
};

using UniqueSdlWindow = std::unique_ptr<SDL_Window, SdlWindowDeleter>;

UniqueSdlWindow create_window(const char* title, int w, int h) {
    SDL_Window* win = SDL_CreateWindow(
        title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        w, h, SDL_WINDOW_SHOWN
    );
    return UniqueSdlWindow(win);
}

// OpenGL 纹理管理
struct GlTextureDeleter {
    void operator()(GLuint* tex) noexcept {
        if (tex) {
            glDeleteTextures(1, tex);
            delete tex;
        }
    }
};

using UniqueGlTexture = std::unique_ptr<GLuint, GlTextureDeleter>;

UniqueGlTexture create_texture(int width, int height) {
    auto tex = std::make_unique<GLuint>();
    glGenTextures(1, tex.get());
    // ... 设置纹理参数 ...
    return UniqueGlTexture(tex.release(), GlTextureDeleter{});
}
```

这里有一个细节值得注意：OpenGL 的纹理 ID 是一个 `GLuint`（整数），不是指针。但 `unique_ptr` 只能管理指针类型。所以我们把 `GLuint` 放在堆上（`new GLuint`），然后用 `unique_ptr` 管理这个堆上的 `GLuint`。删除器在析构时既调用 `glDeleteTextures` 又调用 `delete`。这种"间接"虽然看起来不太完美，但在实践中是标准做法。

## shared_ptr 的删除器：类型擦除

前面讨论的都是 `unique_ptr` 的删除器——删除器类型是 `unique_ptr` 类型的一部分。而 `shared_ptr` 的删除器有一个本质的不同：**删除器类型不是 `shared_ptr` 类型的一部分**，它被"擦除"后存储在控制块里。

这意味着你可以用同一个 `shared_ptr<T>` 类型持有不同删除器的对象：

```cpp
#include <memory>
#include <iostream>
#include <cstdio>
#include <cstdlib>

std::shared_ptr<void> make_resource(const std::string& type) {
    if (type == "file") {
        return std::shared_ptr<void>(
            std::fopen("/tmp/test.txt", "w"),
            [](void* p) noexcept { if (p) std::fclose(static_cast<FILE*>(p)); }
        );
    } else if (type == "malloc") {
        return std::shared_ptr<void>(
            std::malloc(1024),
            [](void* p) noexcept { std::free(p); }
        );
    }
    return nullptr;
}

void resource_demo() {
    auto f = make_resource("file");
    auto m = make_resource("malloc");

    // f 和 m 的类型完全相同：shared_ptr<void>
    // 但内部有不同的删除器（fclose vs free）
    // 析构时会调用正确的删除函数
}
```

这种"运行时多态"的灵活性是 `shared_ptr` 删除器的优势，但也有代价：删除器存储在控制块里（额外的堆分配），每次析构需要通过函数指针调用删除器。根据基准测试（g++ 15.2.1，-O2，100000 次迭代），`shared_ptr` 的创建和销毁比 `unique_ptr` 慢约 30-50%，主要开销来自控制块的内存分配。完整测试代码见 `code/volumn_codes/vol2/ch01-smart-pointers/05-custom-deleter-benchmark.cpp`。

## 侵入式引用计数原理

自定义删除器解决了"非标准释放"的问题，但 `shared_ptr` 本身的开销（控制块、原子操作、额外堆分配）在性能敏感或内存受限的场景下仍然不可忽视。侵入式引用计数（intrusive reference counting）提供了一种替代方案：**把引用计数嵌入到对象内部，而不是在外部分配控制块**。

侵入式方案的核心思想很简单：对象自己知道"有多少人持有我"。引用计数作为对象的一个成员变量存在，而不是分配在独立的控制块中。这意味着不需要额外的堆分配（省去了控制块的内存和管理开销），引用计数的访问也是局部的（和对象的其他成员在同一个缓存行里）。

```cpp
class RefCounted {
public:
    void add_ref() noexcept { ++ref_count_; }
    void release() noexcept {
        if (--ref_count_ == 0) {
            delete this;
        }
    }

protected:
    RefCounted() = default;
    virtual ~RefCounted() = default;

private:
    uint32_t ref_count_{1};  // 创建时默认持有一次
};
```

所有需要被共享管理的对象，只需继承 `RefCounted` 即可获得引用计数能力：

```cpp
class SharedBuffer : public RefCounted {
public:
    explicit SharedBuffer(size_t size) : size_(size), data_(new char[size]) {}
    ~SharedBuffer() override { delete[] data_; }

    char* data() noexcept { return data_; }
    size_t size() const noexcept { return size_; }

private:
    size_t size_;
    char* data_;
};
```

## intrusive_ptr 实现与应用场景

有了引用计数的基类，我们还需要一个智能指针来自动管理 `add_ref/release` 的调用。这就是 `intrusive_ptr`：

```cpp
template <typename T>
class IntrusivePtr {
public:
    IntrusivePtr() noexcept = default;

    explicit IntrusivePtr(T* p) noexcept : ptr_(p) {
        // 不调用 add_ref，因为 RefCounted 创建时 ref_count_ 已经是 1
    }

    IntrusivePtr(const IntrusivePtr& other) noexcept : ptr_(other.ptr_) {
        if (ptr_) ptr_->add_ref();
    }

    IntrusivePtr& operator=(const IntrusivePtr& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            if (ptr_) ptr_->add_ref();
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

    ~IntrusivePtr() { reset(); }

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

private:
    T* ptr_ = nullptr;
};
```

用法和 `shared_ptr` 几乎一样，但底层完全不同——没有控制块、没有额外的堆分配：

```cpp
void intrusive_demo() {
    IntrusivePtr<SharedBuffer> buf(new SharedBuffer(1024));
    {
        auto buf2 = buf;  // 引用计数: 1 → 2，无需额外堆分配
        std::cout << "使用缓冲区: " << buf2->data() << "\n";
    }  // 引用计数: 2 → 1

    std::cout << "缓冲区仍然有效\n";
}  // 引用计数: 1 → 0，SharedBuffer 被销毁

// 完整实现代码见 code/volumn_codes/vol2/ch01-smart-pointers/05-intrusive-ptr-demo.cpp
```

侵入式方案与 `shared_ptr` 的核心区别在于：`shared_ptr` 的控制块是在对象外部的堆上分配的（需要额外的 `new`），而侵入式方案把计数器直接放在对象内部。这意味着只有一次内存分配（对象本身），引用计数的访问不需要跳转到另一个内存位置（缓存更友好）。

侵入式方案也有一些限制：对象必须继承引用计数基类（侵入性），不方便管理已有类型的对象（比如标准库类型），而且引用计数的线程安全性需要你自己决定。但正是这种"你自己决定"的灵活性，使得侵入式方案在嵌入式系统中非常有吸引力——在单线程场景下，你可以用普通的 `uint32_t` 计数器；在多线程场景下，你需要把计数器换成 `std::atomic<uint32_t>`，但这会引入原子操作的开销。完整的多线程实现示例见 `code/volumn_codes/vol2/ch01-smart-pointers/05-intrusive-ptr-demo.cpp`。

## 嵌入式实战：硬件句柄管理

在嵌入式系统中，资源通常不是"new 出来的对象"，而是硬件句柄——DMA 通道、SPI 总线、GPIO 引脚等。这些句柄的"释放"不是 `delete`，而是调用特定的 HAL 函数。自定义删除器 + `unique_ptr`（或侵入式方案）是管理这类资源的理想工具。

```cpp
// DMA 缓冲区管理——使用 unique_ptr + 自定义删除器
struct DmaBufferDeleter {
    void operator()(DmaBuffer* buf) noexcept {
        if (buf) {
            hal_dma_free(buf->data);  // 释放 DMA 缓冲区
            delete buf;
        }
    }
};

using UniqueDmaBuffer = std::unique_ptr<DmaBuffer, DmaBufferDeleter>;

UniqueDmaBuffer allocate_dma(size_t size) {
    void* data = hal_dma_alloc(size);
    if (!data) return nullptr;
    return UniqueDmaBuffer(new DmaBuffer{data, size});
}

// 共享硬件资源——使用侵入式引用计数
class SharedPeripheral : public RefCounted {
public:
    explicit SharedPeripheral(int peripheral_id)
        : id_(peripheral_id)
    {
        hal_peripheral_acquire(id_);
    }

    ~SharedPeripheral() override {
        hal_peripheral_release(id_);
    }

    void write(const uint8_t* data, size_t len) {
        hal_peripheral_write(id_, data, len);
    }

private:
    int id_;
};

// 多个模块共享同一个外设
void peripheral_sharing() {
    auto spi = IntrusivePtr<SharedPeripheral>(new SharedPeripheral(SPI1));

    auto task1 = spi;  // 引用计数 2
    auto task2 = spi;  // 引用计数 3

    task1->write(tx_data, len);
    // 三个持有者都离开后，外设自动释放
}
```

这种模式在嵌入式驱动开发中非常常见。`unique_ptr` + 无状态删除器适合"独占使用"的场景（一次只有一个模块持有），侵入式引用计数适合"共享使用"的场景（多个模块同时持有），两者都比 `shared_ptr` 更轻量、更适合资源受限的环境。

## 小结

自定义删除器让智能指针突破了"只能管理 new/delete"的限制，能够适配任何类型的资源释放方式。函数指针、lambda、函数对象三种删除器形态各有优劣：函数对象通过 EBO 可以实现零开销，是性能敏感场景的首选；lambda 编写方便但要注意捕获带来的大小增加；函数指针最直观但会翻倍 `unique_ptr` 的大小。

侵入式引用计数则是在性能和内存受限场景下替代 `shared_ptr` 的有效方案。通过把引用计数嵌入对象内部，省去了控制块的堆分配和额外的间接访问。代价是需要修改对象类型（侵入性），但在嵌入式和游戏引擎等性能敏感领域，这种权衡通常值得。

下一篇我们将讨论 scope_guard——一种更通用的 RAII 变体，它不仅能管理资源，还能管理任何需要在作用域退出时执行的操作。

## 参考资源

- [cppreference: std::unique_ptr, Deleters](https://en.cppreference.com/w/cpp/memory/unique_ptr)
- [Empty Base Optimization and no_unique_address](https://www.cppstories.com/2021/no-unique-address/)
- [Boost intrusive_ptr documentation](https://www.boost.org/doc/libs/1_40_0/libs/smart_ptr/intrusive_ptr.html)
- [C++ Core Guidelines: R.20-24](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rr-smart)
- [P0468R0: An Intrusive Smart Pointer Proposal](https://www.open-std.org/jc1/sc22/wg21/docs/papers/2016/p0468r0.html)
下一篇我们将讨论 scope_guard——一种更通用的 RAII 变体，它不仅能管理资源，还能管理任何需要在作用域退出时执行的操作。
- [P0468R0: An Intrusive Smart Pointer Proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0468r0.html)

## 验证代码

本文中涉及的技术断言均通过以下代码验证（在 x86_64-linux-gnu 平台，g++ 15.2.1）：

1. **删除器 sizeof 验证**：`code/volumn_codes/vol2/ch01-smart-pointers/05-custom-deleter-sizeof.cpp`
   - 验证函数指针、lambda、函数对象作为删除器时的内存占用
   - 验证空基类优化（EBO）对 `unique_ptr` 大小的影响

2. **删除器性能基准测试**：`code/volumn_codes/vol2/ch01-smart-pointers/05-custom-deleter-benchmark.cpp`
   - 对比 `unique_ptr` 和 `shared_ptr` 在使用自定义删除器时的性能差异
   - 测试条件：100000 次迭代，-O2 优化级别

3. **侵入式引用计数完整实现**：`code/volumn_codes/vol2/ch01-smart-pointers/05-intrusive-ptr-demo.cpp`
   - 完整的 `IntrusivePtr` 实现
   - 单线程和多线程版本的引用计数基类
   - 与 `shared_ptr` 的对比演示

编译和运行方法：

```bash
cd code/volumn_codes/vol2/ch01-smart-pointers
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/05-custom-deleter-sizeof
./build/05-custom-deleter-benchmark
./build/05-intrusive-ptr-demo
```

或使用 g++ 直接编译：
