---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: The interlocking rules of the Rule of Five, the copy-and-swap idiom, and
  designing types that can move but refuse to copy
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 0: Move Construction and Move Assignment'
reading_time_minutes: 10
related:
- 'RVO and NRVO: The Compiler''s Return Value Optimization'
- 'Move Semantics in Practice: Standard Library Containers and Performance Benchmarks'
title: 'The Rule of Five: How the Special Member Functions Fit Together'
tags:
- host
- cpp-modern
- intermediate
- 移动语义
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/03-rule-of-five.md
  source_hash: 5af5132d6ce12d434c091c0b7e8171f94f978e5756a7a0c46622631740ff649e
  translated_at: '2026-09-27T04:30:40+00:00'
  engine: anthropic
  token_count: 6500
---
# The Rule of Five: How the Special Member Functions Fit Together

In the previous article we finished writing `Buffer`'s move constructor and move assignment. But a resource-managing class has more special member functions than those two. We have the **destructor, copy construction, copy assignment, and the move operations**. They form one interlocking mechanism: touch any one of them, and the rest have to be brought in line. In this article, we're going to take them out for a proper spin!

## From the Rule of Three to the Rule of Five

An old saying in C++ is the "Rule of Three": if a class user-defines **any** of the destructor, copy constructor, or copy assignment operator, chances are it needs all three. C++11 added the move constructor and move assignment operator to the list, and the roster became the "Rule of Five" <RefLink :id="1" preview="cppreference The rule of three / five / zero" />.

If you declare only a destructor and not a single move operation, the compiler will **not** automatically generate a move constructor or move assignment operator for you. With no move constructor around, who is the rvalue that `std::move` hands out supposed to match? This is an easy place to get confused: you clearly wrote `std::move`, yet what actually gets called is still the copy constructor. `std::move` itself moves nothing—it is just a `static_cast` to an rvalue reference. What finally decides between move construction and copy construction is the class's definition. If the class has no move constructor, the rvalue reference matches the copy constructor's `const T&` perfectly.

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

The consequence here is worse than plain inefficiency. The implicitly generated copy constructor does a shallow copy—replicating the pointers member by member—so `a` and `b`'s `data_` end up pointing at the same block of memory. When both are destroyed, `delete[]` runs twice, and you get an outright double free: the same memory released twice. We can verify this behavior with type traits (compile-time tools for probing a type's properties) <RefLink :id="2" preview="cppreference std::is_move_constructible — true via copy ctor fallback even without a real move ctor" />:

```cpp
static_assert(!std::is_trivially_move_constructible_v<OnlyDestructor>,
              "没有真正的移动构造函数");
static_assert(std::is_move_constructible_v<OnlyDestructor>,
              "但 is_move_constructible 为 true——退回到拷贝构造");
```

Looks contradictory? Break it apart and it isn't. `is_move_constructible`'s true has a reason behind it: the compiler can use the copy constructor to "satisfy" move construction—rvalues are allowed to bind to `const T&` in the first place. But that doesn't mean a real move constructor exists to do the pointer transfer. Here is the complete verification code:

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

The verification program is ready and waiting too—hit "Try it yourself" below and you can run it directly:

<OnlineCompilerDemo
  title="Hands-On Verification: rule_of_five_fallback.cpp"
  source-path="code/examples/vol2/rule_of_five_fallback.cpp"
  description="Verify online: a class with only a destructor has is_move_constructible true but is_trivially_move_constructible 0—it has no real move constructor."
  run-options="-O0 -std=c++17"
  allow-run
/>

Notice that the `static_assert`s settle the answer at compile time—the two runtime print lines are just a second confirmation. If you actually executed `OnlyDestructor b = std::move(a)`, what awaits you is the double free described earlier.

For a resource-managing class, the safest policy is: **the five special member functions are either all user-defined or all `= default`** <RefLink :id="3" preview="C++ Core Guidelines C.21 — if you define or delete any copy, move, or destructor function, define or delete them all" />. If you manage resources with smart pointers, `= default`-ing everything and letting the compiler generate the correct versions is usually enough—exactly what modern C++ recommends. But for a class like ours that manages raw pointers by hand, you have to honestly write out all five. The example below numbers the leading ordinary constructor too, so the five real special members land on numbers 2 through 6:

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

It does look a bit long as a whole, but really we are writing the same pattern over and over: the copy operations do a deep copy, while the move operations do a pointer transfer plus one extra step that nulls out the source object.

## The copy-and-swap Idiom—Cutting Down on Duplicate Code

