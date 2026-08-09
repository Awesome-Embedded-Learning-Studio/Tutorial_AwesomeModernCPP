---
chapter: 5
cpp_standard:
- 11
- 14
- 17
- 20
description: Placement New Application Strategies
difficulty: intermediate
order: 4
platform: stm32f1
prerequisites:
- 'Chapter 3: 内存与对象管理'
reading_time_minutes: 8
tags:
- cpp-modern
- intermediate
- stm32f1
title: Usage of Placement New
translation:
  source: documents/vol8-domains/embedded/04-placement-new.md
  source_hash: 62724799e9d21e68c1235f75c8d0c818239e849964c1900aba791dfe25895079
  translated_at: '2026-06-16T04:12:38.331640+00:00'
  engine: anthropic
  token_count: 1817
---
# Embedded C++ Tutorial: placement new

In the embedded world, `malloc` / `free` are often not a "magic bullet". Some target platforms have no free heap at all (bare-metal, certain RTOSes), some scenarios require **disabling the heap** for predictability and real-time performance, and in other cases, you need to control memory layout with pixel-level precision—this is where *placement new* comes into play.

So, this time let's discuss this little thing called placement new.

## So what is placement new? How do we use it?

The simplest example—putting an object into a buffer on the stack:

```cpp
#include <new>      // for placement new
#include <cstddef>
#include <iostream>
#include <type_traits>

struct Foo {
    int x;
    Foo(int v): x(v) { std::cout << "Foo(" << x << ") ctor\n"; }
    ~Foo() { std::cout << "Foo(" << x << ") dtor\n"; }
};

int main() {
    // 为了安全，使用对齐良好的 unsigned char 缓冲区
    alignas(Foo) unsigned char buffer[sizeof(Foo)];

    // placement new：在 buffer 起始处构造 Foo
    Foo* p = new (buffer) Foo(42); // 与 delete 无关
    std::cout << "p->x = " << p->x << "\n";

    // 显式析构（非常重要）
    p->~Foo();
}

```

See that `new`? Actually, the `new (buffer) Foo(args...)` here simply calls the constructor to construct the object at the location specified by `buffer`. Note that this area is physically on the stack, so you **cannot** use `delete` on a placement-new object; you must explicitly call the destructor. Of course, to satisfy alignment requirements, the buffer should use `alignas(T)` or `std::aligned_storage`.

However, this is just a demonstration usage; no one would actually use it this way...

------

## Alignment and Memory Layout—Don't Let UB Knock on Your Door

Alignment is a top priority in embedded systems. Before constructing a `T`, the buffer must satisfy `alignof(T)`. A common approach:

```cpp
// C++11/14 风格（仍可用）
using Storage = typename std::aligned_storage<sizeof(Foo), alignof(Foo)>::type;
Storage storage;
Foo* p = new (&storage) Foo(1);

// C++17 以后更直观的方式
alignas(Foo) unsigned char storage2[sizeof(Foo)];
Foo* q = new (storage2) Foo(2);

```

If you are writing your own allocator and need to implement `allocate`, round the return address up to a multiple of `alignof(std::max_align_t)` (using `uintptr_t` arithmetic).

------

## Exception Safety and Construction Failure

