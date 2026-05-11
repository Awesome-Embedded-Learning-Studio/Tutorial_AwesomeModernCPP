# Custom Deleters and Intrusive Reference Counting

So far, the smart pointers we have discussed all manage "objects created with new" — calling `delete` upon destruction, which happens naturally. But the real world is far more complex. The resources you need to manage might be a `FILE*` returned by `fopen` (which needs to be closed with `fclose`), memory allocated by `malloc` (which needs to be freed with `free`), a POSIX file descriptor `int fd` (which needs to be closed with `close`), an SDL window, an OpenGL texture, a CUDA stream — each resource has its own release function. If a smart pointer could only `delete`, it would be far too limited.

A custom deleter is the key mechanism that enables smart pointers to adapt to various "non-standard" resources. Intrusive reference counting, on the other hand, is an important alternative to `shared_ptr` in performance-sensitive and memory-constrained scenarios. We discuss these two topics together today because they both revolve around the same core problem: **how to make C++ smart pointers manage resources that "weren't created with new"**.

## Three Forms of Deleters

A custom deleter is essentially a "callable object" — invoked when the smart pointer is destroyed, responsible for releasing the resource. It can be a function pointer, a lambda expression, or a function object (functor). Each of these three forms has its own characteristics, and we will walk through them one by one, starting with the simplest.

### Function Pointers: The Most Intuitive Approach

Function pointers are the easiest form of deleter to understand. You pass in the address of a function, and the smart pointer calls it upon destruction. However, function pointers have a drawback: they increase the size of `unique_ptr`, because `unique_ptr` needs to store this function pointer additionally.

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

We can also use `decltype` to simplify the type declaration, avoiding the need to manually write out the function pointer type:

```cpp
using FilePtr = std::unique_ptr<FILE, decltype(&std::fclose)>;
FilePtr make_file(const char* path, const char* mode) {
    return FilePtr(std::fopen(path, mode), &std::fclose);
}
```

`sizeof` comparison — a function pointer deleter doubles the size of `unique_ptr`:

```cpp
std::cout << sizeof(std::unique_ptr<int>) << "\n";                        // 8
std::cout << sizeof(std::unique_ptr<FILE, void(*)(FILE*)>) << "\n";       // 16
std::cout << sizeof(std::unique_ptr<FILE, decltype(&std::fclose)>) << "\n"; // 16
```

> **Note**: The above values were tested on the x86_64-linux-gnu platform (g++ 15.2.1). Implementations may vary slightly across different platforms and compilers. For the full verification code, see `code/volumn_codes/vol2/ch01-smart-pointers/05-custom-deleter-sizeof.cpp`.

### Lambdas: Flexible and Modern

Lambdas are the most commonly used deleter form in modern C++. A stateless lambda can be converted to a function pointer, so it has the same memory overhead as a function pointer. However, a lambda with captures becomes a stateful deleter, increasing the size of `unique_ptr`.

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

### Function Objects: The Most Efficient Approach

Function objects (functors) are the best choice for stateless deleters — they have neither the storage overhead of function pointers nor the reusability and naming challenges of lambdas. The key lies in EBO (Empty Base Optimization): if a class has no data members (an empty class), the compiler can optimize its size to zero. `unique_ptr` typically implements EBO by inheriting from the deleter type, so an empty deleter does not increase the size of `unique_ptr`.

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

## Zero Overhead of Stateless Deleters: EBO Explained

"Zero overhead" is not just an empty phrase — EBO (Empty Base Optimization) is an optimization technique in C++ compilers: when an empty class (no data members, no virtual functions) is used as a base class, the compiler can optimize its size to zero bytes, requiring no additional memory space. A typical implementation of `unique_ptr` stores the deleter as a base class (through inheritance), so when the deleter is an empty class, the entire `unique_ptr` contains only a raw pointer.

Let's verify this (on the x86_64-linux-gnu platform, g++ 15.2.1):

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

Typical output on a 64-bit platform (g++ 15.2.1, -O0):

```text
sizeof(int*):                              8
sizeof(unique_ptr<int>):                    8
sizeof(unique_ptr<int, EmptyDeleter>):      8
sizeof(unique_ptr<int, StatefulDeleter>):   16
sizeof(unique_ptr<int, void(*)(int*)>):     16
```

