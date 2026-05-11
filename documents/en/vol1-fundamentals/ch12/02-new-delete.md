# Dynamic Memory Management

In the previous chapter, we divided a program's memory space into four regions: the stack, the heap, the static storage, and the code segment. We clarified where data "lives" and how long it "survives." But we left one question open: how exactly do we manage dynamic memory on the heap? What happens behind the scenes with `new` and `delete`? Why has nearly every chapter so far stressed "use smart pointers, avoid raw `delete`"?

In this chapter, we tackle these questions head-on. Dynamic memory gives us the greatest degree of freedom in C++—we can request memory of any size at runtime, completely free from stack size limits. But this freedom comes with the heaviest responsibility: every block of memory obtained via `new` must be properly `delete`, or we get a leak; every `delete` must correspond to the correct `new`, or we trigger undefined behavior (UB).

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Correctly use `new`/`delete` and `new[]`/`delete[]`, avoiding mismatch errors
> - [ ] Detect memory leaks using AddressSanitizer
> - [ ] Understand how RAII binds heap resource lifetimes to stack object lifetimes
> - [ ] Proficiently use `unique_ptr`, `shared_ptr`, `weak_ptr`, and their factory functions
> - [ ] Understand the existence and applicable scenarios of `placement new`

## Starting with new/delete

C++ replaces C's `malloc` and `free` with `new` and `delete`. Simply put, `new` is a wrapper around `malloc` plus a constructor call; `delete` first calls the destructor, then reclaims the memory. This distinction is the fundamental dividing line between C++ and C dynamic memory management.

When allocating a single object, for class types, `new` automatically calls the constructor, and `delete` automatically calls the destructor:

```cpp
class Sensor {
public:
    Sensor()  { std::cout << "Sensor 初始化\n"; }
    ~Sensor() { std::cout << "Sensor 关闭\n"; }
    void read() { std::cout << "读取数据\n"; }
};

Sensor* s = new Sensor();  // 输出: Sensor 初始化
s->read();                  // 输出: 读取数据
delete s;                   // 输出: Sensor 关闭
```

When allocating arrays, we must use `new[]`, and when freeing them, we must use the corresponding `delete[]`:

```cpp
int* arr = new int[10];
for (int i = 0; i < 10; ++i) {
    arr[i] = i * i;
}
delete[] arr;  // 注意：是 delete[]，不是 delete
```

> **Pitfall Warning**: Mismatching `delete` and `delete[]` is a classic mistake. Using `delete` to free an array allocated with `new[]` results in undefined behavior (UB). For basic types like `int`, some platforms might "happen" to work fine; but for arrays of class types, `delete` (without `[]`) only calls the destructor of the first element—the destructors of the remaining elements are never called. If those destructors are responsible for releasing nested dynamic memory, the consequence is a resource leak. Make this an ironclad rule: `new` goes with `delete`, and `new[]` goes with `delete[]`. It is better to type one extra `[]` than to rely on luck.

## Memory Leaks—The Silent Killer

Just how insidious are memory leaks? Consider the simplest scenario:

```cpp
void leak_example()
{
    int* p = new int(42);
    if (some_condition()) {
        return;  // 提前返回，delete 永远不会执行
    }
    delete p;
}
```

The function returns early via `return`, `delete` is skipped, and those 4 bytes of memory are lost forever. But an even more insidious scenario involves exceptions: if the code throws an exception between `new` and `delete`, the control flow jumps directly to the `catch` block, and `delete` is completely bypassed. Such leaks often go undetected during testing, but in production, a rare condition triggers an exception, and memory starts bleeding away bit by bit.

### Catching Leaks with AddressSanitizer

The good news is that modern compilers provide powerful runtime detection tools. AddressSanitizer (ASan) is a built-in memory error detector in GCC and Clang. By adding `-fsanitize=address` at compile time, we can automatically detect leaks, out-of-bounds accesses, use-after-free errors, and more.

```cpp
// leak_demo.cpp
// 编译: g++ -std=c++17 -O0 -fsanitize=address -g leak_demo.cpp
#include <iostream>

void create_leak()
{
    int* p = new int(42);
    std::cout << "分配了内存，值为: " << *p << "\n";
    // 故意不 delete
}

int main()
{
    create_leak();
    std::cout << "函数返回了，但内存没有释放\n";
    return 0;
}
```