When the constructor might throw exceptions, handling placement new exceptions requires care—if construction fails, no object that needs explicit destruction is produced (because it wasn't successfully constructed). However, if you construct multiple objects in multiple steps during a complex initialization, you must correctly roll back the successfully constructed parts in the `catch` block.

```cpp
// 伪代码：在一段连续缓冲区中构造多个对象
Foo* objs[3];
unsigned char* buf = ...; // 足够大、对齐良好
unsigned char* cur = buf;
int constructed = 0;
try {
    for (int i = 0; i < 3; ++i) {
        void* slot = cur; // assume aligned
        objs[i] = new (slot) Foo(i); // 可能抛
        ++constructed;
        cur += sizeof(Foo); // 简化示意
    }
} catch (...) {
    // 回滚已经构造的对象
    for (int i = 0; i < constructed; ++i) objs[i]->~Foo();
    throw; // 继续抛出或记录错误
}

```

In short: **Construction failure interrupts the flow but does not automatically clean up constructed objects**—you are responsible for this.

------

## Bump (Linear) Allocator + placement new

The most common alternative in embedded systems is an arena / bump allocator: pre-allocate a large block of memory, then allocate linearly on demand; destruction is usually done uniformly when the entire arena is reset. It is perfect for "allocate-once, live-long" objects, such as drivers, initialization data, etc. We will discuss arena / bump allocators in detail later; here, we just take a look.

```cpp
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <new>

struct BumpAllocator {
    uint8_t* base;
    size_t capacity;
    size_t offset;

    BumpAllocator(void* mem, size_t cap) : base(static_cast<uint8_t*>(mem)), capacity(cap), offset(0) {}

    // 返回已对齐的指针，或 nullptr（失败）
    void* allocate(size_t n, size_t align) {
        uintptr_t cur = reinterpret_cast<uintptr_t>(base + offset);
        uintptr_t aligned = (cur + align - 1) & ~(align - 1);
        size_t nextOffset = aligned - reinterpret_cast<uintptr_t>(base) + n;
        if (nextOffset > capacity) return nullptr;
        offset = nextOffset;
        return reinterpret_cast<void*>(aligned);
    }

    void reset() { offset = 0; }
};

struct Bar {
    int v;
    Bar(int x): v(x) {}
    ~Bar() {}
};

int main() {
    static uint8_t arena_mem[1024];
    BumpAllocator arena(arena_mem, sizeof(arena_mem));

    void* p = arena.allocate(sizeof(Bar), alignof(Bar));
    Bar* b = nullptr;
    if (p) b = new (p) Bar(100); // placement new
    // 使用 b...
    b->~Bar(); // 如果你需要提前析构单个对象（可选）
    // 更常见：app 结束或 mode 切换时统一 reset：
    // arena.reset(); // 这不会调用析构函数——只适用于 POD 或者你自己管理析构
}

```

Note: `std::vector::clear` **does not** automatically call destructors—if objects hold important resources (files, mutexes, heap), you must explicitly destroy them first.

------

## Object Pool (Free-List)—Supporting Deallocation/Reuse

Remember our object pool? When you need to control memory and also allow dynamic deallocation, the most common pattern is an object pool (free-list) + placement new. It is suitable for high-frequency allocation/deallocation of fixed-size objects (e.g., network packets, task structures).

```cpp
#include <cstddef>
#include <new>
#include <cassert>

// 简化的对象池（单线程示例）
template<typename T, size_t N>
class ObjectPool {
    union Slot {
        Slot* next;
        alignas(T) unsigned char storage[sizeof(T)];
    };
    Slot pool[N];
    Slot* free_head = nullptr;

public:
    ObjectPool() {
        // 初始化 free list
        for (size_t i = 0; i < N - 1; ++i) pool[i].next = &pool[i+1];
        pool[N-1].next = nullptr;
        free_head = &pool[0];
    }

    template<typename... Args>
    T* allocate(Args&&... args) {
        if (!free_head) return nullptr;
        Slot* s = free_head;
        free_head = s->next;
        T* obj = new (s->storage) T(std::forward<Args>(args)...);
        return obj;
    }

    void deallocate(T* obj) {
        if (!obj) return;
        obj->~T();
        // 将 slot 重回 free list
        Slot* s = reinterpret_cast<Slot*>(reinterpret_cast<unsigned char*>(obj) - offsetof(Slot, storage));
        s->next = free_head;
        free_head = s;
    }
};

```

Key points:

- `offsetof` is used to calculate the start of the slot (be careful with portability—this is a common trick);
- If you need multi-threaded access, remember to lock the pool or use lock-free structures.

------

## To Avoid Messing Up Pointers—What is `std::launder` Used For?

When you repeatedly placement new objects of the same type at the same memory location, `std::launder` is sometimes needed to obtain a "valid" pointer and avoid issues caused by compiler optimizations. Simple illustration (C++17 addition):

```cpp
#include <new>
#include <memory> // for std::launder

alignas(Foo) unsigned char buf[sizeof(Foo)];
Foo* a = new (buf) Foo(1);
a->~Foo();
Foo* b = new (buf) Foo(2);

// 如果你以前保存了旧指针 a，重新使用它可能是 UB。
// 使用 std::launder 可以得到新的、可靠的指针：
Foo* safe_b = std::launder(reinterpret_cast<Foo*>(buf));

```

Usually in embedded code, storing the pointer in a local variable and carefully managing its lifecycle is sufficient; but when facing potential bugs from aliasing or compiler optimization, `std::launder` can come in handy.

------

## Run Online

Run the `InPlace` RAII wrapper example online to observe the safe use of placement new:

<OnlineCompilerDemo
  title="placement new RAII Wrapper: InPlace<T>"
  source-path="code/examples/compiler_explorer/placement_new_inplace_host.cpp"
  arm-source-path="code/examples/compiler_explorer/placement_new_inplace_arm.cpp"
  description="Run online and observe how InPlace<T> safely constructs and destructs objects in a heap-less environment."
  allow-run
  allow-x86-asm
  allow-arm-asm
/>

## Making the Tedious Usable: Writing a Small InPlace RAII Wrapper

Repeatedly writing placement + explicit destruction is error-prone; a small wrapper can make the code cleaner:

```cpp
#include <new>
#include <type_traits>
#include <utility>

template<typename T>
class InPlace {
    alignas(T) unsigned char storage[sizeof(T)];
    bool constructed = false;
public:
    InPlace() noexcept = default;

    template<typename... Args>
    void construct(Args&&... args) {
        if (constructed) this->destroy();
        new (storage) T(std::forward<Args>(args)...);
        constructed = true;
    }

    void destroy() {
        if (constructed) {
            reinterpret_cast<T*>(storage)->~T();
            constructed = false;
        }
    }

    T* get() { return constructed ? reinterpret_cast<T*>(storage) : nullptr; }
    ~InPlace() { destroy(); }
};

```

With `InPlace`, you can bind the lifecycle to a function/object, preventing you from forgetting to destroy it (RAII FTW).

------

## When NOT to use placement new?

- You need complex memory allocation strategies (defragmentation, reclamation strategies)—a more complete allocator (TLSF, slab, buddy) would be more suitable;
- Objects are very large or construction is expensive but they are created/destroyed frequently—unless there is a compelling reason, using dynamic memory or a mature pool is less worrisome;
- Scenarios where you cannot guarantee resources are correctly destructed during exceptions or interrupts.

------

## Conclusion

In a world without a heap, placement new is like a "small and beautiful" toolbox: you decide where to put objects, when to construct them, and when to tear them down. This brings immense control, but also returns some details originally managed by the runtime to you—you are responsible for managing lifecycles, alignment, and exceptions.

If you are the type who likes to "draw memory in grids," placement new will make you happy; if you don't want to manage lifecycles manually, then either put on RAII armor (write it yourself or use a framework) or accept a slightly larger runtime (a managed heap). In short, there is no perfect solution in embedded, only suitable ones—placement new is a sharp, reliable little knife; use it well and it works wonders, use it poorly and you'll cut your fingers.

------

## Code Examples
