---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: Master the core mechanisms of move semantics and achieve zero-copy resource
  transfer
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Rvalue References: From Copy to Move'
reading_time_minutes: 23
related:
- 'RVO and NRVO: The Compiler''s Return Value Optimization'
- 'Perfect Forwarding: Preserving Value Categories Exactly'
tags:
- host
- cpp-modern
- intermediate
- 移动语义
title: Move Construction and Move Assignment
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/02-move-semantics.md
  source_hash: 44c694dcc05d07a27469491f910ec105102d9e467fdd56073499ec330cf345fe
  translated_at: '2026-09-25T14:16:20+00:00'
  engine: anthropic
  token_count: 4300
---
# Move Construction and Move Assignment: Teaching Classes to Truly Move, Not Copy

In the previous article we laid the groundwork of value categories and rvalue references. Now it's time for the real work—teaching our classes to truly "move" instead of "copy". Honestly, we made quite a few mistakes the first time we hand-wrote a move constructor: forgetting to null out the source object's pointer, forgetting to handle self-assignment, never being quite sure when to add `noexcept`... This article rounds up all the pitfalls we stumbled into, in the hope of sparing you a few detours.

We'll start from a simple but realistic-enough scenario: implement a dynamic buffer class ourselves, then use it to work through move construction, move assignment, and the so-called Rule of Five, step by step.

## Why We Need Move—Starting from the Cost of Copying

Suppose you are writing a text-processing tool that constantly passes big chunks of text data between functions. First, here is the most bare-bones dynamic buffer implementation:

```cpp
class Buffer {
    char* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    explicit Buffer(std::size_t capacity)
        : data_(new char[capacity])
        , size_(0)
        , capacity_(capacity)
    {
    }

    // Copy constructor: deep copy
    Buffer(const Buffer& other)
        : data_(new char[other.capacity_])
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        std::memcpy(data_, other.data_, size_); // Just a plain, direct copy of the data
    }

    // Copy assignment: deep copy
    Buffer& operator=(const Buffer& other)
    {
        if (this != &other) {
            delete[] data_;
            data_ = new char[other.capacity_];
            size_ = other.size_;
            capacity_ = other.capacity_;
            std::memcpy(data_, other.data_, size_);
        }
        return *this;
    }

    ~Buffer()
    {
        delete[] data_;
    }

    void append(const char* str, std::size_t len)
    {
        if (size_ + len <= capacity_) {
            std::memcpy(data_ + size_, str, len);
            size_ += len;
        }
    }

    const char* data() const { return data_; }
    std::size_t size() const { return size_; }
};
```

Now let's run an experiment: create a 1MB buffer, then pass it into a function.

```cpp
#include <iostream>

Buffer process_buffer(Buffer buf)
{
    std::cout << "处理中，大小: " << buf.size() << " 字节\n";
    return buf;
}

int main()
{
    Buffer large(1024 * 1024);  // 1MB
    large.append("Hello, World!", 13);

    Buffer result = process_buffer(large);  // A copy!
    return 0;
}
```

