# Move Construction and Move Assignment

In the previous article, we laid the groundwork for value categories and rvalue references. Now it is time to get to the real work—teaching our classes to truly "move" instead of "copy." To be honest, I made quite a few mistakes the first time I wrote a move constructor by hand: forgetting to null out the source object's pointer, forgetting to handle self-assignment, and being unclear on when to add `noexcept`... This article shares all the pitfalls I stumbled into, hoping to save you some headaches.

We will start with a simple but realistic scenario: implementing our own dynamic buffer class, and using it to step-by-step understand move construction, move assignment, and the so-called "Rule of Five."

## Why We Need Move Semantics—Starting with the Cost of Copying

Suppose you are writing a text processing tool that needs to frequently pass large blocks of text data between functions. Let us first look at a very basic dynamic buffer implementation:

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

    // 拷贝构造：深拷贝
    Buffer(const Buffer& other)
        : data_(new char[other.capacity_])
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        std::memcpy(data_, other.data_, size_); // 直接平凡的拷贝数据
    }

    // 拷贝赋值：深拷贝
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

Now let us run an experiment: create a 1MB buffer, and then pass it into a function.

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

    Buffer result = process_buffer(large);  // 拷贝！
    return 0;
}
```

What happens when we call `processBuffer`? The parameter `buf` is passed by value, so the compiler calls `DynamicBuffer`'s copy constructor to create `buf`—this means allocating 1MB of new memory, and then copying the data from `data` over byte by byte. When the function returns, the return statement triggers another copy constructor to create the temporary. Add in the destruction of `buf` when the function ends—the entire process results in **two 1MB memory allocations, two 1MB memory copies, and one 1MB memory deallocation**. Yet all we actually need is to transfer the data from `data`'s `m_data` into the caller's buffer. (I imagine veteran C++ developers are already blushing seeing code written this way, and I bet you cannot help but cringe either.)

This is the fundamental problem with copy semantics: when you no longer need the source object, the copy constructor still faithfully duplicates every byte, and then the source object dutifully frees the original memory block when it destructs. Resources are allocated and then freed, data is copied and then discarded—pure waste.

## Move Constructor—Transferring Resource Ownership

The core idea behind the move constructor is very simple: do not copy the data, just transfer resource ownership. For classes that manage dynamic memory, this means "stealing" the pointer from the source object, and then nulling out the source object's pointer to prevent it from freeing that memory upon destruction.

```cpp
class Buffer {
    char* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    // ... 前面的构造函数和析构函数不变 ...

    // 移动构造函数
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

Let us look at this move constructor line by line. The `&&` in the signature `DynamicBuffer(DynamicBuffer&& other) noexcept` indicates that this is a move constructor—it only accepts rvalue arguments. Inside the function body, we do three things: directly copy the three members of `other` into `this` (three pointer/integer assignments, extremely cheap), and then null out `other`'s pointer. This last step is crucial—if we do not null out `other.m_data`, `other`'s destructor will free the memory we just transferred, leaving `this` holding a dangling pointer that will inevitably crash on access.

Now let us use `std::move` to trigger the move constructor:

```cpp
Buffer large(1024 * 1024);
large.append("Hello, World!", 13);

Buffer moved_to = std::move(large);  // 调用移动构造函数
// large.data_ 现在是 nullptr，但 large 仍然可以安全析构
// moved_to 持有了原来那 1MB 的内存
```

What happens during this entire process? Three pointer/integer assignments—that is it. No `new`, no `memcpy`, no `delete`. An O(n) copy operation has become an O(1) pointer transfer. For a 1MB buffer, this is the difference between "allocate 1MB of memory and copy 1MB of data" and "assign three registers."

## Move Assignment Operator—One Extra Step Compared to Move Construction

The move assignment operator is slightly more complex than the move constructor because the target object of the assignment might already hold resources—we must first release the old resources before taking over the new ones.

```cpp
class Buffer {
    // ... 前面的代码不变 ...

    // 移动赋值运算符
    Buffer& operator=(Buffer&& other) noexcept
    {
        if (this != &other) {
            // 第一步：释放当前持有的资源
            delete[] data_;

            // 第二步：接管 other 的资源
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;

            // 第三步：置空 other
            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }
};
```

Note the first step, `delete[] m_data;`—this is the key difference between move assignment and move construction. During move construction, the target object is not yet initialized, so there are no old resources to release; during move assignment, the target object already exists, and if we do not release the old resources first, we get a memory leak. The self-assignment check `if (this != &other)` is also necessary—although code like `buf = std::move(buf)` almost never appears in normal development, generic implementations of standard library components (like `std::sort`) might produce equivalent operations, so adding this safeguard is the responsible thing to do.

Let us look at the effect of move assignment in actual code:

```cpp
Buffer a(1024);
a.append("Hello", 5);

Buffer b(2048);
b.append("World", 5);

a = std::move(b);  // 移动赋值
// a 原来的 1KB 缓冲区被 delete[] 释放
// a 接管了 b 的 2KB 缓冲区
// b.data_ 变为 nullptr
```

> ⚠️ **Pitfall Warning**: After being moved from, the source object is in a "valid but unspecified" state. This means you can safely assign a new value to it or let it destruct, but you should not read its value—for example, `src.size()` might return 0, or it might return the original value, depending on the specific implementation. My advice is: let the source object leave scope immediately after moving, or assign it a clear new value. Never let a "moved-from" object wander around in your code.

## noexcept—The Safety Guarantee for Move Operations

You might have noticed that both move operations are marked with `noexcept`. This is not optional decoration—it has real performance implications.

The reason lies in the expansion behavior of `std::vector`. When a `std::vector` needs to grow its capacity, it must transfer existing elements to a new memory block. If the elements' move constructor is `noexcept`, `std::vector` will confidently use move semantics; if the move constructor might throw exceptions, `std::vector` will fall back to using the copy constructor—because if an exception is thrown during a move, the half-moved state is very difficult to recover from, but if an exception is thrown during a copy, the original data remains intact.

```cpp
// vector 内部逻辑的简化版本
if constexpr (std::is_nothrow_move_constructible_v<T>) {
    // 使用移动构造——快速且安全
} else {
    // 退化为拷贝构造——慢但异常安全
}
```

You can use `std::is_nothrow_move_constructible` to verify whether your class truly satisfies `noexcept` move operations:

```cpp
static_assert(std::is_nothrow_move_constructible_v<Buffer>,
              "Buffer should be nothrow move constructible");
static_assert(std::is_nothrow_move_assignable_v<Buffer>,
              "Buffer should be nothrow move assignable");
```

This is not just theory on paper—we can write an experiment to verify the actual behavior of `std::vector`. We prepare two `DynamicBuffer` classes with identical structure, the only difference being whether the move constructor has `noexcept`, and then we let a `std::vector` expand. The results are very clear:

```text
=== noexcept 移动 + vector 扩容 ===
--- 触发扩容 ---
  [Noexcept版] 移动构造    <-- vector 放心地移动

=== 非 noexcept 移动 + vector 扩容 ===
--- 触发扩容 ---
  [Throwing版] 拷贝构造    <-- vector 退回拷贝，确保异常安全
```

Compiled and run under GCC 15 and `-O2`, the behavior perfectly matches expectations. The complete code is available at [move_noexcept_demo.cpp](move_noexcept_demo.cpp).

## Rule of Five

C++ has a classic "Rule of Three": if your class needs a custom destructor, copy constructor, or copy assignment operator, it probably needs all three. C++11 added the move constructor and move assignment operator, turning it into the "Rule of Five."

If you only declare a destructor but do not declare any move operations, the compiler **will not** automatically generate a move constructor and move assignment operator. So what does it do? It falls back to using copy operations. This often confuses beginners: they clearly used `std::move`, but the copy constructor is still actually being called. `std::move` itself does not move anything—it is simply a type cast from an lvalue reference to an rvalue reference. What ultimately decides whether to call the move constructor or the copy constructor is the class definition. If the class does not have a move constructor, the rvalue reference will perfectly match the copy constructor that takes a `const&`.

```cpp
class OnlyDestructor {
    char* data_;

public:
    OnlyDestructor(std::size_t n) : data_(new char[n]) {}
    ~OnlyDestructor() { delete[] data_; }

    // 没有声明移动构造函数！
    // 编译器也不会隐式生成（因为有自定义析构函数）
};

OnlyDestructor a(100);
OnlyDestructor b = std::move(a);  // 退化为拷贝构造！
                                    // 隐式拷贝构造做浅拷贝 -> 双重 delete
```

The consequence here is more severe than just "inefficiency"—because the implicitly generated copy constructor performs a shallow copy (copying pointers member by member), `a` and `b`'s `m_data` will point to the same memory block. When both destruct, `delete[] m_data` is called twice, directly triggering a double free. We can use a type trait to verify this behavior:

```cpp
static_assert(!std::is_trivially_move_constructible_v<OnlyDestructor>,
              "没有真正的移动构造函数");
static_assert(std::is_move_constructible_v<OnlyDestructor>,
              "但 is_move_constructible 为 true——退回到拷贝构造");
```

Seems contradictory? Not really. `std::is_move_constructible` being true is because the compiler can use the copy constructor to "satisfy" the demand for move construction (an rvalue can bind to `const&`), but this does not mean a true move constructor exists to perform the pointer transfer. The complete verification code is in [rule_of_five_demo.cpp](rule_of_five_demo.cpp).

For classes that manage resources, the safest approach is to **either fully customize all five special member functions, or make them all `= default`**. If you use smart pointers to manage resources, you can typically use `= default` to let the compiler generate the correct versions—this is exactly the approach recommended by modern C++. But for classes like ours that manually manage raw pointers, we must dutifully write all five:

```cpp
class Buffer {
    char* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    // 1. 构造函数
    explicit Buffer(std::size_t capacity)
        : data_(new char[capacity])
        , size_(0)
        , capacity_(capacity)
    {
    }

    // 2. 析构函数
    ~Buffer()
    {
        delete[] data_;
    }

    // 3. 拷贝构造
    Buffer(const Buffer& other)
        : data_(new char[other.capacity_])
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        std::memcpy(data_, other.data_, size_);
    }

    // 4. 移动构造
    Buffer(Buffer&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    // 5. 拷贝赋值
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

    // 6. 移动赋值
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

It looks a bit long, but the logic is repetitive—copy operations perform deep copies, and move operations perform pointer transfers plus nulling out the source object.

## Copy-and-Swap Idiom—Reducing Code Duplication

If you feel that writing four assignment operators (copy assignment + move assignment) is too verbose, there is a classic idiom that can help you simplify. The core idea is: **let copy assignment and move assignment share a single implementation**, leveraging the semantics of pass-by-value to automatically choose between copying or moving.

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

    // 拷贝构造
    Buffer(const Buffer& other)
        : data_(other.capacity_ ? new char[other.capacity_] : nullptr)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        if (data_) {
            std::memcpy(data_, other.data_, size_);
        }
    }

    // 移动构造
    Buffer(Buffer&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    // 统一的赋值运算符——通过值传递自动选择拷贝或移动
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

Here, `operator=` receives the parameter by value—if you pass in an lvalue, `other` is created via the copy constructor; if you pass in an rvalue (like `std::move(buf)`), `other` is created via the move constructor. Then `swap` exchanges the contents of `*this` and `other`, and when the function ends, `other` destructs, automatically releasing the old resources.

The advantage of this idiom is less code, exception safety, and automatic handling of self-assignment. The downside is an extra swap operation (three pointer swaps), which might have a minor impact in extreme performance scenarios. However, in the vast majority of cases, this overhead is completely negligible—comparing the assembly with GCC 15 under `-O2` reveals that the move assignment path of copy-and-swap adds about three register move instructions (the cost of the swap) compared to a standalone move assignment operator, but there are no extra function calls or memory operations. For classes managing dynamic memory, the overhead of `new`/`delete` far outweighs these three register instructions, so the extra cost of copy-and-swap is practically immeasurable in real-world use.

## Practical Example—Moving File Handles

Beyond dynamic memory, move semantics are equally powerful for classes managing other types of resources. File handles are a typical example—operating systems limit the number of open handles for the same file, and if you accidentally copy an object holding a file handle, it can lead to handle leaks or duplicate closes.

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

    // 禁止拷贝——文件句柄不可共享
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    // 允许移动——文件句柄可以转移所有权
    FileHandle(FileHandle&& other) noexcept
        : file_(other.file_)
        , path_(std::move(other.path_))
    {
        other.file_ = nullptr;  // 防止 other 析构时关闭文件
    }

    FileHandle& operator=(FileHandle&& other) noexcept
    {
        if (this != &other) {
            if (file_) {
                std::fclose(file_);  // 关闭当前文件
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

/// @brief 工厂函数：打开日志文件
FileHandle open_log(const std::string& name)
{
    return FileHandle(name.c_str(), "a");
}

int main()
{
    auto log = open_log("app.log");
    std::fprintf(log.get(), "Application started\n");

    // 把日志文件的所有权转移给另一个变量
    FileHandle moved_log = std::move(log);
    std::fprintf(moved_log.get(), "Log handle moved\n");

    // log.get() 现在返回 nullptr，不要再使用它
    return 0;
}
```

This example demonstrates a common design pattern: **non-copyable but movable**. A file handle physically exists as only one instance and should not be "copied" into a second one—copying would cause both objects to try to close the same file. But moving is reasonable: `openFile` creates the file handle, then transfers ownership to the caller, and the temporary object inside the function no longer holds any resources.

When you run this program, you will see:

```text
  关闭文件: app.log
```

Note that there is only one "Closing file" output—even though both `file` and `movedFile` go through destruction, `file`'s `m_fd` was set to `-1` after being moved, so the `m_fd >= 0` check in its destructor fails, preventing a duplicate close.

## Hands-On Experiment—move_semantics_demo.cpp

Let us write a complete program to verify all the key behaviors of move semantics.

```cpp
// move_semantics_demo.cpp -- 移动构造与移动赋值演示
// Standard: C++17

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
    buffers.push_back(c);             // 拷贝
    std::cout << "  push_back std::move:\n";
    buffers.push_back(std::move(c));  // 移动
    std::cout << "  emplace_back 原位构造:\n";
    buffers.emplace_back(512);        // 直接在 vector 中构造
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

The contrast in the output between "Move constructor (pointer transfer)" and "Copy constructor X bytes" is clear at a glance—copying requires memory allocation plus data duplication, while moving is just three pointer assignments. Step 5's vector operations are even more noteworthy: passing an lvalue with `push_back` triggers a copy, passing an rvalue with `std::move` triggers a move, and `emplace_back` constructs in-place directly in the vector's memory, saving even the move. The performance differences between these three operations become very obvious with large data volumes.

Notice that there is no "Freeing 0 bytes" output during destruction—those are the objects that have been moved from, their `m_data` is `nullptr`, and the `m_data != nullptr` check in the destructor skips the `delete[]`. The three elements in the vector each destruct independently—the first is a copy of `buf1` (1024 bytes), the second was moved from `buf2` (1024 bytes), and the third was constructed in-place by `emplace_back` (512 bytes).

## Summary

In this article, we thoroughly broke down move constructors and move assignment operators from start to finish. The core of move operations is **resource ownership transfer**—do not copy data, just steal the pointer, and then null out the source object. Move assignment has one extra step compared to move construction: you must first release the old resources held by the target object. All move operations should be marked `noexcept`, as this directly impacts the behavior of containers like `std::vector` during expansion. If your class manages resources, remember the Rule of Five: destructor, copy constructor, move constructor, copy assignment, and move assignment—either write all five, or make them all `= default`.

In the next article, we will look at another major thing the compiler does for us behind the scenes—return value optimization (RVO and NRVO), which can reduce the cost of returning large objects from functions to exactly zero.
