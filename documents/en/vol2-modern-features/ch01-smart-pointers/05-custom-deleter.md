---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Wrapping C APIs, managing special resources, and implementing intrusive
  smart pointers
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'Deep Dive into unique_ptr: A Zero-Overhead Smart Pointer with Exclusive Ownership'
- 'Deep Dive into shared_ptr: Shared Ownership and Reference Counting'
reading_time_minutes: 17
related:
- 'scope_guard and defer: A General-Purpose Scope Guard'
tags:
- host
- cpp-modern
- intermediate
- 智能指针
- intrusive_ptr
- 引用计数
title: Custom Deleters and Intrusive Reference Counting
translation:
  source: documents/vol2-modern-features/ch01-smart-pointers/05-custom-deleter.md
  source_hash: 901650ebfa9d0380ca1bdace3ff9641d2fdc2b58678fd44419e3d236e1eead10
  translated_at: '2026-09-25T14:42:47+00:00'
  engine: anthropic
  token_count: 4800
---
# Custom Deleters and Intrusive Reference Counting

So far, every smart pointer we've discussed manages an "object born from new"—destruction calls `delete`, and everything follows naturally. But the real world is far more complicated than that. The resource you need to manage might be a `FILE*` returned by `fopen()` (to be closed with `fclose`), memory allocated by `malloc()` (to be released with `free`), a POSIX file descriptor `int` (to be closed with `close`), an SDL window, an OpenGL texture, a CUDA stream—each kind of resource comes with its own release function. If smart pointers could only ever `delete`, they would be of precious little use.

The custom deleter is the key mechanism that lets smart pointers adapt to all these "non-standard" resources. Intrusive reference counting, meanwhile, is an important alternative to `shared_ptr` in performance-sensitive and memory-constrained scenarios. We're covering both topics today because they revolve around the same core question: **how to make C++ smart pointers manage resources that are "not born from new"**.

## Three Forms of Deleters

A custom deleter is, in essence, a "callable object"—invoked when the smart pointer is destroyed, and responsible for releasing the resource. It can be a function pointer, a lambda expression, or a function object (functor). Each of the three forms has its own traits; let's go through them one by one, starting with the simplest.

### Function Pointers: The Most Intuitive Form

The function pointer is the easiest deleter form to understand: you pass the address of a function, and the smart pointer calls it upon destruction. But function pointers have one drawback—they make the `unique_ptr` bigger, because the `unique_ptr` has to store that extra function pointer.

```cpp
#include <cstdio>
#include <memory>
#include <iostream>

// Manage a FILE* with a function pointer
void close_file(FILE* f) noexcept {
    if (f) {
        std::cout << "fclose called\n";
        std::fclose(f);
    }
}

void file_example() {
    // unique_ptr<FILE, function-pointer type>
    std::unique_ptr<FILE, void(*)(FILE*)> fp(std::fopen("/tmp/test.txt", "w"), close_file);

    if (fp) {
        std::fprintf(fp.get(), "hello from unique_ptr with custom deleter\n");
    }

    // close_file(fp.get()) is invoked automatically when leaving scope
}
```

You can also use `decltype` to simplify the type declaration and spare yourself writing the function pointer type by hand:

```cpp
using FilePtr = std::unique_ptr<FILE, decltype(&std::fclose)>;
FilePtr make_file(const char* path, const char* mode) {
    return FilePtr(std::fopen(path, mode), &std::fclose);
}
```

A `sizeof` comparison—a function-pointer deleter doubles the size of `unique_ptr`:

```cpp
std::cout << sizeof(std::unique_ptr<int>) << "\n";                        // 8
std::cout << sizeof(std::unique_ptr<FILE, void(*)(FILE*)>) << "\n";       // 16
std::cout << sizeof(std::unique_ptr<FILE, decltype(&std::fclose)>) << "\n"; // 16
```

> **Note**: The values above were measured on x86_64-linux-gnu (GCC 16.1.1). Other platforms and compilers may differ slightly.

### Lambdas: Flexible and Modern

The lambda is the most frequently used deleter form in modern C++. A captureless lambda can convert to a function pointer, so it costs exactly as much memory as one. A lambda with captures, however, becomes a stateful deleter and grows the `unique_ptr`.

```cpp
// Captureless lambda — equivalent to a function pointer
auto file_closer = [](FILE* f) noexcept {
    if (f) std::fclose(f);
};
using LambdaFilePtr = std::unique_ptr<FILE, decltype(file_closer)>;

// sizeof(LambdaFilePtr) == sizeof(FILE*) == 8 (thanks to EBO)

// Lambda with captures — stateful, grows the unique_ptr
void captured_lambda_example() {
    int log_fd = 42;  // pretend this is a log file descriptor

    auto logging_closer = [log_fd](FILE* f) noexcept {
        if (f) {
            // the deleter can access captured variables
            write_log(log_fd, "closing file");
            std::fclose(f);
        }
    };

    std::unique_ptr<FILE, decltype(logging_closer)> fp(
        std::fopen("/tmp/test.txt", "w"),
        logging_closer
    );
    // sizeof(fp) > sizeof(FILE*), because the lambda captured log_fd
    }
```

