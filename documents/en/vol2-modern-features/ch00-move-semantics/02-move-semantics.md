---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: Master the core mechanisms of move semantics to implement zero-copy resource
  transfer
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 0: 右值引用'
reading_time_minutes: 19
related:
- RVO 与 NRVO
- 完美转发
tags:
- host
- cpp-modern
- intermediate
- 移动语义
title: Move Construction and Move Assignment
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/02-move-semantics.md
  source_hash: 2fd26cd9e10b01ed7661cc2dadb4e706657d417e541c9e788ad0f96acaf956eb
  translated_at: '2026-06-16T03:54:35.931166+00:00'
  engine: anthropic
  token_count: 4408
---
# Move Construction and Move Assignment

In the previous post, we laid the groundwork for value categories and rvalue references. Now it's time for the real work—making our classes truly "move" instead of "copy". Honestly, I made quite a few mistakes when I first wrote move constructors by hand: forgetting to null out the source object's pointer, forgetting to handle self-assignment, and not being sure when to add `noexcept`. This article shares the pitfalls I've encountered to help you avoid these detours.

We will start with a simple but realistic scenario: implementing a dynamic buffer class ourselves, and using it to understand move constructors, move assignment, and the so-called "Rule of Five" step by step.

## Why We Need Move—Starting with the Cost of Copying

Suppose you are writing a text processing tool that needs to pass large chunks of text data between functions frequently. Let's look at a naive dynamic buffer implementation first:

```cpp
class Buffer {
public:
    Buffer() : data_(nullptr), size_(0), capacity_(0) {}

    explicit Buffer(size_t size) : data_(new char[size]), size_(size), capacity_(size) {}

    ~Buffer() {
        delete[] data_;
    }

    // Copy constructor - deep copy
    Buffer(const Buffer& other)
        : data_(new char[other.size_]), size_(other.size_), capacity_(other.capacity_) {
        std::copy(other.data_, other.data_ + size_, data_);
    }

    // Copy assignment - deep copy
    Buffer& operator=(const Buffer& other) {
        if (this != &other) {
            delete[] data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            data_ = new char[size_];
            std::copy(other.data_, other.data_ + size_, data_);
        }
        return *this;
    }

private:
    char* data_;
    size_t size_;
    size_t capacity_;
};
```

Now let's do an experiment: create a 1MB buffer and pass it into a function.

```cpp
void process(Buffer buf) {
    // Do something with buf
}

int main() {
    Buffer buf(1024 * 1024); // 1MB buffer
    process(buf);
}
```

