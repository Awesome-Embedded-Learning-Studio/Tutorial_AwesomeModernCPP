# 嵌入式C++教程：placement new

在嵌入式世界里，`new` / `delete` 往往不是“万能钥匙”。有的目标平台根本没有自由堆（裸机、某些 RTOS），有的场景为了可预测性和实时性要**禁用 heap**，更有些情形下你需要把内存布局控制到厘米级——这就要靠 *placement new*（放置 new）来做事情了。

所以，这一次我们来讨论下placement new这个小东西。

## 所以什么是 placement new？怎么用

最简单的例子——把一个对象放进栈上的一个缓存区：

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

看到了这个new嘛？实际上这里的new(buffer) Foo(args...)只是调用构造函数，在 `buffer` 指定的位置构造对象，而且注意到，这个区域是实际放在栈上的，**不能**对 placement-new 的对象使用 `delete p;`；必须显式调用 `p->~Foo()`。当然，为了满足对齐，缓冲区应使用 `alignas(T)` 或 `std::aligned_storage_t`。

不过这只是一个示例用法，没有人真的会这样使用的。。。

------

## 对齐与内存布局——别让 UB 找上门

对齐问题在嵌入式里是头等大事。构造 `Foo` 前，缓冲区必须满足 `alignof(Foo)`。常见做法：

```cpp
// C++11/14 风格（仍可用）
using Storage = typename std::aligned_storage<sizeof(Foo), alignof(Foo)>::type;
Storage storage;
Foo* p = new (&storage) Foo(1);

// C++17 以后更直观的方式
alignas(Foo) unsigned char storage2[sizeof(Foo)];
Foo* q = new (storage2) Foo(2);
```

如果你自己写 allocator，要实现 `align_up()`，把返回地址向上对齐到 `align` 的倍数（使用 uintptr_t 算术）。

------

## 异常安全与构造失败

当构造函数可能抛异常时，placement new 的异常处理要小心——如果构造失败，则不会产生要显式析构的对象（因为没有成功构造），但如果你在一段复杂的初始化里分多步构造多个对象，就要在 catch 中正确回滚已经成功构造的部分。

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

总之：**构造失败会中断流程，但不会自动清理已构造对象**，这事儿你要负责。

------

## Bump（线性）分配器 + placement new

在嵌入式里最常见的替代方案是 arena / bump allocator：预先申请一块大内存，然后按需线性分配；析构通常在整个 arena reset 的时候统一做。它非常适合“启动时分配后长期存在”的对象，例如 drivers、初始化数据等。arena / bump allocator会之后专门聊，这里就是看看。

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

注意：`arena.reset()` **不会**自动调用析构函数——如果对象有重要资源（文件、mutex、heap），你必须先显式析构。

------

## 对象池（free-list）——支持释放/重用

还记得我们的对象池嘛？当你既要控制内存又要动态释放时，最常见模式是对象池（free-list）＋ placement new。适合固定大小对象的高频分配/释放（例如网络包、任务结构体）。

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

要点：

- `offsetof(Slot, storage)` 用来回算出 slot 起点（小心可移植性——这里是常见技巧）；
- 如果你需要多线程访问，记得给 pool 加锁或使用无锁结构。

------

## 为了不把指针玩残——`std::launder` 有什么用？

当你在同一内存位置反复 placement new 相同类型对象时，某些情况下需要 `std::launder` 来取得“有效”的指针，避免编译器优化引起的问题。简单示意（C++17 增）：

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

通常在嵌入式代码中，直接把指针存放在 local 变量并谨慎管理生命周期就行；但当你面对别名 / 编译器优化带来的潜在的小bug，`std::launder` 可以派上用场。

------

## 把繁琐变得可用：写一个小型 InPlace RAII wrapper

重复写 placement + 显式析构容易出错，做个小封装能让代码更干净：

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

有了 `InPlace<T>`，你可以把生命周期绑定到函数/对象上，防止忘记析构（RAII FTW）。

------

## 什么时候不要用 placement new？

- 你需要复杂的内存分配策略（碎片整理、回收策略）——更完善的 allocator（TLSF、slab、buddy）会更适合；
- 对象很大或构造很昂贵但频繁创建/销毁，除非有充分理由，用动态内存或成熟池更省心；
- 不能保证在异常或中断下正确析构资源的场景。

------

## 所以

在没有堆的世界里，placement new 就像一个“小而美”的工具箱：你把对象放在哪里，什么时候构造、什么时候拆掉，都由你说了算。这既带来巨大的可控性，也把一些原本由 runtime 负责的细节交还给你——你要负责任地管理生命周期、对齐与异常。

如果你是那种喜欢把内存“画成格子”的人，placement new 会让你很开心；如果你不想手动管理生命周期，那么你要么带上 RAII 护甲（自己写或用框架），要么就接受有点儿更大的运行时（受控的 heap）。总之，嵌入式没有完美解，只有合适的解——placement new 是一把锋利而可靠的小刀，用好了事半功倍，用不好就是割手指。