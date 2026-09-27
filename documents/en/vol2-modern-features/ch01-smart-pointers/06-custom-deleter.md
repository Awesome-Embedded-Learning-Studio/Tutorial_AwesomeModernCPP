---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Wrapping C APIs, managing special resources, and implementing an intrusive
  smart pointer
difficulty: intermediate
order: 6
platform: host
prerequisites:
- 'Chapter 1: Deep Dive into unique_ptr: The Zero-Overhead Smart Pointer with Exclusive Ownership'
- 'Chapter 1: Deep Dive into shared_ptr: Shared Ownership and Reference Counting'
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
  source: documents/vol2-modern-features/ch01-smart-pointers/06-custom-deleter.md
  source_hash: ec2532385b273f49de8e9b2cfcaea85594946bc8559cc4560114d80e48dc9269
  translated_at: '2026-09-27T04:51:03+00:00'
  engine: anthropic
  token_count: 3200
---
# Custom Deleters and Intrusive Reference Counting

So far, every smart pointer we have discussed has managed "objects that came from new"—call `delete` in the destructor, and everything falls out naturally. The real world is far more complicated than that. The resource you need to manage might be a `FILE*` returned by `fopen()` (to be closed with `fclose`), memory allocated by `malloc()` (to be released with `free`), a POSIX file descriptor `int` (to be closed with `close`), an SDL window, an OpenGL texture, a CUDA stream—every kind of resource comes with its own release function. If smart pointers could only ever `delete`, they would be of pretty limited value.

A custom deleter is the key mechanism that lets smart pointers adapt to all these "non-standard" resources. Intrusive reference counting, in turn, is an important alternative to `shared_ptr` in performance-critical and memory-constrained settings. We are putting the two topics together today because they both revolve around the same core question: **how to make C++ smart pointers manage resources that did not come from `new`**.

## The Three Forms of a Deleter

A custom deleter is at heart just a "callable object"—invoked when the smart pointer is destroyed, and responsible for releasing the resource<RefLink :id="1" preview="cppreference std::unique_ptr — Deleters and member types" />. It can be a function pointer, a lambda expression, or a function object (functor). Each of the three forms has its own character, so let's walk through them one by one, starting with the simplest.

### Function Pointers: The Most Intuitive Form

Function pointers are the easiest form of deleter to understand. You pass in the address of a function, and the smart pointer calls it on destruction. But function pointers have one drawback: they enlarge `unique_ptr`, because `unique_ptr` has to store that function pointer as an extra member.

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

    // close_file(fp.get()) is invoked automatically on scope exit
}
```

You can also use `decltype` to simplify the type declaration and avoid writing out the function pointer type by hand:

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

> **Note**: The numbers above were measured on x86_64-linux-gnu (GCC 16.1.1). Implementations on other platforms and compilers may differ slightly.

### Lambda: Flexible and Modern

Lambdas are the most common deleter form in modern C++. A captureless lambda converts to a function pointer, so its memory cost is the same as one. But a lambda with captures becomes a stateful deleter and enlarges `unique_ptr`.

```cpp
// Captureless lambda — equivalent to a function pointer
auto file_closer = [](FILE* f) noexcept {
    if (f) std::fclose(f);
};
using LambdaFilePtr = std::unique_ptr<FILE, decltype(file_closer)>;

// sizeof(LambdaFilePtr) == sizeof(FILE*) == 8 (EBO at work)