What happens when `process` is called? The parameter `buf` is passed by value, so the compiler calls the copy constructor of `Buffer` to create `buf`—this means allocating 1MB of new memory and copying the data from `buf` byte by byte. When the function returns, `buf` triggers another copy constructor to create the return value. Including the destruction of `buf` at the end of the function—the whole process performs **two 1MB memory allocations, two 1MB memory copies, and one 1MB memory deallocation**. But what we actually need is just to transfer the data from `buf` in `main` to `buf` in `process`. (I estimate old C++ hands would be blushing seeing this, and I believe you won't be able to hold it back either.)

This is the fundamental problem with copy semantics: when you no longer need the source object, the copy constructor still faithfully copies every byte, and then the source object dutifully releases that block of memory when it destructs. Resources are allocated and then released, data is copied and then discarded—pure waste.

## Move Constructor—Transfer of Resource Ownership

The core idea of the move constructor is very simple: don't copy data, just transfer ownership of resources. For classes that manage dynamic memory, this means "stealing" the pointer from the source object and then nulling out the source object's pointer to prevent it from freeing that memory when it destructs.

```cpp
// Move constructor
Buffer(Buffer&& other) noexcept
    : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}
```

Let's look at this move constructor line by line. The signature `Buffer(Buffer&& other)` indicates that this is a move constructor—it only accepts rvalue arguments. In the function body, we do three things: copy the three members of `other` directly to `this` (three pointer/integer assignments, very low cost), and then set the source object's pointer to null. This last step is crucial—if we don't set `other.data_` to null, when `other` destructs, its destructor will free the memory we just transferred, and `this` will hold a dangling pointer, leading to a guaranteed crash on subsequent access.

Now we use `std::move` to trigger the move constructor:

```cpp
int main() {
    Buffer buf(1024 * 1024);
    process(std::move(buf)); // Trigger move constructor
}
```

What happens in the whole process? Three pointer/integer assignments—done. No `new`, no `memcpy`, no `delete`. It turns an O(n) copy operation into an O(1) pointer transfer. For a 1MB buffer, this is the difference between "allocate 1MB memory plus copy 1MB data" and "assign three registers".

## Move Assignment Operator—One More Step Than Move Construction

The move assignment operator is slightly more complex than the move constructor because the target object of the assignment may already hold resources—we must release the old resources before taking over the new ones.

```cpp
// Move assignment operator
Buffer& operator=(Buffer&& other) noexcept {
    if (this != &other) { // Self-assignment check
        delete[] data_;   // Release old resources

        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;

        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    return *this;
}
```

Note the first step `delete[] data_`—this is the key difference between move assignment and move construction. During move construction, the target object is not yet initialized, so there are no old resources to release; during move assignment, the target object already exists, and if we don't release the old resources first, we will leak memory. The self-assignment check `if (this != &other)` is also necessary—although code like `buf = std::move(buf)` rarely appears in normal development, generic implementations of standard library components (like `std::vector`) might produce equivalent operations, so adding this safeguard is a responsible practice.

Let's look at the effect of move assignment in actual code:

```cpp
int main() {
    Buffer buf1(1024);
    Buffer buf2(2048);

    buf2 = std::move(buf1); // Move assignment
    // buf1 is now in a "valid but unspecified" state
    // buf2 owns the 1024-byte buffer
}
```

> ⚠️ **Pitfall Warning**: The source object after a move is in a "valid but unspecified" state. This means you can safely assign a new value to it or let it destruct, but you shouldn't read its value—for example, `buf1.size()` might return 0, or it might return the original value, depending on the specific implementation. My advice is: let the source object leave scope immediately after moving, or assign it a clear new value; never let a "moved" object wander around in your code.

## noexcept—The Safety Promise of Move Operations

You may have noticed that both move operations are marked with `noexcept`. This is not optional decoration—it has real performance implications.

The reason lies in the expansion behavior of `std::vector`. When `std::vector` needs to grow its capacity, it must transfer existing elements to a new memory block. If the element's move constructor is `noexcept`, `std::vector` will confidently use move; if the move constructor might throw an exception, `std::vector` will fall back to using the copy constructor—because if an exception is thrown during a move, the half-moved state is hard to recover, but if an exception is thrown during a copy, the original data is still intact.

```cpp
// If move constructor is noexcept, vector uses move
// If move constructor is not noexcept, vector uses copy
std::vector<Buffer> vec;
vec.push_back(Buffer(1024)); // May trigger reallocation
```

You can use `std::is_nothrow_move_constructible` to verify if your class truly satisfies `noexcept` move:

```cpp
static_assert(std::is_nothrow_move_constructible_v<Buffer>,
              "Buffer should be noexcept move constructible");
```

This isn't just theory on paper—we can write an experiment to verify the actual behavior of `std::vector`. Prepare two `Buffer` classes with identical structure, the only difference being whether the move constructor has `noexcept`, and then let `std::vector` expand. The results are very clear:

```text
With noexcept move constructor:
  Reallocation triggered: using move constructor (fast)

Without noexcept move constructor:
  Reallocation triggered: using copy constructor (slow)
```

Compiled and run with GCC 15, `-O2`, the behavior matches expectations perfectly. Full code see `noexcept_demo.cpp`.

## Rule of Five

C++ has a classic "Rule of Three": if your class needs a custom destructor, copy constructor, or copy assignment operator, it likely needs all three. C++11 adds move constructor and move assignment operator, making it the "Rule of Five".

If you only declare a destructor but do not declare move operations, the compiler will **not** automatically generate move constructor and move assignment operator. So what happens? It will fall back to using copy operations. This often confuses beginners: clearly `std::move` was used, but the copy constructor is actually called. `std::move` itself doesn't move anything—it's just a type cast from an lvalue reference to an rvalue reference. The ultimate decision to call the move constructor or the copy constructor lies in the class definition. If the class doesn't have a move constructor, the rvalue reference will perfectly match the copy constructor that takes `const Buffer&`.

```cpp
class Buffer {
public:
    ~Buffer(); // Destructor declared
    // No move constructor declared

    // Compiler will NOT generate move constructor
    // std::move(buf) will match the copy constructor
};
```

The consequence here is more serious than "inefficiency"—because the implicitly generated copy constructor does a shallow copy (copying pointers member by member), `buf1` and `buf2`'s `data_` will point to the same memory block. When both destruct, `delete[]` is called twice, directly triggering a double free. We can use type traits to verify this behavior:

```cpp
class Buffer {
public:
    ~Buffer() {}
    // No move/copy declarations
};

static_assert(std::is_move_constructible_v<Buffer>, "Move constructible?");
// But there is no real move constructor!
```

Seems contradictory? Not really. `is_move_constructible` being true is because the compiler can use the copy constructor to "satisfy" the move constructor requirement (rvalues can bind to `const Buffer&`), but this doesn't mean there exists a real move constructor to do pointer transfer. Complete verification code is in `rule_of_five_demo.cpp`.

For classes that manage resources, the safest approach is to **either fully customize all five special member functions, or fully default them**. If you use smart pointers to manage resources, you can usually use `= default` to let the compiler generate the correct version—this is exactly what modern C++ recommends. But for classes like ours that manually manage raw pointers, we must honestly write all five:

```cpp
class Buffer {
public:
    // 1. Destructor
    ~Buffer() { delete[] data_; }

    // 2. Copy constructor
    Buffer(const Buffer& other);

    // 3. Move constructor
    Buffer(Buffer&& other) noexcept;

    // 4. Copy assignment
    Buffer& operator=(const Buffer& other);

    // 5. Move assignment
    Buffer& operator=(Buffer&& other) noexcept;
};
```

It looks a bit long, but the logic is repetitive—copy operations do deep copies, move operations do pointer transfers plus source object nulling.

## copy-and-swap Idiom—Reduce Code Duplication

If you think writing four assignment operators (copy + move) is too verbose, there's a classic idiom that can help you simplify. The core idea is: **let copy assignment and move assignment share a single implementation**, leveraging value-passing semantics to automatically choose between copy or move.

```cpp
class Buffer {
public:
    // Unified assignment operator (takes value)
    Buffer& operator=(Buffer other) noexcept {
        swap(*this, other);
        return *this;
    }

    friend void swap(Buffer& a, Buffer& b) noexcept {
        using std::swap;
        swap(a.data_, b.data_);
        swap(a.size_, b.size_);
        swap(a.capacity_, b.capacity_);
    }
};
```

Here `operator=` receives the parameter by value—if you pass an lvalue in, `other` is created via the copy constructor; if you pass an rvalue (like `std::move(buf)`), `other` is created via the move constructor. Then `swap` swaps the contents of `*this` and `other`, and when the function ends, `other` destructs, automatically releasing the old resources.

The advantage of this idiom is less code, exception safety, and automatic handling of self-assignment. The disadvantage is an extra `swap` operation (three pointer swaps), which might have a tiny impact in extreme performance scenarios. However, in the vast majority of scenarios, this overhead is completely negligible—comparing assembly with GCC 15 at `-O2` reveals that the move assignment path of copy-and-swap adds about three register move instructions (the cost of `swap`) compared to the standalone move assignment operator, but there are no extra function calls or memory operations. For classes managing dynamic memory, the overhead of `new`/`delete` far outweighs these three register instructions, so the extra cost of copy-and-swap is practically immeasurable in reality.

## General Example—Moving File Handles

Besides dynamic memory, move semantics is equally powerful for classes managing other resources. File handles are a typical example—the operating system limits the number of open files; if you accidentally copy an object holding a file handle, it can lead to handle leaks or duplicate closes.

```cpp
class FileHandle {
public:
    explicit FileHandle(const char* filename) {
        fd_ = open(filename, O_RDONLY);
    }

    // Delete copy operations
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    // Move constructor
    FileHandle(FileHandle&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    // Move assignment
    FileHandle& operator=(FileHandle&& other) noexcept {
        if (this != &other) {
            close(fd_);
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    ~FileHandle() {
        if (fd_ != -1) {
            close(fd_);
            std::cout << "File closed\n";
        }
    }

private:
    int fd_;
};
```

This example demonstrates a common design pattern: **non-copyable but movable**. A file handle physically exists only once and shouldn't be "copied" to a second copy—copying would lead to both objects trying to close the same file. But moving is reasonable: `openFile` creates a file handle, then transfers ownership to the caller, and the temporary object inside the function no longer holds any resources.

Running this program, you will see:

```text
File opened
File closed
```

Note there is only one "File closed" output—although both `handle` and the temporary object in `openFile` go through destruction, the temporary object's `fd_` was set to `-1` after the move, so the `if` check in its destructor fails, preventing a duplicate close.

## Hands-on Experiment—move_semantics_demo.cpp

Let's write a complete program to verify all key behaviors of move semantics.

```cpp
#include <iostream>
#include <vector>
#include <string>
#include <utility>
#include <algorithm>

class Buffer {
public:
    Buffer() : data_(nullptr), size_(0), capacity_(0) {
        std::cout << "默认构造\n";
    }

    explicit Buffer(size_t size)
        : data_(new char[size]), size_(size), capacity_(size) {
        std::cout << "构造 " << size << " 字节缓冲区\n";
    }

    ~Buffer() {
        if (data_) {
            std::cout << "释放 " << size_ << " 字节\n";
            delete[] data_;
        }
    }

    // Copy constructor
    Buffer(const Buffer& other)
        : data_(new char[other.size_]), size_(other.size_), capacity_(other.capacity_) {
        std::copy(other.data_, other.data_ + size_, data_);
        std::cout << "拷贝构造 " << size_ << " 字节\n";
    }

    // Move constructor
    Buffer(Buffer&& other) noexcept
        : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        std::cout << "移动构造（指针转移）\n";
    }

    // Copy assignment
    Buffer& operator=(const Buffer& other) {
        if (this != &other) {
            delete[] data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            data_ = new char[size_];
            std::copy(other.data_, other.data_ + size_, data_);
            std::cout << "拷贝赋值 " << size_ << " 字节\n";
        }
        return *this;
    }

    // Move assignment
    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            delete[] data_;
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
            std::cout << "移动赋值（指针转移）\n";
        }
        return *this;
    }

    size_t size() const { return size_; }

private:
    char* data_;
    size_t size_;
    size_t capacity_;
};

int main() {
    std::cout << "=== 1. 构造 ===\n";
    Buffer buf1(1024);

    std::cout << "\n=== 2. 拷贝构造 ===\n";
    Buffer buf2 = buf1;

    std::cout << "\n=== 3. 移动构造 ===\n";
    Buffer buf3 = std::move(buf1);

    std::cout << "\n=== 4. 移动赋值 ===\n";
    Buffer buf4(512);
    buf4 = std::move(buf2);

    std::cout << "\n=== 5. Vector 操作 ===\n";
    std::vector<Buffer> vec;
    vec.reserve(3);

    std::cout << "5.1 传入左值（拷贝）:\n";
    vec.push_back(buf3);

    std::cout << "5.2 传入右值（移动）:\n";
    vec.push_back(std::move(buf4));

    std::cout << "5.3 原位构造（无移动）:\n";
    vec.emplace_back(2048);

    std::cout << "\n=== 6. 析构 ===\n";
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++23 -O2 -o move_demo move_semantics_demo.cpp
./move_demo
```

Expected output:

```text
=== 1. 构造 ===
构造 1024 字节缓冲区

=== 2. 拷贝构造 ===
拷贝构造 1024 字节

=== 3. 移动构造 ===
移动构造（指针转移）

=== 4. 移动赋值 ===
构造 512 字节缓冲区
释放 512 字节
移动赋值（指针转移）

=== 5. Vector 操作 ===
5.1 传入左值（拷贝）:
拷贝构造 1024 字节

5.2 传入右值（移动）:
移动构造（指针转移）

5.3 原位构造（无移动）:
构造 2048 字节缓冲区

=== 6. 析构 ===
释放 2048 字节
释放 1024 字节
释放 1024 字节
```

The contrast between "Move constructor (pointer transfer)" and "Copy constructor X bytes" in the output is clear at a glance—copying requires allocating memory plus copying data, while moving is just three pointer assignments. Step 5's vector operations are even more noteworthy: passing an lvalue triggers a copy, passing an rvalue from `std::move` triggers a move, and `emplace_back` constructs directly in the vector's memory, saving even the move. The performance difference between these three operations will be very significant in large data scenarios.

Note that there is no "release 0 bytes" output during destruction—those are the objects that have been moved, their `data_` is `nullptr`, so the `if` check in the destructor skips `delete`. The three elements in the vector destruct independently—the first is a copy of `buf3` (1024 bytes), the second was moved from `buf4` (1024 bytes), and the third was constructed in-place by `emplace_back` (2048 bytes).

## Run Online

Run the Buffer move semantics example online and compare the resource overhead of copying vs. moving:

<OnlineCompilerDemo
  title="Move Construction and Move Assignment: Buffer Resource Transfer"
  source-path="code/examples/vol2/02_move_semantics.cpp"
  description="Run online and compare Buffer's copy constructor vs. move constructor, and their behavior differences in vector."
  allow-run
  allow-x86-asm
/>

## Summary

In this post, we broke down move constructors and move assignment operators from start to finish. The core of move operations is **resource ownership transfer**—don't copy data, just steal the pointer, and then null the source object. Move assignment has one more step than move construction: you must release the old resources held by the target object first. All move operations should be marked `noexcept`, which directly affects the behavior of containers like `std::vector` during reallocation. If your class manages resources, remember the Rule of Five: destructor, copy constructor, move constructor, copy assignment, move assignment—either write all five, or `= default` all five.

In the next post, we will look at another major thing the compiler does for us behind the scenes—Return Value Optimization (RVO and NRVO), which can make the cost of returning large objects from functions drop to zero.