### Function Objects: The Most Efficient Form

The function object (functor) is the best choice for stateless deleters—it carries none of the storage overhead of a function pointer, and it's easier to reuse and name than a lambda. The key is the Empty Base Optimization (EBO): when a class has no data members at all (an empty class), the compiler can optimize its size down to 0. `unique_ptr` implementations typically achieve EBO by inheriting from the deleter type, so an empty deleter never enlarges the `unique_ptr`.

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
    // Manage memory allocated by malloc
    auto buf = std::unique_ptr<char, FreeDeleter>(
        static_cast<char*>(std::malloc(256))
    );
    std::strcpy(buf.get(), "hello");
    std::cout << buf.get() << "\n";  // hello
    // Automatically freed at destruction

    // sizeof comparison: EBO at work, sizeof(buf) == sizeof(char*)
    std::cout << sizeof(buf) << "\n";  // 8 (on x86_64)
}
```

## Zero Overhead for Stateless Deleters: EBO in Depth

"Zero overhead" is not an empty phrase—the Empty Base Optimization (EBO) is a real optimization technique in C++ compilers: when an empty class (no data members, no virtual functions) serves as a base class, the compiler can optimize its size down to 0 bytes, so it takes up no extra memory. A typical `unique_ptr` implementation stores the deleter as a base class (through inheritance), so when the deleter is an empty class, the whole `unique_ptr` consists of nothing more than a raw pointer.

Let's verify that (on x86_64-linux-gnu, GCC 16.1.1):

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

Typical output on a 64-bit platform (GCC 16.1.1, -O0):

```text
sizeof(int*):                              8
sizeof(unique_ptr<int>):                    8
sizeof(unique_ptr<int, EmptyDeleter>):      8
sizeof(unique_ptr<int, StatefulDeleter>):   16
sizeof(unique_ptr<int, void(*)(int*)>):     16
```


The numbers tell a clear story: empty deleters (the default deleter included, plus empty function objects) do not enlarge `unique_ptr`. Only stateful deleters (a lambda that captured variables, a function object with data members, a function pointer) add to the size.

Here is how the memory layouts of the two forms compare:

![Memory layout of unique_ptr with a stateless vs. a stateful deleter](./05-custom-deleter-layout.drawio)

This is also why we recommend function objects over function pointers in performance-sensitive settings—a function object gets zero overhead through EBO, while a function pointer always needs that extra storage.

## Managing FILE*: Wrapping C APIs in Practice

With the basic principles of deleters under our belt, let's look at a few practical wrapping scenarios. The first is the most common C API wrapping job: managing a `FILE*` with `unique_ptr`.

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
    // No manual fclose needed—RAII takes care of it
}
```

The second scenario is wrapping `malloc/free`:

```cpp
struct FreeDeleter {
    void operator()(void* p) noexcept {
        std::free(p);
    }
};

// Create a type-safe smart pointer for memory returned by malloc
template <typename T>
using MallocPtr = std::unique_ptr<T, FreeDeleter>;

template <typename T>
MallocPtr<T> malloc_array(size_t count) {
    void* mem = std::malloc(count * sizeof(T));
    if (!mem) throw std::bad_alloc();
    return MallocPtr<T>(static_cast<T*>(mem));
}
```

### An SDL/OpenGL Resource Management Example

Graphics programming is full of resources that demand specific release functions. A `unique_ptr` with a custom deleter manages them gracefully:

```cpp
// SDL window management
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

// OpenGL texture management
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
    // ... set texture parameters ...
    return UniqueGlTexture(tex.release(), GlTextureDeleter{});
}
```

One detail here is worth calling out: an OpenGL texture ID is a `GLuint` (an integer), not a pointer. But `unique_ptr` can only manage pointer types. So we put the `GLuint` on the heap (`new GLuint`) and let the `unique_ptr` manage that heap-allocated `GLuint`. On destruction the deleter calls both `glDeleteTextures` and `delete`. This layer of "indirection" may not look perfectly elegant, but in practice it is the standard approach.

## shared_ptr's Deleter: Type Erasure

Everything we've discussed so far concerned `unique_ptr`'s deleter—there, the deleter type is part of the `unique_ptr` type. `shared_ptr`'s deleter differs in one fundamental way: **the deleter type is not part of the `shared_ptr` type**—it gets "erased" and stored in the control block.