After compiling and running, ASan reports at program exit:

```text
=================================================================
==12345==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 4 byte(s) in 1 object(s) allocated from:
    #0 0x401234 in operator new(unsigned long)
    #1 0x401156 in create_leak() leak_demo.cpp:7
    #2 0x401178 in main leak_demo.cpp:14

SUMMARY: AddressSanitizer: 4 byte(s) leaked in 1 allocation(s).
=================================================================
```

> **Pitfall Warning**: ASan significantly slows down program execution (typically 2–5 times slower) and increases memory usage (roughly 3–5 times more), so we should only use it during debugging and testing. Always remove `-fsanitize=address` in production builds. Additionally, ASan may conflict with certain parallel debugging tools. If you encounter strange segmentation faults, try disabling ASan to see if the tool itself is the culprit.

## RAII Binds Heap Resources to the Stack

The core problem with raw `new`/`delete` usage is that we must manually guarantee every block of memory is freed exactly once—whether through a normal return, an early `return`, or an exception exit. C++'s answer is RAII—Resource Acquisition Is Initialization. The core idea is to bind the lifetime of a heap resource to a stack object: `new` in the constructor, `delete` in the destructor, and rely on the automatic invocation of destructors when stack objects leave scope to guarantee release.

```cpp
class AutoInt {
public:
    explicit AutoInt(int value) : ptr_(new int(value)) {}
    ~AutoInt() {
        delete ptr_;
        std::cout << "AutoInt 析构，内存已释放\n";
    }

    // 禁止拷贝（后面会解释原因）
    AutoInt(const AutoInt&) = delete;
    AutoInt& operator=(const AutoInt&) = delete;

    int& operator*() { return *ptr_; }
private:
    int* ptr_;
};

void safe_function()
{
    AutoInt value(42);
    std::cout << *value << "\n";
    risky_operation();  // 即使这里抛出异常
    // 析构函数也会在栈展开时被自动调用
}
```

The destructor of `AutoInt` guarantees that `delete` will be executed—whether `safe_function` returns normally or exits due to an exception. In practice, however, we don't hand-write a `AutoXxx` wrapper class for every type. The standard library has already done this for us, and in a much more robust way. These are smart pointers.

## Smart Pointers—The Standard Answer to RAII

C++11 introduced three smart pointers, all defined in the `<memory>` header, each corresponding to different ownership semantics.

### unique_ptr—Exclusive Ownership

`std::unique_ptr` expresses "exclusive ownership": a block of memory can be held by only one `unique_ptr` at any given time. It is not copyable, but it is movable—ownership can be transferred from one `unique_ptr` to another via `std::move`:

```cpp
auto p = std::make_unique<int>(42);   // C++14 的 make_unique
std::cout << *p << "\n";              // 42

// auto p2 = p;                       // 编译错误！unique_ptr 不可拷贝
auto p2 = std::move(p);              // OK：所有权转移，p 变为 nullptr
std::cout << *p2 << "\n";            // 42
// 离开作用域，p2 析构，内存自动释放
```

`std::make_unique` (C++14) is safer than a direct `std::unique_ptr<int>(new int(42))`—it combines allocation and construction into a single, uninterruptible step, avoiding leaks in edge cases. For C++11 projects, we can directly write `std::unique_ptr<int>(new int(42))`.

`unique_ptr` also supports custom deleters and an array version. A custom deleter lets us execute custom operations when freeing memory, which is highly useful in embedded development—for example, returning memory to a memory pool instead of the standard heap:

```cpp
auto pool_deleter = [](int* p) {
    std::cout << "归还到内存池\n";
    ::operator delete(p);
};
std::unique_ptr<int, decltype(pool_deleter)> p(new int(42), pool_deleter);
// p 析构时，pool_deleter 被调用，而不是默认的 delete
```

The array version replaces `new[]`/`delete[]`: `auto arr = std::make_unique<int[]>(10);` automatically provides `operator[]`, and when it leaves scope, it automatically calls `delete[]`.

### shared_ptr—Shared Ownership

`std::shared_ptr` allows multiple pointers to share ownership of the same block of memory. Internally, it tracks this via a reference count—each copy increments the count, each destruction decrements it, and when the count reaches zero, the memory is automatically released.