For the full verification code, see `code/volumn_codes/vol2/ch01-smart-pointers/05-custom-deleter-sizeof.cpp`.

The data is clear: empty deleters (including the default deleter and empty function objects) do not increase the size of `unique_ptr`. Only stateful deleters (such as lambdas that capture variables, function objects with data members, or function pointers) increase the size.

This is also why the author recommends using function objects over function pointers in performance-sensitive scenarios — function objects can achieve zero overhead through EBO, whereas function pointers always require additional storage space.

## FILE* Management and C API Wrapping in Practice

Having grasped the basic principles of deleters, let's look at a few practical wrapping scenarios. The first is the most common C API wrapping: using `unique_ptr` to manage `FILE*`.

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

The second scenario is wrapping `malloc`:

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

### SDL/OpenGL Resource Management Example

Graphics programming is full of resources that require specific release functions. Using `unique_ptr` with custom deleters allows us to manage them elegantly:

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

There is a detail worth noting here: an OpenGL texture ID is a `GLuint` (an integer), not a pointer. But `unique_ptr` can only manage pointer types. So we place the `GLuint` on the heap (`new GLuint`), and then use `unique_ptr` to manage this heap-allocated `GLuint`. The deleter calls both `glDeleteTextures` and `delete` upon destruction. Although this "indirection" might seem less than perfect, it is standard practice.

## shared_ptr Deleters: Type Erasure

The deleters discussed above are all for `unique_ptr` — the deleter type is part of the `unique_ptr` type. A `shared_ptr` deleter, however, has a fundamental difference: **the deleter type is not part of the `shared_ptr` type**; it is "erased" and stored in the control block.

This means you can hold objects with different deleters using the same `shared_ptr` type:

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

The flexibility of this "runtime polymorphism" is an advantage of `shared_ptr` deleters, but it comes with a cost: the deleter is stored in the control block (an extra heap allocation), and each destruction requires invoking the deleter through a function pointer. According to benchmarks (g++ 15.2.1, -O2, 100,000 iterations), the creation and destruction of `shared_ptr` is about 30-50% slower than `unique_ptr`, with the main overhead coming from the memory allocation of the control block. For the full test code, see `code/volumn_codes/vol2/ch01-smart-pointers/05-custom-deleter-benchmark.cpp`.

## Principles of Intrusive Reference Counting

Custom deleters solve the problem of "non-standard release," but the overhead of `shared_ptr` itself (control block, atomic operations, extra heap allocation) remains significant in performance-sensitive or memory-constrained scenarios. Intrusive reference counting provides an alternative: **embedding the reference count inside the object itself, rather than allocating a control block externally**.