If writing copy assignment and move assignment each as its own function feels too verbose, there is a classic idiom that folds the two into one: **give copy assignment and move assignment a shared implementation**, and let pass-by-value semantics pick copy or move automatically <RefLink :id="1" preview="cppreference The rule of three / five — copy-and-swap idiom example" />.

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

    // Unified assignment operator—pass-by-value picks copy or move automatically
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

What we want to look at is how the parameter is received: `operator=(Buffer other)` takes it by value. Pass in an lvalue, and `other` is created with the copy constructor. Pass in an rvalue (say, `std::move(x)`), and `other` gets created with the move constructor instead. Then `swap` exchanges the contents of `this` and `other`; when the function ends, `other` is destroyed, and the old resources get released along with it.

Looking back at this idiom, its advantages are very real: less code, exception safety, and self-assignment handled automatically. The cost is one extra `swap`, which might have a tiny impact in extreme-performance scenarios. If you actually compare the assembly at `-O2`, you'll find the two paths nearly tie on instruction count—what copy-and-swap adds is a few extra memory reads and writes from the `swap`, not more instructions. For a class managing dynamic memory, the cost of `new`/`delete` dwarfs this little register shuffling, so copy-and-swap's extra cost is practically unmeasurable in real programs.

> Here are the specifics of our side-by-side comparison: the copy-and-swap `operator=` body is down to just `swap`—about 13 `movq` instructions, one exchange per member for the three members, with `delete` deferred until the parameter's destruction. The standalone move-assignment `operator=`, on the other hand, has to `delete[]` the old resources itself and perform the self-assignment check, and the compiler also uses one SSE `movdqu` to merge the two `size_t`s into 16 bytes and move them in a single shot.

## A General Example—Moving a File Handle

Move semantics can govern more than dynamic memory—ownership of other resources can change hands just the same. Take the most classic example: file handles. The operating system limits how many times the same file can be open, and if you accidentally copy an object holding a file handle, you can wind up with a handle leak or a double-close incident.

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

    // Copying is forbidden—a file handle cannot be shared
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    // Moving is allowed—a file handle can transfer ownership
    FileHandle(FileHandle&& other) noexcept
        : file_(other.file_)
        , path_(std::move(other.path_))
    {
        other.file_ = nullptr;  // Prevent 'other' from closing the file when destroyed
    }

    FileHandle& operator=(FileHandle&& other) noexcept
    {
        if (this != &other) {
            if (file_) {
                std::fclose(file_);  // Close the current file
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

/// @brief Factory function: opens a log file
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

    // log.get() now returns nullptr; don't use it anymore
    return 0;
}
```

This example shows a common design orientation: **non-copyable, but movable**. There is physically only one file handle, so "copying" a second one is simply wrong—if you really copied it, you would be holding two objects racing to close the same file. Moving, on the other hand, is perfectly legitimate: `open_log` creates the file handle inside the function, then hands ownership over to you, and the temporary inside the function no longer holds any resource.

The example is embedded below as well—click "Try it yourself" to run it once, and keep an eye on the destructor output at the end:

<OnlineCompilerDemo
  title="Hands-On Verification: file_handle_move.cpp"
  source-path="code/examples/vol2/16_file_handle_move.cpp"
  description="Verify online how a file handle moves. Run it, watch the destructor output, and count how many times 关闭文件 is printed."
  run-options="-O0 -std=c++17"
  allow-run
/>

In the output, `关闭文件` appears only once—let's think about why. Both `log` and `moved_log` clearly went through destruction. The reason is simple: after the move, `log`'s `file_` was nulled out, so the `if (file_)` check in its destructor fails, and it naturally never tries to close the file a second time.

In the next article, we'll look at the biggest chunk the compiler saves for us behind the scenes: return value optimization (RVO) and named return value optimization (NRVO). They can drive the cost of returning a large object from a function all the way down to zero.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="The Rule of Three / Five / Zero"
    chapter="Includes the copy-and-swap idiom"
    url="https://en.cppreference.com/w/cpp/language/rule_of_three"
  />
  <ReferenceItem
    :id="2"
    author="cppreference.com"
    title="std::is_move_constructible"
    url="https://en.cppreference.com/w/cpp/types/is_move_constructible"
  />
  <ReferenceItem
    :id="3"
    author="Bjarne Stroustrup / Herb Sutter (eds.)"
    title="C++ Core Guidelines — C.21: If You Define or Delete Any Copy, Move, or Destructor Function, Define or Delete Them All"
    publisher="isocpp.org"
    url="https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#rc-five"
  />
</ReferenceCard>