```cpp
auto p1 = std::make_shared<int>(42);
std::cout << p1.use_count() << "\n";  // 1

auto p2 = p1;  // 拷贝，共享所有权
std::cout << p1.use_count() << "\n";  // 2

{
    auto p3 = p1;
    std::cout << p1.use_count() << "\n";  // 3
}  // p3 析构，计数减为 2

std::cout << p1.use_count() << "\n";  // 2
// p1 和 p2 离开作用域后，计数归零，内存释放
```

`std::make_shared` is more efficient than `std::shared_ptr<int>(new int(42))`—it requires only a single allocation to allocate both the control block and the object itself, whereas the latter requires two. Unless we need a custom deleter, we should prefer it.

> **Pitfall Warning**: The reference counting of `shared_ptr` is itself thread-safe (atomic operations), but concurrent access to the pointed-to object is not—multiple threads simultaneously reading and writing to `*p` still constitutes a data race. Furthermore, `shared_ptr` incurs performance overhead: the memory overhead of the control block, the atomic operation overhead of reference counting, and potential cache unfriendliness caused by the object and control block not residing on the same cache line. If our ownership semantics are exclusive, we should use `unique_ptr` and not abuse `shared_ptr` "for safety."

### weak_ptr—Breaking Circular References

`shared_ptr` has a classic trap: circular references. If object A holds a `shared_ptr` pointing to B, and object B holds a `shared_ptr` pointing to A, the reference counts of both will never reach zero, and the memory will never be freed.

`std::weak_ptr` exists to solve this problem. It acts as an "observer"—it can be constructed from a `shared_ptr` but does not increase the reference count. To access the object pointed to by a `weak_ptr`, we must first call `lock()` to promote it to a `shared_ptr`:

```cpp
struct Node {
    std::shared_ptr<Node> next;
    std::weak_ptr<Node> prev;  // 用 weak_ptr 打破循环
    int value;
    explicit Node(int v) : value(v) {}
    ~Node() { std::cout << "Node(" << value << ") 析构\n"; }
};

auto n1 = std::make_shared<Node>(1);
auto n2 = std::make_shared<Node>(2);
n1->next = n2;       // n2 的引用计数变为 2
n2->prev = n1;       // n1 的引用计数不变（weak_ptr 不增加计数）

// 通过 weak_ptr 访问前驱节点
if (auto locked = n2->prev.lock()) {
    std::cout << "前驱节点值: " << locked->value << "\n";  // 1
}
// n1、n2 正常析构，没有泄漏
```

If `prev` were also a `shared_ptr`, `n1` and `n2` would form a circular reference—even if the external `n1` and `n2` leave scope, the `shared_ptr` they hold on each other would keep the reference count at 1, and they would never be destroyed. After switching to `weak_ptr`, the cycle is broken, and both nodes can be properly released.

## placement new—Constructing Objects at a Specified Address

A normal `new` automatically finds memory on the heap, whereas `placement new` says "you specify the address, I only call the constructor." The memory allocation is entirely our own responsibility.

```cpp
#include <new>  // placement new 需要这个头文件

alignas(int) unsigned char buffer[sizeof(int)];
int* p = new (buffer) int(42);  // 在 buffer 上构造一个 int
std::cout << *p << "\n";        // 42

// 不能用 delete！因为内存不是 new 分配的
p->~int();  // 显式调用析构函数（对于 int 是空操作）
```

`placement new` is not heavily used in application development, but it is highly valuable in embedded systems—it allows us to construct C++ objects in pre-allocated memory pools or shared memory. Note three things: the buffer alignment must satisfy the object's requirements (`alignas` guarantees this); since the memory was not allocated by `new`, we cannot call `delete`, we must explicitly call the destructor; explicitly calling a destructor is extremely rare in C++ and almost exclusively appears in this scenario.

## Hands-on Practice—Raw Pointers vs. Smart Pointers

Let's integrate the previous content into a complete example—comparing raw pointers, smart pointers, and custom deleters.

