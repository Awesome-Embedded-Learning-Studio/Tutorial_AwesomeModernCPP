# 嵌入式现代 C++教程——自定义删除器（Custom Deleter）

写嵌入式代码，常常遇到“资源不是 new 就是 delete”的假象世界。现实里，你可能得释放的不只是 `new` 出来的内存：外设句柄、MMIO 映射、DMA 缓冲、FILE*、socket、或者某个 C API 的 `free()`。这时候，C++ 的自定义删除器就像一个可靠的清道夫——把资源清理的细节藏到智能指针后面，让你把注意力放回功能实现。今天我们带着一点幽默（和大量实例）把这个话题讲清楚，顺带告诉你在内存受限的嵌入式环境下应该注意什么。

------

## 为什么要用自定义删除器？

因为世界并不总是 `delete ptr;`。你需要调用 `fclose()`、`free()`、`hal_release_buffer()`、`munmap()`、`close()` 或者通知某个硬件控制器释放通道。也许还要把释放动作带着上下文（比如 allocator 指针 或 device handle）。把释放逻辑放到删除器里有三大好处：自动化（RAII）、类型安全（不忘释放、不会双重释放）、可组合（智能指针 + 删除器 = 干净的 API）。

现在讲例子——代码比大道理更能说话。

------

### 最简单的场景：`FILE*` 与 `fclose`

C 风格的文件句柄是很常见的例子。直接把 `FILE*` 放进 `unique_ptr`，并用 `fclose` 作为删除器：

```cpp
#include <cstdio>
#include <memory>

// 使用函数指针作为删除器类型
using FilePtr = std::unique_ptr<FILE, decltype(&fclose)>;

FilePtr open_file(const char* path, const char* mode) {
    FILE* f = std::fopen(path, mode);
    return FilePtr(f, &fclose); // 智能指针负责 fclose
}

void example() {
    auto fp = open_file("/tmp/log.txt", "w");
    if (fp) std::fprintf(fp.get(), "hello, embedded world\n");
} // 离开作用域时自动 fclose
```

注意这里 `unique_ptr` 的第二个模板参数是 `decltype(&fclose)`，也可以直接写成 `void(*)(FILE*)`。函数指针作为删除器时，`unique_ptr` 的类型大小会包含一个指针（即比裸指针大一倍）。

------

### 无捕获的 lambda / 小型函数对象（高性能选项）

在嵌入式里我们通常关心二件事：RAM/ROM 占用与运行时开销。删除器是 `unique_ptr` 类型的一部分，所以它的类型大小会影响每个智能指针变量的大小。幸运的是，如果删除器是一个空的、无状态的类型（比如空 struct 或者无捕获 lambda），编译器通常能通过 **Empty Base Optimization (EBO)** 把 `unique_ptr` 压缩回与裸指针相同的大小。

示例：用无状态函数对象封装 `free()`：

```cpp
#include <cstdlib>
#include <memory>
#include <iostream>

struct FreeDeleter {
    void operator()(void* p) noexcept {
        std::free(p);
    }
};

void example() {
    using Ptr = std::unique_ptr<char, FreeDeleter>;
    Ptr p(static_cast<char*>(std::malloc(128))); // malloc/ free 交给删除器
    std::strcpy(p.get(), "hi");
    std::puts(p.get());
    // p 离开作用域时调用 FreeDeleter::operator()(p.get())
}
```

`FreeDeleter` 标记为无状态（没有成员），因此通常不会增加 `unique_ptr` 的大小。对于嵌入式，这是非常有用的：零运行时开销、类型在编译期就确定。

**实践建议**：删除器最好声明为 `noexcept`（或其 `operator()` 标注 `noexcept`），因为 `unique_ptr` 在析构时调用删除器时不希望抛异常 —— 异常会导致 `std::terminate`，这是你不想在 MCU 上遇到的。

------

### 有状态删除器（需要上下文时用它）

有时释放动作需要上下文，比如要通过某个 allocator/driver 对象来释放资源，或者需要一个 device handle。此时删除器会有成员（状态），但也意味着 `unique_ptr` 的大小会变大。

```cpp
// 有状态删除器示例：通过 HAL 接口释放 DMA buffer
struct DmaController {
    void release_buffer(void* p);
    // ... driver 状态
};

struct DmaDeleter {
    DmaController* ctrl;
    void operator()(void* p) noexcept {
        if (p) ctrl->release_buffer(p);
    }
};

void example(DmaController* ctrl) {
    using DmaPtr = std::unique_ptr<uint8_t, DmaDeleter>;
    // 假设 dma_alloc 返回裸指针
    uint8_t* buf = dma_alloc(1024);
    DmaPtr p(buf, DmaDeleter{ctrl}); // 删除器内部持有指针到 controller
} // 离开作用域自动调用 ctrl->release_buffer
```

有状态删除器的好处是灵活，但代价是：智能指针不再是“只含一个指针”的小结构——它包含删除器的状态。嵌入式工程师要衡量：每个实例是否真的需要自己的状态？还是可以把状态提升为全局/单例/线程本地，从而使用无状态删除器？

------

### `shared_ptr` 的删除器：运行时删除策略（类型擦除）

如果需要运行时选择删除逻辑（例如某些资源在运行时决定如何释放），`shared_ptr` 支持在构造时传入自定义删除器（类型擦除是在 `shared_ptr` 内部实现的），而且删除器不是 `shared_ptr` 类型的一部分，因此不会影响 `shared_ptr<T>` 的类型大小。