The core idea of the intrusive approach is very simple: the object itself knows "how many people hold me." The reference count exists as a member variable of the object, rather than being allocated in a separate control block. This means no extra heap allocation is needed (eliminating the memory and management overhead of the control block), and access to the reference count is local (in the same cache line as the object's other members).

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

Any object that needs to be managed with shared ownership simply inherits from `RefCounted` to gain reference counting capabilities:

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

## intrusive_ptr Implementation and Use Cases

With the reference counting base class in place, we still need a smart pointer to automatically manage the calls to `add_ref` and `release`. This is where `intrusive_ptr` comes in:

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

Its usage is almost identical to `shared_ptr`, but the underlying mechanism is completely different — there is no control block and no extra heap allocation:

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

The core difference between the intrusive approach and `shared_ptr` lies in this: the control block of `shared_ptr` is allocated on the heap outside the object (requiring an extra `new`), whereas the intrusive approach places the counter directly inside the object. This means there is only one memory allocation (the object itself), and accessing the reference count does not require jumping to another memory location (which is more cache-friendly).

The intrusive approach also has some limitations: the object must inherit from a reference counting base class (intrusiveness), it is not convenient for managing objects of existing types (such as standard library types), and the thread safety of the reference count is up to you to decide. However, it is precisely this "you decide" flexibility that makes the intrusive approach very attractive in embedded systems — in a single-threaded scenario, you can use a plain `int` counter; in a multi-threaded scenario, you need to switch the counter to `std::atomic<int>`, which introduces the overhead of atomic operations. For a complete multi-threaded implementation example, see `code/volumn_codes/vol2/ch01-smart-pointers/05-intrusive-ptr-demo.cpp`.

## Embedded in Practice: Hardware Handle Management

In embedded systems, resources are typically not "objects created with new," but rather hardware handles — DMA channels, SPI buses, GPIO pins, and so on. "Releasing" these handles does not mean calling `delete`, but rather calling specific HAL functions. Custom deleters + `unique_ptr` (or the intrusive approach) are ideal tools for managing this type of resource.

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

This pattern is very common in embedded driver development. `unique_ptr` + a stateless deleter is suitable for "exclusive use" scenarios (where only one module holds the resource at a time), while intrusive reference counting is suitable for "shared use" scenarios (where multiple modules hold the resource simultaneously). Both are lighter and more suitable for resource-constrained environments than `shared_ptr`.

## Summary

Custom deleters enable smart pointers to break through the limitation of "only managing new/delete," adapting to any type of resource release method. The three deleter forms — function pointers, lambdas, and function objects — each have their pros and cons: function objects can achieve zero overhead through EBO, making them the top choice for performance-sensitive scenarios; lambdas are convenient to write, but you must be mindful of the size increase caused by captures; function pointers are the most intuitive but double the size of `unique_ptr`.

Intrusive reference counting is an effective alternative to `shared_ptr` in performance-sensitive and memory-constrained scenarios. By embedding the reference count inside the object, it eliminates the heap allocation of the control block and the extra indirection. The trade-off is that you need to modify the object type (intrusiveness), but in performance-sensitive fields like embedded systems and game engines, this trade-off is usually worth it.

In the next article, we will discuss scope_guard — a more general RAII variant that can manage not only resources but also any operation that needs to execute when a scope exits.

## Reference Resources

- [cppreference: std::unique_ptr, Deleters](https://en.cppreference.com/w/cpp/memory/unique_ptr)
- [Empty Base Optimization and no_unique_address](https://www.cppstories.com/2021/no-unique-address/)
- [Boost intrusive_ptr documentation](https://www.boost.org/doc/libs/1_40_0/libs/smart_ptr/intrusive_ptr.html)
- [C++ Core Guidelines: R.20-24](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rr-smart)
- [P0468R0: An Intrusive Smart Pointer Proposal](https://www.open-std.org/jc1/sc22/wg21/docs/papers/2016/p0468r0.html)
In the next article, we will discuss scope_guard — a more general RAII variant that can manage not only resources but also any operation that needs to execute when a scope exits.
- [P0468R0: An Intrusive Smart Pointer Proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0468r0.html)

## Verification Code

The technical assertions made in this article have all been verified through the following code (on the x86_64-linux-gnu platform, g++ 15.2.1):

1. **Deleter sizeof verification**: `code/volumn_codes/vol2/ch01-smart-pointers/05-custom-deleter-sizeof.cpp`
   - Verifies the memory footprint when using function pointers, lambdas, and function objects as deleters
   - Verifies the impact of EBO (Empty Base Optimization) on the size of `unique_ptr`

2. **Deleter performance benchmark**: `code/volumn_codes/vol2/ch01-smart-pointers/05-custom-deleter-benchmark.cpp`
   - Compares the performance differences between `unique_ptr` and `shared_ptr` when using custom deleters
   - Test conditions: 100,000 iterations, -O2 optimization level

3. **Complete intrusive reference counting implementation**: `code/volumn_codes/vol2/ch01-smart-pointers/05-intrusive-ptr-demo.cpp`
   - Complete `intrusive_ptr` implementation
   - Single-threaded and multi-threaded versions of the reference counting base class
   - Comparison demonstration with `shared_ptr`

How to compile and run:

```bash
cd code/volumn_codes/vol2/ch01-smart-pointers
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/05-custom-deleter-sizeof
./build/05-custom-deleter-benchmark
./build/05-intrusive-ptr-demo
```

Or compile directly with g++:
