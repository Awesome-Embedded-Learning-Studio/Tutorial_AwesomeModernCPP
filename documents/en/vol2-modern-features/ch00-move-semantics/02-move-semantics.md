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
reading_time_minutes: 13
related:
- 'The Rule of Five: How the Special Member Functions Fit Together'
- 'RVO and NRVO: The Compiler''s Return Value Optimization'
- 'Perfect Forwarding: Keeping Value Categories Intact'
tags:
- host
- cpp-modern
- intermediate
- 移动语义
title: Move Construction and Move Assignment
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/02-move-semantics.md
  source_hash: fd8a095487d30611ebcfe3bf7f56d94acd9b3f03022548fdcdd4a320bdfbaea5
  translated_at: '2026-09-27T04:31:21+00:00'
  engine: anthropic
  token_count: 7100
---
# Move Construction and Move Assignment

At the end of the last article we made a deal: this time we hand-write the move constructor and move assignment operator for a resource-managing class. The first time I wrote them myself, I got plenty wrong—forgot to null out the source object's pointer, skipped the self-assignment check, couldn't make up my mind about whether `noexcept` belonged... We'll lay out all the mistakes I made back then so you can dodge them in one pass when it's your turn to write them.

We'll start from a simple but realistic-enough scenario: build a dynamic buffer class ourselves, then work through the mechanics of move construction and move assignment step by step. The Rule of Five and the companion copy-and-swap idiom get their own dedicated treatment in the next article.

## Why We Need Move—Starting from the Cost of Copying

Suppose you're writing a text-processing tool that constantly passes big chunks of text data between functions. Let's start with the most bare-bones dynamic buffer implementation:

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

So what actually happens when we call `process_buffer(large)`? The parameter `buf` is passed by value, and what the compiler uses to create it is `Buffer`'s copy constructor—with the bill following right behind: allocate a fresh 1MB of memory, then copy `large`'s data over byte by byte. When the function returns, `return buf;` triggers one more copy construction, and that's where `result` comes from. When the function wraps up, `buf` gets destroyed as well. Tally the whole trip and we've done **two 1MB allocations, two 1MB copies, plus one 1MB deallocation**. And all we really wanted was to get the data from `large` in `main` over into `result`.

> I reckon the folks who've written old-school C++ for years are already red in the face at code like this. Trust me, you on the other side of the screen won't keep a straight face either.

Right there, the problem with copy semantics is out in the open: you're clearly done with the source object, yet the copy constructor still faithfully duplicates every byte. Then when the source object is destroyed, it dutifully releases that block of memory. Resources allocated then freed, data copied then thrown away—pure waste.

## The Move Constructor—Transferring Resource Ownership

Now it's the move constructor's turn on stage. What it does fits in one sentence: copy not a single byte of data—just transfer ownership of the resources<RefLink :id="1" preview="cppreference Move constructor — transfer instead of copy" />. For a class managing dynamic memory, the action is to "steal" the pointer from the source object, then null the source object out and be done with it. Straight to the code:

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

Let's walk through this move constructor line by line. The `&&` in the signature `Buffer(Buffer&& other)` announces that it accepts only rvalue arguments. The function body doesn't do much: carry `other`'s three members straight over—three pointer/integer assignments, cheap enough that we can all but ignore the cost. The remaining step zeroes out `other`'s members. That nulling step deserves a second look. Suppose we didn't null `other.data_`: when `other` is destroyed, `delete[] other.data_` would release the very memory we just took over. What `this` ends up holding is then a dangling pointer, and any later access buys us a crash.

Now let's trigger move construction with `std::move`:

```cpp
Buffer large(1024 * 1024);
large.append("Hello, World!", 13);

Buffer moved_to = std::move(large);  // Invokes the move constructor
// large.data_ is now nullptr, yet large can still be destroyed safely
// moved_to owns the original 1MB of memory
```

What happened over this whole trip? Let's count: carry `other`'s members over, null them out—a handful of pointer/integer assignments and we're done. No `new` allocation, no `memcpy` replication, no `delete` deallocation. An O(n) copy just became an O(1) pointer transfer. For a 1MB buffer, one side has to allocate 1MB of memory and then copy 1MB of data; the other side just shuffles a few registers. Quite a gap, wouldn't you say?

## The Move Assignment Operator—One Step More than Move Construction

The move assignment operator carries one extra chore compared to the move constructor. During construction, the target object's initialization hasn't happened yet, so there's no old resource to speak of. During assignment, the target object already exists and may well be clutching a ready-made resource in its hand. So we have to let go of the old one before taking over the new one<RefLink :id="2" preview="cppreference Move assignment operator — release current resource, take over the source" />.

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

Point the camera at the `delete[] data_` at the top of the function body. What that line releases is exactly the ready-made resource the target object was clutching, as described a moment ago. Skip releasing it and the memory simply leaks. The self-assignment check `if (this != &other)` also deserves a mention. Nobody writes code like `x = std::move(x)` in normal development. But if it does get written, it does its damage without hesitation: `delete[] data_` releases your own resource, and then you go take the pointers from the already-dangling `other` (which is really yourself)—and that is a straight-up UAF (use-after-free). Adding that one extra check, trading a few lines of code for a deterministic outcome, is worth it.

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

The moved-from source object is left in a "valid but unspecified" state<RefLink :id="3" preview="cppreference std::move — Notes: moved-from standard-library objects are valid but unspecified" />. You can let it be destroyed safely, or go on assigning it new values—but don't read its value. For a moved-from standard-library type, a call like `size()` might give you 0 or might give you the original value, entirely depending on the library's implementation. My advice: after a move, either get the source object out of scope right away, or assign it a definite new value. Until it holds a definite new value, don't read it again.

## noexcept—The Safety Promise of Move Operations

You may have already noticed that both move operations wear `noexcept`. It's not optional decoration—real performance is wired to it.

The reason has to be hunted down in `std::vector`'s reallocation behavior. When capacity runs out, the `vector` has to transfer the existing elements into a new memory block. At that point it starts weighing the element's move constructor: if it's marked `noexcept`, the `vector` moves with confidence. And if the move constructor might throw? The `vector` retreats and uses the copy constructor instead<RefLink :id="4" preview="cppreference std::move_if_noexcept — how vector reallocation picks move vs copy" />. One moment of thought and the logic is obvious—an exception thrown midway through a move leaves a half-moved state that is very hard to recover. An exception thrown midway through a copy is a different story: the original data is still intact.

```cpp
// A simplified version of vector's internal logic
if constexpr (std::is_nothrow_move_constructible_v<T>) {
    // Use move construction—fast and safe
} else {
    // Fall back to copy construction—slower but exception-safe
}
```

You can use `static_assert` to verify that your class really does satisfy `noexcept` moving:

```cpp
static_assert(std::is_nothrow_move_constructible_v<Buffer>,
              "Buffer should be nothrow move constructible");
static_assert(std::is_nothrow_move_assignable_v<Buffer>,
              "Buffer should be nothrow move assignable");
```

All this reasoning is armchair strategy on its own—let's genuinely run an experiment and watch how `vector` actually chooses. Prepare two identically structured classes whose only difference is whether the move constructor carries `noexcept`, then make the `vector` grow. The cheapest way is a single template parameter `NoexceptMove` that toggles the `noexcept` marker, with all the remaining code completely identical:

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

The experiment program is wired into the demo below—click "Try It" and run it right away:

<OnlineCompilerDemo
  title="Hands-On Verification: noexcept_vector_realloc.cpp"
  source-path="code/examples/vol2/noexcept_vector_realloc.cpp"
  description="Verify online how vector chooses during reallocation—run it and compare which kind of construction each of the two output sections triggers."
  run-options="-O0 -std=c++17"
  allow-run
/>

Each of the two output sections prints exactly one line—let's set them side by side. Why only one line each? The only members of the class that print anything are the copy constructor and the move constructor; both `emplace_back` in-place constructions stay silent, so what actually gets printed is the reallocation transfer. The line tagged `[Noexcept版]` says **移动构造**, and the line tagged `[Throwing版]` says **拷贝构造**.

## Hands-On Experiment—move_semantics_demo.cpp

Let's write a complete program that verifies every key behavior of move semantics.

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

The demo below holds the complete code—click "Try It" to run it. We can also switch to the assembly view and watch the move constructor's pointer transfer:

<OnlineCompilerDemo
  title="Hands-On Experiment: move_semantics_demo.cpp"
  source-path="code/examples/vol2/02_move_semantics.cpp"
  description="Run online and compare Buffer's copy construction vs move construction, plus how they behave differently inside a vector."
  run-options="-O0 -std=c++17"
  allow-run
  allow-x86-asm
/>

Steps 2 and 3 have been turned into an animation of the memory-level actions—you can play it, pause it, or use the step buttons to walk through one step at a time and get a clear look at the pointer handoff:

<Anim id="copy-vs-move" />

Put "移动构造（指针转移）" next to "拷贝构造 X 字节" and the contrast is plain at a glance: copying means allocating memory and then replicating data, while moving is just three pointer assignments. Step 5's `vector` operations have even more to offer. When `push_back` receives an lvalue, a copy happens. Pass an rvalue wrapped in `std::move`, and a move happens. `emplace_back` goes one step further, constructing in place directly in the `vector`'s memory, skipping even the move. Once the data volume grows, the performance gap among the three spellings becomes very noticeable.

We'll also notice there's no "释放 0 字节" line at destruction time. Those would be the moved-from objects: their `data_` is already `nullptr`, so the `if (data_)` check in the destructor skips the `delete[]`. The three elements in the `vector` are destroyed independently: the first is the copy of `c` (1024 bytes), the second is the one moved out of `c` (1024 bytes), and the third is the one `emplace_back` constructed in place (512 bytes).

With that, the move constructor and the move assignment operator are both written out. But for a resource-managing class, moves alone are not enough—how do the destructor, copy constructor, copy assignment, and the move operations work as a set, and what happens if one goes missing? In the next article we'll walk through the "Rule of Five" in full, then look at two companion ways to write it.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="Move Constructor"
    url="https://en.cppreference.com/w/cpp/language/move_constructor"
  />
  <ReferenceItem
    :id="2"
    author="cppreference.com"
    title="Move Assignment Operator"
    url="https://en.cppreference.com/w/cpp/language/move_assignment"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="std::move"
    chapter="Notes: moved-from state"
    url="https://en.cppreference.com/w/cpp/utility/move"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="std::move_if_noexcept"
    url="https://en.cppreference.com/w/cpp/utility/move_if_noexcept"
  />
  <ReferenceItem
    :id="5"
    author="Howard E. Hinnant, Peter Dimov, Dave Abrahams"
    title="A Proposal to Add Move Semantics Support to the C++ Language (N1377)"
    publisher="WG21 / ISO C++ Committee"
    :year="2002"
    url="https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2002/n1377.htm"
  />
</ReferenceCard>