```cpp
// dynamic.cpp
// 编译（泄漏检测）:
//   g++ -std=c++17 -O0 -fsanitize=address -g dynamic.cpp -o dynamic
// 编译（正常）:
//   g++ -std=c++17 -O0 -g dynamic.cpp -o dynamic

#include <iostream>
#include <memory>

void raw_pointer_demo()
{
    std::cout << "=== 裸指针版本 ===\n";
    int* p = new int(42);
    std::cout << "值: " << *p << "\n";

    int* arr = new int[5];
    for (int i = 0; i < 5; ++i) { arr[i] = i * 10; }

    // 模拟提前返回（取消注释以观察泄漏）:
    // if (true) return;

    delete p;
    delete[] arr;
    std::cout << "手动释放完成\n";
}

void smart_pointer_demo()
{
    std::cout << "\n=== 智能指针版本 ===\n";
    auto p = std::make_unique<int>(42);
    std::cout << "值: " << *p << "\n";
    auto arr = std::make_unique<int[]>(5);
    for (int i = 0; i < 5; ++i) { arr[i] = i * 10; }
    // 不管以何种方式离开（正常返回、提前 return、异常）
    // 析构函数都会自动释放内存
    std::cout << "离开作用域时自动释放\n";
}

void custom_deleter_demo()
{
    std::cout << "\n=== 自定义删除器 ===\n";
    auto deleter = [](int* ptr) {
        std::cout << "自定义删除器被调用，值为: " << *ptr << "\n";
        delete ptr;
    };
    std::unique_ptr<int, decltype(deleter)> p(new int(99), deleter);
    std::cout << "值: " << *p << "\n";
}

int main()
{
    raw_pointer_demo();
    smart_pointer_demo();
    custom_deleter_demo();
    std::cout << "\n程序结束\n";
    return 0;
}
```

Compiling and running normally produces the following output:

```text
=== 裸指针版本 ===
值: 42
手动释放完成

=== 智能指针版本 ===
值: 42
离开作用域时自动释放

=== 自定义删除器 ===
值: 99
自定义删除器被调用，值为: 99

程序结束
```

If we uncomment the early return in `raw_pointer_demo`, ASan will report two leak points totaling 24 bytes. Meanwhile, `smart_pointer_demo` will never leak, no matter what—this is the peace of mind that RAII provides.

## Exercises

### Exercise 1: Convert Raw Pointers to Smart Pointers

Rewrite the following code using smart pointers: use `unique_ptr` for individual objects, and `shared_ptr` for shared objects.

```cpp
class Logger {
public:
    explicit Logger(const std::string& name) : name_(name) {}
    ~Logger() { std::cout << "Logger(" << name_ << ") 析构\n"; }
    void log(const std::string& msg) { std::cout << "[" << name_ << "] " << msg << "\n"; }
private:
    std::string name_;
};

int main()
{
    Logger* logger = new Logger("app");
    logger->log("程序启动");
    Logger* backup = logger;  // 别名，不拥有
    delete logger;
    // backup 此刻是悬空指针！
    return 0;
}
```

### Exercise 2: Implement a Simple Memory Pool with a Custom Deleter

Implement a fixed-size memory pool class that uses `unique_ptr` with a custom deleter to manage objects allocated from the pool. Hint: a deleter does not have to `delete`; it can call `pool.deallocate()` to return memory.

## Summary

In this chapter, we started with `new`/`delete` and walked through a complete cognitive path. The problem with raw `new`/`delete` usage is not syntactic complexity, but rather that we must guarantee `delete` is correctly executed on every possible exit path—normal returns, early `return`, and exception exits. Every omission is a potential memory leak. RAII fundamentally solves this problem by binding the lifetime of heap resources to stack objects.

`unique_ptr` is the default choice—zero-overhead, exclusive ownership, non-copyable but movable. `shared_ptr` is for scenarios that genuinely require shared ownership, but we must be mindful of reference counting overhead and circular references. `weak_ptr` is a sharp tool for breaking circular references; it observes but does not own. `make_unique` and `make_shared` are the preferred ways to create smart pointers. AddressSanitizer is a powerful tool for detecting memory issues and should always be enabled during development and testing.

Now that we have mastered dynamic memory management, our next step is to dive into a related topic—memory alignment and padding. Why does `sizeof`ing a struct with only a few fields always result in more bytes than manually summing the sizes of those fields? The answer lies hidden within the alignment rules.