```cpp
#include <memory>
#include <unistd.h> // POSIX close
#include <iostream>

std::shared_ptr<int> make_fd_shared(int raw_fd) {
    // 将 fd 存在堆上，shared_ptr 管理其生命周期并自带删除器
    return std::shared_ptr<int>(new int(raw_fd),
        [](int* p){
            if (p) {
                if (*p >= 0) ::close(*p);
                delete p;
            }
        });
}

void use_fd() {
    auto fd = make_fd_shared(open("/dev/ttyS0", O_RDWR));
    // 多处共享并自动 close
}
```

`shared_ptr` 的删除器在运行时存储在控制块里，灵活但相对开销更大（控制块、原子计数等），在嵌入式上要慎用。

------

### 当资源不是指针怎么办？（比如文件描述符是 int）

智能指针本意是管理指针，但可以把非指针资源包装为堆对象然后配合删除器，或者直接写一个轻量 RAII wrapper（在嵌入式中通常更常见也更简洁）。示例两种做法：

**方案 A（用 `shared_ptr<int>`）**：见上面的文件描述符示例——把 `int` 放在 `new int(fd)` 上，用自定义删除器 `close()`。简单但略显笨重。

**方案 B（自己写个小 RAII）**：更常见、更轻量也更清晰。

```cpp
struct FileDescriptor {
    int fd{-1};
    explicit FileDescriptor(int fd_) noexcept : fd(fd_) {}
    ~FileDescriptor() noexcept { if (fd >= 0) ::close(fd); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& o) noexcept : fd(o.fd) { o.fd = -1; }
    FileDescriptor& operator=(FileDescriptor&& o) noexcept {
        if (this != &o) {
            if (fd >= 0) ::close(fd);
            fd = o.fd;
            o.fd = -1;
        }
        return *this;
    }
};
```

这类 wrapper 在嵌入式中非常常用：比把整套删除器玩花样更直观、代码也更可控。

------

### 删除器的常见陷阱（别踩坑）

1. **捕获的 lambda 作为删除器会让智能指针变“胖”**。捕获 lambda 有状态，会把捕获的数据存在删除器对象里，从而增大 `unique_ptr` 的大小。如果你关心内存，优先使用无状态函数对象或函数指针。
2. **删除器抛异常 = `std::terminate`**。删除器在析构时必须保证不抛异常，给 `operator()` 加上 `noexcept`。
3. **多态删除（基类指针删除派生）**：如果是 `delete ptr;`，保证基类析构函数为 `virtual`。如果你用自定义删除器做特殊删除（例如通过特定 allocator 释放），确保删除器做了正确的转换/释放。
4. **类型大小注意**：`unique_ptr<T, D>` 的类型依赖于 `D`。若 `D` 很大，你就得接受更多的内存占用；若 `D` 是空类型，编译器通常能优化掉大小差异。
5. **不要在中断上下文里做复杂释放**。如果删除器会执行阻塞或慢操作（比如等待锁、IO），那就不要在中断处理路径里直接触发。把删除动作推到任务/线程。

------

### 嵌入式工程师的实用模式（贴地气的建议）

- **资源少且运行时可知：优先用 `unique_ptr` + 无状态删除器。** 编译期确定一切，体积小，零运行时开销。
- **需要运行时策略或共享所有权：`shared_ptr` + 自定义删除器。** 但要留意控制块开销，嵌入式谨慎使用。
- **资源不是指针的优先自写 RAII wrapper。** 小巧、明确、可控。若要方便与 C API 交互再考虑 `shared_ptr<int>` 等变通做法。
- **如果删除逻辑与外设/驱动强耦合：让删除器持有驱动指针或索引，并标注 `noexcept`。** 但注意这样的 `unique_ptr` 会更“重”。
- **在接口层（API）暴露智能指针类型时，尽量用具体类型而非 `std::function` 或 `void\*` 隐式处理——类型信息能帮静态分析和优化。**

------

### 一点额外的高级小花招（老鸟技巧）

- 无捕获 lambda 可以写得漂亮又类型小巧，但你需要写 `decltype(lambda)` 作为 `unique_ptr` 的删除器类型，或者用 `auto` 变量（C++14/17 写法）推导。示例：

```cpp
auto deleter = [](FILE* f) noexcept { if (f) fclose(f); };
using FilePtr2 = std::unique_ptr<FILE, decltype(deleter)>;

FilePtr2 make_fp(const char* path) {
    FILE* f = fopen(path, "r");
    return FilePtr2(f, deleter);
}
```

- 如果你不得不在接口层隐藏删除器类型（比如库 API 不想暴露复杂模板），可以在内部用 `unique_ptr`，对外提供轻量的 handle 或者专门的 RAII 类型。

------

## 小结

自定义删除器不是魔法，但它是一个把“谁来释放”这个烦人的问题放到正确位置的优雅工具。嵌入式场景下，我们在意的是二件事：**内存/二进制体积** 与 **运行时开销/确定性**。把删除策略分为三类：编译期删除器（无状态、`unique_ptr` 最优）、运行时删除器（`shared_ptr` 灵活但有代价）、以及传统的 RAII wrapper（明确、轻量、可控）。每次设计 API 时问自己一句话：**这份资源谁能最安全、最高效地在正确的时间释放？**