That means one and the same `shared_ptr<T>` type can hold objects that carry different deleters:

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

    // f and m have exactly the same type: shared_ptr<void>
    // but they carry different deleters inside (fclose vs free)
    // the correct release function is called on destruction
}
```

This "runtime polymorphism" flexibility is the strength of `shared_ptr`'s deleter, but it has a price: the deleter is stored in the control block (an extra heap allocation), and every destruction calls the deleter through a function pointer. Creating and destroying a `shared_ptr` is roughly 30-50% slower than a `unique_ptr` (`-O2`, 100,000 iterations), and most of that overhead comes from allocating the control block.

## The Principle of Intrusive Reference Counting

Custom deleters solve the "non-standard release" problem, but the inherent overhead of `shared_ptr` itself (control block, atomic operations, extra heap allocation) is still hard to ignore in performance-sensitive or memory-constrained settings. Intrusive reference counting offers an alternative: **embed the reference count inside the object instead of allocating a control block outside it**.

The core idea of the intrusive approach is dead simple: the object itself knows "how many holders I have". The reference count lives as a member variable of the object rather than in a separately allocated control block. That means no additional heap allocation (no control-block memory or bookkeeping overhead), and accesses to the reference count stay local (in the same cache line as the rest of the object's members).

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
    uint32_t ref_count_{1};  // one reference held by default at creation
};
```

Any object that needs shared management gains reference counting just by inheriting from `RefCounted`:

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

## Implementing intrusive_ptr and Its Use Cases

With a reference-counted base class in hand, we still need a smart pointer to drive the `add_ref`/`release` calls automatically. That's `intrusive_ptr`:

```cpp
template <typename T>
class IntrusivePtr {
public:
    IntrusivePtr() noexcept = default;

    explicit IntrusivePtr(T* p) noexcept : ptr_(p) {
        // No add_ref call: RefCounted is born with ref_count_ already at 1
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

Using it feels almost identical to `shared_ptr`, but what lies beneath is completely different—no control block, no extra heap allocation:

```cpp
void intrusive_demo() {
    IntrusivePtr<SharedBuffer> buf(new SharedBuffer(1024));
    {
        auto buf2 = buf;  // Refcount: 1 → 2, no extra heap allocation
        std::cout << "使用缓冲区: " << buf2->data() << "\n";
    }  // Refcount: 2 → 1

    std::cout << "缓冲区仍然有效\n";
}  // Refcount: 1 → 0, SharedBuffer destroyed
```

The core difference between the intrusive approach and `shared_ptr` is this: `shared_ptr`'s control block is heap-allocated outside the object (an extra `new`), while the intrusive approach puts the counter directly inside the object. That means a single memory allocation (the object itself), and reading the reference count never jumps to a different memory location (which the cache appreciates).

The intrusive approach has its limitations too: objects must inherit from a reference-counted base class (that's the "intrusive" part), it's clumsy for managing objects of types you don't control (standard library types, for instance), and the thread safety of the reference count is left for you to decide. But that very "you decide" flexibility is exactly what makes the intrusive approach so attractive in embedded systems—in a single-threaded scenario, a plain `uint32_t` counter suffices; in a multithreaded one, you swap the counter for `std::atomic<uint32_t>`, at the cost of introducing atomic-operation overhead.

## Embedded Practice: Hardware Handle Management

In embedded systems, resources are usually not "objects born from new" but hardware handles—DMA channels, SPI buses, GPIO pins, and the like. "Releasing" such a handle isn't `delete`; it's a call to a specific HAL function. A custom deleter + `unique_ptr` (or the intrusive approach) is the ideal tool for managing resources of this kind.

```cpp
// DMA buffer management—unique_ptr + custom deleter
struct DmaBufferDeleter {
    void operator()(DmaBuffer* buf) noexcept {
        if (buf) {
            hal_dma_free(buf->data);  // free the DMA buffer
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

// Sharing a hardware resource—intrusive reference counting
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

// Multiple modules share the same peripheral
void peripheral_sharing() {
    auto spi = IntrusivePtr<SharedPeripheral>(new SharedPeripheral(SPI1));

    auto task1 = spi;  // Refcount 2
    auto task2 = spi;  // Refcount 3

    task1->write(tx_data, len);
    // Once all three holders are gone, the peripheral is released automatically
}
```

This pattern is extremely common in embedded driver development. `unique_ptr` + a stateless deleter fits "exclusive use" scenarios (one module holds it at a time); intrusive reference counting fits "shared use" scenarios (several modules hold it simultaneously). Both are lighter than `shared_ptr` and better suited to resource-constrained environments.

Next up is scope_guard—a more general RAII variant that manages not only resources but any "action to run when the scope exits".

## References

- [cppreference: std::unique_ptr, Deleters](https://en.cppreference.com/w/cpp/memory/unique_ptr)
- [Empty Base Optimization and no_unique_address](https://www.cppstories.com/2021/no-unique-address/)
- [Boost intrusive_ptr documentation](https://www.boost.org/doc/libs/1_40_0/libs/smart_ptr/intrusive_ptr.html)
- [C++ Core Guidelines: R.20-24](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rr-smart)
- [P0468R0: An Intrusive Smart Pointer Proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0468r0.html)