What happens when we call `process_buffer(large)`? The parameter `buf` is passed by value, so the compiler invokes `Buffer`'s copy constructor to create `buf`—which means allocating a fresh 1MB of memory and copying every byte of `large`'s data into it. When the function returns, `return buf;` triggers one more copy construction to create `result`. Add in the destruction of `buf` when the function ends, and the whole trip costs **two 1MB allocations, two 1MB copies, and one 1MB deallocation**—while all we really wanted was to move the data from `large` in `main` over to `result`. (We suspect the old-school C++ folks reading this are already red in the face, and we trust that you on the other side of the screen won't keep a straight face either.)

That is the fundamental problem of copy semantics: when you no longer need the source object, the copy constructor still faithfully duplicates every byte, and then the source object's destructor dutifully releases the original block of memory. Resources allocated then freed, data copied then thrown away—pure waste.

## The Move Constructor—Transferring Resource Ownership

The core idea of the move constructor is dead simple: don't copy the data, just transfer ownership of the resources. For a class that manages dynamic memory, that means "stealing" the pointer from the source object and then nulling the source object's pointer, so that its destructor won't release that memory.

```cpp
class Buffer {
    char* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    // ... The constructors and the destructor from before stay unchanged ...

    // The move constructor
    Buffer(Buffer&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
};
```

Let's walk through this move constructor line by line. The `&&` in the signature `Buffer(Buffer&& other)` announces a move constructor—it accepts only rvalue arguments. Inside the function body we do three things: copy `other`'s three members straight into `this` (three pointer/integer assignments, dirt cheap), then null out `other`'s pointer. That last step is the critical one—if we don't null `other.data_`, then when `other` is destroyed, `delete[] other.data_` releases the very memory we just took over, `this` ends up holding a dangling pointer, and any later access is a guaranteed crash.

Now let's trigger the move constructor with `std::move`:

```cpp
Buffer large(1024 * 1024);
large.append("Hello, World!", 13);

Buffer moved_to = std::move(large);  // Invokes the move constructor
// large.data_ is now nullptr, yet large can still be destroyed safely
// moved_to owns the original 1MB of memory
```

What did the whole operation do? A few pointer/integer assignments and we're done—`other`'s members are carried over, then `other` is zeroed out. No `new`, no `memcpy`, no `delete`. What used to be an O(n) copy becomes an O(1) pointer transfer. For a 1MB buffer, this is the gap between "allocate 1MB of memory and copy 1MB of data" and "shuffle a few registers".

## The Move Assignment Operator—One Step More than Move Construction

The move assignment operator is slightly more complicated than the move constructor, because the target of the assignment may already hold resources—we must release the old resources first, then take over the new ones.

```cpp
class Buffer {
    // ... The code above stays unchanged ...

    // The move assignment operator
    Buffer& operator=(Buffer&& other) noexcept
    {
        if (this != &other) {
            // Step 1: release the resources we currently hold
            delete[] data_;

            // Step 2: take over other's resources
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;

            // Step 3: null out other
            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }
};
```

Pay attention to that first step, `delete[] data_`—it is the key difference between move assignment and move construction. During move construction the target object is not initialized yet, so there are no old resources to release; during move assignment the target object already exists, and if you don't release the old resources first, you leak memory. The self-assignment check `if (this != &other)` is also necessary—code like `x = std::move(x)` almost never shows up in normal development, but when it does, it goes wrong: `delete[] data_` first releases your own resource, then you grab the pointers from the already-dangling `other` (which is really yourself), and you get an instant use-after-free. Adding the check—a few lines of code in exchange for determinism—is worth it.

Let's see what move assignment does in real code:

```cpp
Buffer a(1024);
a.append("Hello", 5);

Buffer b(2048);
b.append("World", 5);

a = std::move(b);  // Move assignment
// a's original 1KB buffer was released by delete[]
// a took over b's 2KB buffer
// b.data_ is now nullptr
```

After the move, the source object is left in a "valid but unspecified" state. That means you can safely assign it a new value or let it be destroyed, but you should not read its value—for instance, `moved_from.size()` might return 0 or the original value, depending on the implementation. Our advice: right after a move, either get the source object out of scope or assign it a definite new value—never let a moved-from object wander around in your code.

## noexcept—The Safety Promise of Move Operations

You may have noticed that both move operations are marked `noexcept`. This is not optional decoration—it has a very real performance impact.

The reason lies in `std::vector`'s reallocation behavior. When a `vector` needs to grow its capacity, it must transfer the existing elements into the new memory block. If the element type's move constructor is `noexcept`, the `vector` moves with confidence; if the move constructor might throw, the `vector` falls back to the copy constructor—because an exception thrown midway through a move leaves a half-moved state that is very hard to recover, whereas if an exception is thrown during a copy, the original data is still intact.

```cpp
// A simplified version of vector's internal logic
if constexpr (std::is_nothrow_move_constructible_v<T>) {
    // Use move construction—fast and safe
} else {
    // Fall back to copy construction—slower but exception-safe
}
```

You can use `static_assert` to verify that your class really satisfies `noexcept` moving:

```cpp
static_assert(std::is_nothrow_move_constructible_v<Buffer>,
              "Buffer should be nothrow move constructible");
static_assert(std::is_nothrow_move_assignable_v<Buffer>,
              "Buffer should be nothrow move assignable");
```

This is no armchair theory—we can write an experiment to verify `vector`'s actual behavior. Prepare two identically structured `Buffer` classes whose only difference is whether the move constructor carries `noexcept`, then let the `vector` grow. The easiest way is a single template parameter `NoexceptMove` that toggles the `noexcept` marker, with all the remaining code identical:

```cpp
// noexcept_vector_realloc.cpp -- noexcept move vs non-noexcept move: the difference when a vector reallocates
// Standard: C++17

#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// The template parameter NoexceptMove toggles whether the move constructor is marked noexcept; everything else is identical
template <bool NoexceptMove>
class TrackedBuffer
{
    char* data_;
    std::size_t capacity_;
    std::string tag_;

public:
    explicit TrackedBuffer(std::size_t cap, std::string tag)
        : data_(new char[cap])
        , capacity_(cap)
        , tag_(std::move(tag))
    {
    }

    ~TrackedBuffer() { delete[] data_; }

    TrackedBuffer(const TrackedBuffer& other)
        : data_(new char[other.capacity_])
        , capacity_(other.capacity_)
        , tag_(other.tag_)
    {
        std::cout << "  [" << tag_ << "] 拷贝构造\n";
    }

    // The only difference: the noexcept marker
    TrackedBuffer(TrackedBuffer&& other) noexcept(NoexceptMove)
        : data_(other.data_)
        , capacity_(other.capacity_)
        , tag_(std::move(other.tag_))
    {
        other.data_ = nullptr;
        other.capacity_ = 0;
        std::cout << "  [" << tag_ << "] 移动构造\n";
    }

    TrackedBuffer& operator=(const TrackedBuffer&) = delete;
    TrackedBuffer& operator=(TrackedBuffer&&) = delete;
};

int main()
{
    using NB = TrackedBuffer<true>;   // The move constructor is marked noexcept
    using TB = TrackedBuffer<false>;  // The move constructor is not marked noexcept

    static_assert(std::is_nothrow_move_constructible_v<NB>,
                  "NB 的移动构造是 noexcept");
    static_assert(!std::is_nothrow_move_constructible_v<TB>,
                  "TB 的移动构造不是 noexcept");

    std::cout << "=== noexcept 移动 + vector 扩容 ===\n";
    {
        std::vector<NB> v;
        v.reserve(1);                       // Reserve one slot up front
        v.emplace_back(64, "Noexcept版");   // Occupy the only slot
        std::cout << "--- 触发扩容 ---\n";
        v.emplace_back(64, "Noexcept版");   // Exceeds capacity—must grow and relocate
    }

    std::cout << "\n=== 非 noexcept 移动 + vector 扩容 ===\n";
    {
        std::vector<TB> v;
        v.reserve(1);
        v.emplace_back(64, "Throwing版");
        std::cout << "--- 触发扩容 ---\n";
        v.emplace_back(64, "Throwing版");   // On growth the vector dares not move—falls back to copying
    }

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -O0 -Wall -o noexcept_vector_realloc noexcept_vector_realloc.cpp
./noexcept_vector_realloc
```

```text
=== noexcept 移动 + vector 扩容 ===
--- 触发扩容 ---
  [Noexcept版] 移动构造    <-- vector moves with confidence

=== 非 noexcept 移动 + vector 扩容 ===
--- 触发扩容 ---
  [Throwing版] 拷贝构造    <-- vector falls back to copying for exception safety
```

Compiled and run under GCC 16 with `-std=c++17 -O0`, the behavior matches expectations exactly.

## The Rule of Five

C++ has a classic "Rule of Three": if your class needs a user-defined destructor, copy constructor, or copy assignment operator, chances are it needs all three. C++11 added the move constructor and the move assignment operator to the list, turning it into the "Rule of Five".

If you declare only a destructor and no move operations, the compiler will **not** automatically generate a move constructor or move assignment operator—so what happens then? It falls back on the copy operations. This confuses newcomers all the time: you clearly wrote `std::move`, yet what actually gets called is still the copy constructor. `std::move` itself moves nothing—it is just a `static_cast` to an rvalue reference. What finally decides between move construction and copy construction is the class's definition. If the class has no move constructor, the rvalue reference matches the copy constructor's `const T&` perfectly.

```cpp
class OnlyDestructor {
    char* data_;

public:
    OnlyDestructor(std::size_t n) : data_(new char[n]) {}
    ~OnlyDestructor() { delete[] data_; }

    // No move constructor declared!
    // And the compiler won't implicitly generate one either (a user-defined destructor is present)
};

OnlyDestructor a(100);
OnlyDestructor b = std::move(a);  // Degrades to copy construction!
                                    // The implicit copy constructor shallow-copies -> double delete
```

The consequence here is worse than "inefficient"—because the implicitly generated copy constructor does a shallow copy (member-by-member pointer duplication), `a` and `b` end up with `data_` pointing at the same block of memory. When both are destroyed, `delete[]` runs twice and you get an outright double free. We can verify this behavior with type traits:

```cpp
static_assert(!std::is_trivially_move_constructible_v<OnlyDestructor>,
              "没有真正的移动构造函数");
static_assert(std::is_move_constructible_v<OnlyDestructor>,
              "但 is_move_constructible 为 true——退回到拷贝构造");
```

Looks contradictory? It isn't. `is_move_constructible` is true because the compiler can use the copy constructor to "satisfy" move construction (an rvalue binds to `const T&`), but that does not mean a real move constructor exists to do the pointer transfer. Here is the complete verification code:

```cpp
// rule_of_five_fallback.cpp -- with only a destructor, std::move degrades to copy construction
// Standard: C++17

#include <iostream>
#include <type_traits>
#include <utility>

// Only a destructor is defined; no copy/move operations are declared
class OnlyDestructor
{
    char* data_;

public:
    explicit OnlyDestructor(std::size_t n)
        : data_(new char[n])
    {
    }

    ~OnlyDestructor() { delete[] data_; }
    // Note: neither a move constructor nor a copy constructor is declared here
};

// Compile-time verification: it "can be move constructed", but not because a real move constructor exists
static_assert(!std::is_trivially_move_constructible_v<OnlyDestructor>,
              "没有真正的（平凡的）移动构造函数");
static_assert(std::is_move_constructible_v<OnlyDestructor>,
              "但 is_move_constructible 为 true —— 编译器退回到拷贝构造来满足");

int main()
{
    std::cout << "is_trivially_move_constructible_v: "
              << std::is_trivially_move_constructible_v<OnlyDestructor>
              << "  (没有真正的移动构造)\n";
    std::cout << "is_move_constructible_v:           "
              << std::is_move_constructible_v<OnlyDestructor>
              << "  (但能用拷贝构造蒙混过关)\n";

    // Really executing OnlyDestructor b = std::move(a): the implicit copy constructor shallow-copies,
    // a and b's data_ point at the same memory, and destroying both double frees.
    // We don't actually run it here (it would crash); the compile-time static_asserts already gave the verdict.
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -O0 -Wall -o rule_of_five_fallback rule_of_five_fallback.cpp
./rule_of_five_fallback
```

```text
is_trivially_move_constructible_v: 0  (没有真正的移动构造)
is_move_constructible_v:           1  (但能用拷贝构造蒙混过关)
```

Note that the `static_assert`s settle the question at compile time—the runtime printing is just a second confirmation. If you actually executed `OnlyDestructor b = std::move(a)`, the implicit copy constructor would shallow-copy, `a` and `b`'s `data_` would point at the same memory, and destruction would end in a double free.

For resource-managing classes, the safest policy is: **the five special member functions are either all user-defined or all `= default`**. If you manage resources through smart pointers, you can usually `= default` them and let the compiler generate the correct versions—exactly what modern C++ recommends. But for a class like ours that manages raw pointers by hand, you have to honestly write out all five:

```cpp
class Buffer {
    char* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    // 1. Constructor
    explicit Buffer(std::size_t capacity)
        : data_(new char[capacity])
        , size_(0)
        , capacity_(capacity)
    {
    }

    // 2. Destructor
    ~Buffer()
    {
        delete[] data_;
    }

    // 3. Copy constructor
    Buffer(const Buffer& other)
        : data_(new char[other.capacity_])
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        std::memcpy(data_, other.data_, size_);
    }

    // 4. Move constructor
    Buffer(Buffer&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    // 5. Copy assignment
    Buffer& operator=(const Buffer& other)
    {
        if (this != &other) {
            delete[] data_;
            data_ = new char[other.capacity_];
            size_ = other.size_;
            capacity_ = other.capacity_;
            std::memcpy(data_, other.data_, size_);
        }
        return *this;
    }

    // 6. Move assignment
    Buffer& operator=(Buffer&& other) noexcept
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
};
```

It looks a bit long, but the logic is all repetitive—copy operations do deep copies; move operations do pointer transfers plus nulling out the source.

## The copy-and-swap Idiom—Cutting Down Repetition

If writing four assignment operators (copy assignment + move assignment) feels too wordy, a classic idiom can simplify things for you. The core idea: **give copy assignment and move assignment one shared implementation**, letting pass-by-value semantics pick copy or move automatically.

```cpp
class Buffer {
    char* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    explicit Buffer(std::size_t capacity = 0)
        : data_(capacity ? new char[capacity] : nullptr)
        , size_(0)
        , capacity_(capacity)
    {
    }

    ~Buffer() { delete[] data_; }

    // Copy constructor
    Buffer(const Buffer& other)
        : data_(other.capacity_ ? new char[other.capacity_] : nullptr)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        if (data_) {
            std::memcpy(data_, other.data_, size_);
        }
    }

    // Move constructor
    Buffer(Buffer&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    // The unified assignment operator—pass-by-value picks copy or move automatically
    Buffer& operator=(Buffer other) noexcept
    {
        swap(*this, other);
        return *this;
    }

    friend void swap(Buffer& a, Buffer& b) noexcept
    {
        using std::swap;
        swap(a.data_, b.data_);
        swap(a.size_, b.size_);
        swap(a.capacity_, b.capacity_);
    }
};
```

Here `operator=(Buffer other)` takes its parameter by value—pass an lvalue in, and `other` is created via the copy constructor; pass an rvalue (say, `std::move(x)`), and `other` is created via the move constructor. Then `swap` exchanges the contents of `this` and `other`, and when the function ends, `other` is destroyed, automatically releasing the old resources.

The idiom's advantages: less code, exception safety, and self-assignment handled automatically. Its disadvantage is one extra swap, which can cost a tiny bit in extreme-performance scenarios. If you actually compare the assembly at `-O2`, the two paths have nearly identical instruction counts—the copy-and-swap `operator=` body reduces to just the swap (about 13 `movq` instructions, one exchange per member for the three members), with the `delete` deferred to the parameter's destruction; the standalone move-assignment `operator=` has to `delete[]` its old resource itself and do the self-assignment check, and the compiler will even conveniently use a single SSE `movdqu` to merge the two `size_t`s into one 16-byte move. Totaled up, the instruction counts come out nearly even—what copy-and-swap adds is a few extra memory reads and writes from the swap, not extra instructions. For a class managing dynamic memory, the cost of `new`/`delete` dwarfs this little bit of register traffic, so copy-and-swap's extra cost is practically unmeasurable in practice.

## A More General Example—Moving File Handles

Beyond dynamic memory, move semantics is just as powerful on classes that manage other resources. File handles are a typical example—the operating system limits how many times the same file can be open simultaneously, and carelessly copying an object that holds a file handle can lead to leaked handles or double closes.

```cpp
#include <cstdio>
#include <utility>
#include <iostream>

class FileHandle {
    std::FILE* file_;
    std::string path_;

public:
    explicit FileHandle(const char* path, const char* mode)
        : file_(std::fopen(path, mode))
        , path_(path)
    {
        if (!file_) {
            throw std::runtime_error("Failed to open file: " + path_);
        }
    }

    ~FileHandle()
    {
        if (file_) {
            std::fclose(file_);
            std::cout << "  关闭文件: " << path_ << "\n";
        }
    }

    // Copying forbidden—a file handle cannot be shared
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    // Moving allowed—a file handle can have its ownership transferred
    FileHandle(FileHandle&& other) noexcept
        : file_(other.file_)
        , path_(std::move(other.path_))
    {
        other.file_ = nullptr;  // Prevent other's destructor from closing the file
    }

    FileHandle& operator=(FileHandle&& other) noexcept
    {
        if (this != &other) {
            if (file_) {
                std::fclose(file_);  // Close the currently held file
            }
            file_ = other.file_;
            path_ = std::move(other.path_);
            other.file_ = nullptr;
        }
        return *this;
    }

    std::FILE* get() const { return file_; }
    const std::string& path() const { return path_; }
};

/// @brief Factory function: opens the log file
FileHandle open_log(const std::string& name)
{
    return FileHandle(name.c_str(), "a");
}

int main()
{
    auto log = open_log("app.log");
    std::fprintf(log.get(), "Application started\n");

    // Transfer ownership of the log file to another variable
    FileHandle moved_log = std::move(log);
    std::fprintf(moved_log.get(), "Log handle moved\n");

    // log.get() now returns nullptr—don't use it anymore
    return 0;
}
```

This example demonstrates a common design pattern: **non-copyable but movable**. A file handle is physically unique; there should not be a second "copy" of it—copying would leave two objects both trying to close the same file. But moving is reasonable: `open_log` creates the file handle, then hands ownership over to the caller, and the temporary inside the function no longer holds any resource.

Run this program and you will see:

```text
  关闭文件: app.log
```

Note that the file-closing message is printed only once—even though both `log` and `moved_log` go through destruction, `log`'s `file_` was nulled by the move, so the `if (file_)` check inside its destructor fails, and no double close happens.

## Hands-On Experiment—move_semantics_demo.cpp

Let's write a complete program to verify all the key behaviors of move semantics.

```cpp
// move_semantics_demo.cpp -- a demonstration of move construction and move assignment
// Standard: C++17

#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

class Buffer
{
    char* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    explicit Buffer(std::size_t capacity)
        : data_(new char[capacity])
        , size_(0)
        , capacity_(capacity)
    {
        std::cout << "  [Buffer] 分配 " << capacity << " 字节\n";
    }

    ~Buffer()
    {
        if (data_) {
            std::cout << "  [Buffer] 释放 " << capacity_ << " 字节\n";
            delete[] data_;
        }
    }

    Buffer(const Buffer& other)
        : data_(new char[other.capacity_])
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        std::memcpy(data_, other.data_, size_);
        std::cout << "  [Buffer] 拷贝构造 " << capacity_ << " 字节\n";
    }

    Buffer(Buffer&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        std::cout << "  [Buffer] 移动构造（指针转移）\n";
    }

    Buffer& operator=(const Buffer& other)
    {
        if (this != &other) {
            delete[] data_;
            data_ = new char[other.capacity_];
            size_ = other.size_;
            capacity_ = other.capacity_;
            std::memcpy(data_, other.data_, size_);
            std::cout << "  [Buffer] 拷贝赋值 " << capacity_ << " 字节\n";
        }
        return *this;
    }

    Buffer& operator=(Buffer&& other) noexcept
    {
        if (this != &other) {
            delete[] data_;
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
            std::cout << "  [Buffer] 移动赋值（指针转移）\n";
        }
        return *this;
    }

    void append(const char* str, std::size_t len)
    {
        if (size_ + len <= capacity_) {
            std::memcpy(data_ + size_, str, len);
            size_ += len;
        }
    }

    std::size_t size() const { return size_; }
    std::size_t capacity() const { return capacity_; }
};

int main()
{
    std::cout << "=== 1. 创建两个缓冲区 ===\n";
    Buffer a(1024);
    a.append("Hello", 5);
    Buffer b(2048);
    b.append("World", 5);
    std::cout << '\n';

    std::cout << "=== 2. 拷贝构造 ===\n";
    Buffer c = a;
    std::cout << "  c.size() = " << c.size() << "\n\n";

    std::cout << "=== 3. 移动构造 ===\n";
    Buffer d = std::move(b);
    std::cout << "  d.size() = " << d.size() << "\n";
    std::cout << "  b.capacity() = " << b.capacity() << "\n\n";

    std::cout << "=== 4. 移动赋值 ===\n";
    a = std::move(d);
    std::cout << "  a.size() = " << a.size() << "\n";
    std::cout << "  d.capacity() = " << d.capacity() << "\n\n";

    std::cout << "=== 5. vector 中的移动 ===\n";
    std::vector<Buffer> buffers;
    buffers.reserve(4);
    std::cout << "  push_back 左值:\n";
    buffers.push_back(c);             // Copy
    std::cout << "  push_back std::move:\n";
    buffers.push_back(std::move(c));  // Move
    std::cout << "  emplace_back 原位构造:\n";
    buffers.emplace_back(512);        // Constructed directly inside the vector
    std::cout << '\n';

    std::cout << "=== 6. 程序结束 ===\n";
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o move_demo move_semantics_demo.cpp
./move_demo
```

Expected output:

```text
=== 1. 创建两个缓冲区 ===
  [Buffer] 分配 1024 字节
  [Buffer] 分配 2048 字节

=== 2. 拷贝构造 ===
  [Buffer] 拷贝构造 1024 字节
  c.size() = 5

=== 3. 移动构造 ===
  [Buffer] 移动构造（指针转移）
  d.size() = 5
  b.capacity() = 0

=== 4. 移动赋值 ===
  [Buffer] 移动赋值（指针转移）
  a.size() = 5
  d.capacity() = 0

=== 5. vector 中的移动 ===
  push_back 左值:
  [Buffer] 拷贝构造 1024 字节
  push_back std::move:
  [Buffer] 移动构造（指针转移）
  emplace_back 原位构造:
  [Buffer] 分配 512 字节

=== 6. 程序结束 ===
  [Buffer] 释放 1024 字节
  [Buffer] 释放 1024 字节
  [Buffer] 释放 512 字节
  [Buffer] 释放 2048 字节
```

Steps 2 and 3 have been turned into an animation of the memory-level actions—you can play it, pause it, or use the step buttons to single-step through and see the pointer handoff clearly:

<Anim id="copy-vs-move" />

The contrast in the output between the move-construction (pointer-transfer) line and the copy-construction-of-N-bytes line is plain at a glance—copying means allocating memory and replicating data; moving is just three pointer assignments. Step 5's vector operations deserve even more attention: `push_back` with an lvalue copies, `push_back` with a `std::move`d rvalue moves, and `emplace_back` constructs in place directly in the vector's memory, skipping even the move. In large-data scenarios the performance differences among these three operations become very noticeable.

Notice there is no "released 0 bytes" line at destruction time—those would be the moved-from objects: their `data_` is `nullptr`, so the `if (data_)` check in the destructor skips the `delete[]`. The three elements in the vector are destroyed independently—the first is the copy of `c` (1024 bytes), the second is the one moved out of `c` (1024 bytes), and the third is the one `emplace_back` constructed in place (512 bytes).

## Run It Online

Run the Buffer move-semantics example online and compare the resource cost of copying versus moving:

<OnlineCompilerDemo
  title="Move Construction and Move Assignment: Buffer Resource Transfer"
  source-path="code/examples/vol2/02_move_semantics.cpp"
  description="Run online and compare Buffer's copy construction vs move construction, and how they behave differently inside a vector."
  allow-run
  allow-x86-asm
/>

In the next article we'll look at the big chunk the compiler quietly saves us behind the scenes—return value optimization (RVO and NRVO), which can drive the cost of returning a large object all the way to zero.