// Lambda with captures — stateful, enlarges unique_ptr
void captured_lambda_example() {
    int log_fd = 42;  // pretend this is a log file descriptor

    auto logging_closer = [log_fd](FILE* f) noexcept {
        if (f) {
            // captured variables are accessible inside the deleter
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

Function objects (functors) are the best choice for stateless deleters—they carry none of the function pointer's storage overhead, and they are easier to reuse and name than lambdas. The key is the Empty Base Optimization (EBO): if a class has no data members at all (an empty class), the compiler can optimize its size down to 0. `unique_ptr` typically achieves EBO by inheriting from the deleter type, so an empty deleter adds nothing to `unique_ptr`'s size.

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
    // freed automatically on destruction

    // sizeof comparison: EBO kicks in, sizeof(buf) == sizeof(char*)
    std::cout << sizeof(buf) << "\n";  // 8 (x86_64)
}
```

## Zero Overhead for Stateless Deleters: EBO in Depth

"Zero overhead" is not an empty slogan—the Empty Base Optimization (EBO) is a compiler optimization technique in C++: when an empty class (no data members, no virtual functions) is used as a base class, the compiler can optimize its size down to 0 bytes, requiring no extra memory. A typical `unique_ptr` implementation stores the deleter as a base class (through inheritance), so when the deleter is an empty class, the whole `unique_ptr` contains nothing but a raw pointer<RefLink :id="2" preview="Bartlomiej Filipek, Empty Base Class Optimisation, no_unique_address and unique_ptr, C++ Stories, 2021" />.

Let's verify (on x86_64-linux-gnu, GCC 16.1.1):

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

This verification program is right below—click "Try it yourself" to run it directly (on a 64-bit platform):

<OnlineCompilerDemo
  title="Hands-On Verification: Stateful vs Stateless Deleters—How Much unique_ptr Grows"
  source-path="code/examples/vol2/30_custom_deleter_sizeof.cpp"
  description="Compare sizeof online: the default deleter and an empty function object are both 8 bytes (EBO at work), while a deleter with data members and a function pointer both grow to 16 bytes."
  run-options="-std=c++17"
  allow-run
/>

The data is unambiguous: empty deleters (the default deleter as well as empty function objects) do not increase the size of `unique_ptr`. Only stateful deleters (a lambda that captured variables, a function object with data members, a function pointer) add size.

Here is the memory layout of the two forms side by side:

![Memory layout comparison of unique_ptr with stateless vs stateful deleters](./06-custom-deleter-layout.drawio)

That is also why we recommend function objects over function pointers in performance-sensitive scenarios—a function object can achieve zero overhead through EBO, while a function pointer always needs extra storage.

## FILE* Management and C API Wrapping in Practice

With the basic principles of deleters in hand, let's look at a few real wrapping scenarios. The first is the most common kind of C API wrapping: managing a `FILE*` with `unique_ptr`.

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
    // No manual fclose needed — RAII takes care of it
}
```

The second scenario is wrapping `malloc`/`free`:

```cpp
struct FreeDeleter {
    void operator()(void* p) noexcept {
        std::free(p);
    }
};

// A type-safe smart pointer for memory returned by malloc
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

Graphics programming is full of resources that each need their own specific release function. A `unique_ptr` with a custom deleter manages them elegantly:

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

One detail here is worth noting: an OpenGL texture ID is a `GLuint` (an integer), not a pointer. But `unique_ptr` can only manage pointer types. So we put the `GLuint` on the heap (`new GLuint`) and let `unique_ptr` manage that heap-allocated `GLuint`. At destruction the deleter calls both `glDeleteTextures` and `delete`. This "indirection" looks less than perfect, but in practice it is the standard approach.

## shared_ptr Deleters: Type Erasure

Everything so far has been about `unique_ptr` deleters—the deleter type is part of the `unique_ptr` type. `shared_ptr` deleters are fundamentally different: **the deleter type is not part of the `shared_ptr` type**—it gets "erased" and stored in the control block<RefLink :id="3" preview="Raymond Chen, Inside STL: The different types of shared pointer control blocks, The Old New Thing, 2023" />.

This means a single `shared_ptr<T>` type can hold objects with different deleters:

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
    // but hold different deleters internally (fclose vs free)
    // on destruction the correct release function gets called
}
```

This "runtime polymorphism" flexibility is the advantage of `shared_ptr` deleters, but it comes at a price: the deleter is stored in the control block (an extra heap allocation), and every destruction has to call the deleter through a function pointer. Creating and destroying a `shared_ptr` is roughly 30-50% slower than a `unique_ptr` (`-O2`, 100,000 iterations), with the bulk of the overhead coming from the control block's memory allocation.

## How Intrusive Reference Counting Works

Custom deleters solve the "non-standard release" problem, but `shared_ptr`'s own overhead (control block, atomic operations, an extra heap allocation) is still hard to ignore in performance-sensitive or memory-constrained scenarios. Intrusive reference counting offers an alternative: **embed the reference count inside the object itself, instead of allocating a control block externally**<RefLink :id="4" preview="Isabella Muerte, P0468R0: A Proposal to Add an Intrusive Smart Pointer to the C++ Standard Library, WG21, 2016" />.

The core idea of the intrusive approach is plain: the object itself knows "how many holders I have". The reference count lives as a member variable of the object rather than being allocated in a separate control block. That means no extra heap allocation (saving the control block's memory and management overhead), and access to the count is local (sharing a cache line with the object's other members).

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

Any object that needs to be shared simply inherits `RefCounted` and gains reference counting:

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

With the reference-counted base class in hand, we still need a smart pointer to automate the `add_ref`/`release` calls. That is `intrusive_ptr`<RefLink :id="5" preview="Boost.SmartPtr — intrusive_ptr: reference count stored inside the managed object" />:

```cpp
template <typename T>
class IntrusivePtr {
public:
    IntrusivePtr() noexcept = default;

    explicit IntrusivePtr(T* p) noexcept : ptr_(p) {
        // No add_ref call: RefCounted is born with ref_count_ == 1
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

Usage looks almost identical to `shared_ptr`, but what's underneath is completely different—no control block, no extra heap allocation:

```cpp
void intrusive_demo() {
    IntrusivePtr<SharedBuffer> buf(new SharedBuffer(1024));
    {
        auto buf2 = buf;  // refcount: 1 → 2, no extra heap allocation
        std::cout << "使用缓冲区: " << buf2->data() << "\n";
    }  // refcount: 2 → 1

    std::cout << "缓冲区仍然有效\n";
}  // refcount: 1 → 0, SharedBuffer destroyed
```

The core difference between the intrusive approach and `shared_ptr` is this: `shared_ptr`'s control block is allocated on the heap outside the object (requiring an extra `new`), while the intrusive approach puts the counter directly inside the object. That means a single memory allocation (the object itself), and accessing the reference count never jumps to another memory location (more cache-friendly).

The intrusive approach also has its limitations: objects must inherit the reference-counted base class (that's the "intrusive" part), it is awkward for managing objects of types that already exist (standard library types, for instance), and the thread safety of the reference count is your decision. But that very "you decide" flexibility is exactly what makes the intrusive approach so attractive in embedded systems—in a single-threaded scenario you can use a plain `uint32_t` counter; in a multi-threaded scenario you need to swap the counter for a `std::atomic<uint32_t>`, which brings in the overhead of atomic operations.

## Embedded in Practice: Managing Hardware Handles

In embedded systems, resources usually aren't "objects that came from new" but hardware handles—DMA channels, SPI buses, GPIO pins, and so on. "Releasing" these handles is not `delete`; it is calling a specific HAL function. Custom deleters + `unique_ptr` (or the intrusive approach) are the ideal tools for managing this kind of resource.

```cpp
// DMA buffer management — unique_ptr + a custom deleter
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

// Sharing a hardware resource — intrusive reference counting
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

// Several modules sharing one peripheral
void peripheral_sharing() {
    auto spi = IntrusivePtr<SharedPeripheral>(new SharedPeripheral(SPI1));

    auto task1 = spi;  // refcount 2
    auto task2 = spi;  // refcount 3

    task1->write(tx_data, len);
    // Once all three holders are gone, the peripheral is released automatically
}
```

This pattern is extremely common in embedded driver development. `unique_ptr` + a stateless deleter fits "exclusive use" scenarios (only one module holds it at a time); intrusive reference counting fits "shared use" scenarios (several modules hold it simultaneously). Both are lighter than `shared_ptr` and better suited to resource-constrained environments<RefLink :id="6" preview="C++ Core Guidelines R.20-24 — smart pointer rules" />.

Next time we'll talk about scope_guard—a more general RAII variant that manages not just resources, but any "action you want executed on scope exit".

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="std::unique_ptr"
    chapter="Deleters; Member types"
    url="https://en.cppreference.com/w/cpp/memory/unique_ptr"
  />
  <ReferenceItem
    :id="2"
    author="Bartlomiej Filipek"
    title="Empty Base Class Optimisation, no_unique_address and unique_ptr"
    publisher="C++ Stories"
    :year="2021"
    url="https://www.cppstories.com/2021/no-unique-address/"
  />
  <ReferenceItem
    :id="3"
    author="Raymond Chen"
    title="Inside STL: The Different Types of Shared Pointer Control Blocks"
    publisher="The Old New Thing, Microsoft DevBlogs"
    :year="2023"
    url="https://devblogs.microsoft.com/oldnewthing/20230821-00/?p=108626"
  />
  <ReferenceItem
    :id="4"
    author="Isabella Muerte"
    title="P0468R0: A Proposal to Add an Intrusive Smart Pointer to the C++ Standard Library"
    publisher="WG21 / ISO C++ Committee"
    :year="2016"
    url="https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0468r0.html"
  />
  <ReferenceItem
    :id="5"
    author="Boost"
    title="Boost.SmartPtr — intrusive_ptr"
    publisher="boost.org"
    url="https://www.boost.org/libs/smart_ptr/"
  />
  <ReferenceItem
    :id="6"
    author="Bjarne Stroustrup / Herb Sutter (eds.)"
    title="C++ Core Guidelines — R.20-R.24: Smart Pointer Rules"
    publisher="isocpp.org"
    url="https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#rr-owner"
  />
</ReferenceCard>
