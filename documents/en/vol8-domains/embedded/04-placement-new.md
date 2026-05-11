# Embedded C++ Tutorial: placement new

In the embedded world, `malloc` / `new` are often not a silver bullet. Some target platforms have no free heap at all (bare-metal, certain RTOSes), some scenarios require **disabling the heap** for predictability and real-time performance, and in some cases you need to control the memory layout down to the byte — that's where *placement new* comes in.

So, this time we'll discuss this handy little feature called placement new.

## So what is placement new? How do we use it?

The simplest example — placing an object into a buffer on the stack:

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

See that `new`? In reality, `new(buffer) Foo(args...)` here simply invokes the constructor, constructing the object at the location specified by `buffer`. Notice that this region actually resides on the stack, and you **cannot** use `delete` on a placement-new object; you must explicitly call the destructor. Of course, to satisfy alignment requirements, the buffer should use `alignas` or `std::aligned_storage`.

This is just a demo usage, though — nobody would actually use it this way in practice...

------

## Alignment and Memory Layout — Don't Let UB Come Knocking

Alignment is a top priority in embedded development. Before constructing `T`, the buffer must satisfy `alignof(T)`. A common approach:

```cpp
// C++11/14 风格（仍可用）
using Storage = typename std::aligned_storage<sizeof(Foo), alignof(Foo)>::type;
Storage storage;
Foo* p = new (&storage) Foo(1);

// C++17 以后更直观的方式
alignas(Foo) unsigned char storage2[sizeof(Foo)];
Foo* q = new (storage2) Foo(2);

```

If you are writing your own allocator, you need to implement `allocate`, rounding the returned address up to a multiple of `alignment` (using `uintptr_t` arithmetic).

------

## Exception Safety and Construction Failure

When a constructor might throw an exception, we need to be careful with placement new exception handling — if construction fails, no object requiring explicit destruction is produced (since it wasn't successfully constructed). However, if you are constructing multiple objects in multiple steps during a complex initialization, you must correctly roll back the successfully constructed parts in the `catch` block.

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

In short: **construction failure interrupts the flow but does not automatically clean up already-constructed objects** — you are responsible for this.

------

## Bump (Linear) Allocator + placement new

The most common alternative in embedded systems is an arena / bump allocator: allocate a large block of memory upfront, then allocate linearly on demand; destruction is typically done all at once when the entire arena is reset. It is perfectly suited for objects that are allocated at startup and exist long-term, such as drivers, initialization data, and so on. We'll dive into arena / bump allocators in a dedicated future discussion; for now, we're just taking a look.

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

Note: `reset()` **does not** automatically call destructors — if objects hold important resources (files, mutexes, heap memory), you must explicitly destroy them first.

------

## Object Pools (free-list) — Supporting Deallocation / Reuse

Remember our object pool? When you need to control memory while still allowing dynamic deallocation, the most common pattern is an object pool (free-list) + placement new. This is ideal for high-frequency allocation/deallocation of fixed-size objects (such as network packets, task structures).

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

- `std::ptrdiff_t` is used to calculate back to the start of the slot (be mindful of portability — this is a common trick);
- If you need multi-threaded access, remember to add a lock to the pool or use a lock-free structure.

------

## To Keep Pointers Intact — What's the Point of `std::launder`?

When you repeatedly use placement new to construct objects of the same type at the same memory location, certain situations require `std::launder` to obtain a "valid" pointer, avoiding issues caused by compiler optimizations. A simple demonstration (added in C++17):

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

Typically in embedded code, simply storing the pointer in a local variable and carefully managing its lifetime is sufficient. But when you face potential subtle bugs from aliasing or compiler optimizations, `std::launder` can come in handy.

------

## Making the Tedious Usable: Writing a Small InPlace RAII Wrapper

Repeatedly writing placement new + explicit destructors is error-prone. A small wrapper can make the code much cleaner:

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

With `InPlace<T>`, you can bind the lifetime to a function or object, preventing forgotten destructors (RAII FTW).

------

## When NOT to Use placement new?

- You need complex memory allocation strategies (defragmentation, reclamation policies) — a more complete allocator (TLSF, slab, buddy) would be more suitable;
- Objects are very large or expensive to construct but are frequently created/destroyed — unless you have a compelling reason, using dynamic memory or a mature pool is less of a headache;
- Scenarios where you cannot guarantee proper resource destruction under exceptions or interrupts.

------

## Wrapping Up

In a world without a heap, placement new is like a "small and beautiful" toolbox: where you place objects, when you construct them, and when you tear them down — it's all up to you. This brings immense controllability, but it also hands back some details originally handled by the runtime — you must responsibly manage lifetimes, alignment, and exceptions.

If you're the type who likes to "draw memory into grids," placement new will make you very happy. If you don't want to manage lifetimes manually, then you either put on RAII armor (write it yourself or use a framework), or accept a slightly larger runtime (a controlled heap). Ultimately, there are no perfect solutions in embedded, only suitable ones — placement new is a sharp, reliable little knife. Use it well, and it saves time and effort; use it poorly, and you'll cut your fingers.

------

## Code Examples
