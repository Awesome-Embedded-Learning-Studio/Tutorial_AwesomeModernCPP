---
chapter: 12
cpp_standard:
- 11
- 14
- 17
- 20
description: Master new/delete usage and its pitfalls, and understand why RAII sits at the core.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- Memory Layout
reading_time_minutes: 13
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Dynamic Memory Management
translation:
  source: documents/vol1-fundamentals/ch12/02-new-delete.md
  source_hash: 3d217029d46ac5838f94b2c488545dbb7ec7002936724c0a1d1e9d29f6416f2b
  translated_at: '2026-09-25T12:09:48+00:00'
  engine: anthropic
  token_count: 2500
---
# Dynamic Memory Management: What Goes On Behind new and delete

In the previous chapter we split the program's memory space into four major areas—stack, heap, static storage, and the code segment—and worked out where data "lives" and how long it "survives." But we left one thread hanging: how exactly is dynamic memory on the heap managed? What do `new` and `delete` actually do behind the scenes? And why has nearly every earlier chapter kept nagging us to "use smart pointers, don't write bare `delete`s"?

This chapter answers those questions head-on. Dynamic memory is the greatest freedom C++ grants us: at runtime we can request memory of any size on demand, entirely unconstrained by stack-space limits. But that freedom carries the heaviest responsibility: every block obtained with `new` must be correctly `delete`d, or it leaks; every `delete` must correspond to the right kind of `new`, or it is undefined behavior.

## Starting with new/delete

Let's look at how C++ replaces C's `malloc` and `free` with `new` and `delete`. Simply put, `new` wraps `malloc` plus a constructor call, while `delete` first invokes the destructor and then reclaims the memory. That difference is precisely the fundamental watershed between C++ and C dynamic memory management.

When we allocate a single object of a class type, `new` automatically calls the constructor, and `delete` automatically calls the destructor:

```cpp
class Sensor {
public:
    Sensor()  { std::cout << "Sensor 初始化\n"; }
    ~Sensor() { std::cout << "Sensor 关闭\n"; }
    void read() { std::cout << "读取数据\n"; }
};

Sensor* s = new Sensor();  // Output: Sensor 初始化
s->read();                  // Output: 读取数据
delete s;                   // Output: Sensor 关闭
```

When allocating an array we must use `new[]`, and when releasing it we must use the matching `delete[]`:

```cpp
int* arr = new int[10];
for (int i = 0; i < 10; ++i) {
    arr[i] = i * i;
}
delete[] arr;  // Note: this is delete[], not delete
```

A mismatched `delete`/`delete[]` is the most classic error of all. Releasing an array allocated with `new[]` using plain `delete` is undefined behavior. For fundamental types like `int`, some platforms might "get lucky" and show no problem; but for an array of class types, `delete` (without `[]`) invokes the destructor of only the first element—the destructors of the remaining elements are never called at all—and if those destructors were responsible for releasing nested dynamic memory, the consequence is a resource leak. Burn in an ironclad habit: `new` pairs with `delete`, `new[]` pairs with `delete[]`; better to type one extra `[]` than to gamble on luck.

## Memory Leaks: The Silent Killer

Just how insidious is a memory leak? Take the simplest scenario:

```cpp
void leak_example()
{
    int* p = new int(42);
    if (some_condition()) {
        return;  // Early return; the delete never runs
    }
    delete p;
}
```

The function returns midway, the `delete` is skipped, and those 4 bytes are lost forever. But the more insidious scenario is exceptions: code between the `new` and the `delete` throws, control flow jumps straight to the `catch` block, and the `delete` is bypassed entirely. This kind of leak often stays hidden during testing, but in production, once some rare condition triggers the exception, memory starts draining away bit by bit.

### Catching Leaks with AddressSanitizer

The good news is that modern compilers give us powerful runtime detection tools. AddressSanitizer (ASan) is the memory error detector built into GCC and Clang; compile with `-fsanitize=address` and it automatically detects leaks, out-of-bounds accesses, use-after-free, and similar problems.

```cpp
// leak_demo.cpp
// Compile: g++ -std=c++17 -O0 -fsanitize=address -g leak_demo.cpp
#include <iostream>

void create_leak()
{
    int* p = new int(42);
    std::cout << "分配了内存，值为: " << *p << "\n";
    // Intentionally no delete
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

ASan noticeably slows programs down (typically 2-5x) and inflates memory usage (roughly 3-5x), so use it only during debugging and testing. Always strip `-fsanitize=address` from production builds. Also, ASan can conflict with some parallel debugging tools; when you run into a strange segmentation fault, try disabling ASan to see whether the tool itself is the culprit.

## RAII: Tying Heap Resources to the Stack

The core problem with using raw `new`/`delete` is this: by hand, we must guarantee that every block of memory is released exactly once—whether we leave via a normal return, an early `return`, or an exception. C++'s answer is RAII—Resource Acquisition Is Initialization. The core idea is to bind the lifetime of a heap resource to a stack object: `new` in the constructor, `delete` in the destructor, leaning on the fact that a stack object's destructor is invoked automatically when it leaves its scope to guarantee the release.

```cpp
class AutoInt {
public:
    explicit AutoInt(int value) : ptr_(new int(value)) {}
    ~AutoInt() {
        delete ptr_;
        std::cout << "AutoInt 析构，内存已释放\n";
    }

    // Copying is disabled (the reason comes later)
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
    risky_operation();  // Even if this throws
    // the destructor is still invoked automatically during stack unwinding
}
```

`AutoInt`'s destructor guarantees the `delete` is executed, no matter whether `safe_function` returns normally or exits because of an exception. In practice, though, we don't hand-write an `AutoXxx` wrapper for every type—the standard library has already done it for us, and done it more thoroughly. Those are smart pointers.

## Smart Pointers: RAII's Standard Answer

C++11 introduced three smart pointers, all defined in the `<memory>` header, each matching a different ownership semantics.

### unique_ptr: Exclusive Ownership

`std::unique_ptr` expresses sole ownership: at any given moment, a block of memory can be held by only one `unique_ptr`. It is not copyable, but it is movable—`std::move` transfers ownership from one `unique_ptr` to another:

```cpp
auto p = std::make_unique<int>(42);   // C++14's make_unique
std::cout << *p << "\n";              // 42

// auto p2 = p;                       // Compile error! unique_ptr is not copyable
auto p2 = std::move(p);              // OK: ownership transferred, p becomes nullptr
std::cout << *p2 << "\n";            // 42
// At scope exit, p2 is destroyed and the memory is released automatically
```

`std::make_unique` (C++14) is safer than writing `std::unique_ptr<int>(new int(42))` directly: it folds allocation and construction into one non-interruptible step, avoiding leaks in edge cases. In a C++11 project, `std::unique_ptr<int>(new int(42))` is fine to write directly.

`unique_ptr` also supports custom deleters and an array version. A custom deleter lets us run our own actions when the memory is released—extremely useful in embedded development, for example returning memory to a memory pool instead of the standard heap:

```cpp
auto pool_deleter = [](int* p) {
    std::cout << "归还到内存池\n";
    ::operator delete(p);
};
std::unique_ptr<int, decltype(pool_deleter)> p(new int(42), pool_deleter);
// When p is destroyed, pool_deleter is invoked instead of the default delete
```

The array version, in turn, replaces `new[]`/`delete[]`: `auto arr = std::make_unique<int[]>(10);` automatically provides `operator[]`, and calls `delete[]` automatically when it leaves its scope.

### shared_ptr: Shared Ownership

`std::shared_ptr` allows multiple pointers to share ownership of the same memory. It tracks this internally with a reference count: each copy increments it, each destruction decrements it, and when the count reaches zero the memory is released automatically.

```cpp
auto p1 = std::make_shared<int>(42);
std::cout << p1.use_count() << "\n";  // 1

auto p2 = p1;  // Copy; ownership is shared
std::cout << p1.use_count() << "\n";  // 2

{
    auto p3 = p1;
    std::cout << p1.use_count() << "\n";  // 3
}  // p3 is destroyed; the count drops to 2

std::cout << p1.use_count() << "\n";  // 2
// After p1 and p2 leave their scope, the count reaches zero and the memory is released
```

`std::make_shared` is more efficient than `std::shared_ptr<int>(new int(42))`: a single allocation covers both the control block and the object itself, whereas the latter needs two. Prefer it unless you need a custom deleter.

`shared_ptr`'s reference count is itself thread-safe (atomic operations), but concurrent access to the pointed-to object is not: multiple threads reading and writing `*p` at the same time is still a data race. `shared_ptr` also carries performance costs: the memory overhead of the control block, the cost of atomic reference-count operations, and cache unfriendliness when the object and the control block may not sit on the same cache line. If your ownership semantics are exclusive, use `unique_ptr`—don't reach for `shared_ptr` "just to be safe".

### weak_ptr: Breaking Circular References

`shared_ptr` has one classic trap: circular references. Object A holds a `shared_ptr` to B, object B holds a `shared_ptr` to A, neither reference count ever reaches zero, and the memory is never freed.

`std::weak_ptr` exists to solve exactly this problem. It is an "observer": it can be constructed from a `shared_ptr`, but it does not increment the reference count. To access the object a `weak_ptr` points to, we must first call `lock()` to promote it to a `shared_ptr`:

```cpp
struct Node {
    std::shared_ptr<Node> next;
    std::weak_ptr<Node> prev;  // weak_ptr breaks the cycle
    int value;
    explicit Node(int v) : value(v) {}
    ~Node() { std::cout << "Node(" << value << ") 析构\n"; }
};

auto n1 = std::make_shared<Node>(1);
auto n2 = std::make_shared<Node>(2);
n1->next = n2;       // n2's reference count becomes 2
n2->prev = n1;       // n1's reference count is unchanged (weak_ptr doesn't increment it)

// Access the predecessor through the weak_ptr
if (auto locked = n2->prev.lock()) {
    std::cout << "前驱节点值: " << locked->value << "\n";  // 1
}
// n1 and n2 are destroyed normally; no leak
```

If `prev` were also a `shared_ptr`, `n1` and `n2` would form a circular reference—even after the outer `n1` and `n2` leave their scope, the `shared_ptr`s they hold for each other would keep both reference counts stuck at 1, so they would never be destroyed. Swapping in `weak_ptr` breaks the cycle, and both nodes are released normally.

## placement new: Constructing an Object at a Given Address

Plain `new` automatically finds memory on the heap; `placement new` instead says, "you pick the address, I'll just call the constructor." Allocating the memory is entirely your own responsibility.

```cpp
#include <new>  // placement new requires this header

alignas(int) unsigned char buffer[sizeof(int)];
int* p = new (buffer) int(42);  // Construct an int on buffer
std::cout << *p << "\n";        // 42

// No delete here! The memory was not allocated by new
p->~int();  // Explicit destructor call (a no-op for int)
```

`placement new` sees little use in host-side development, but in embedded systems it is highly valuable: it lets us construct C++ objects inside pre-allocated memory pools or shared memory. Note three things: the buffer's alignment must satisfy the object's requirement (`alignas` guarantees that here); the memory was not allocated by `new`, so no `delete` may be called—only an explicit destructor call; and explicitly calling a destructor is extremely rare in C++, appearing almost exclusively in this scenario.

## Hands-On: Raw Pointers vs Smart Pointers

Let's fold everything above into one complete example: a comparison of raw pointers, smart pointers, and custom deleters.

```cpp
// dynamic.cpp
// Compile (with leak detection):
//   g++ -std=c++17 -O0 -fsanitize=address -g dynamic.cpp -o dynamic
// Compile (normal):
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

    // Simulate an early return (uncomment to observe the leak):
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
    // No matter how we leave (normal return, early return, exception),
    // the destructor releases the memory automatically
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

Compiled and run normally, the output is:

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

Uncomment the early return inside `raw_pointer_demo`, and ASan reports two leaks totaling 24 bytes. `smart_pointer_demo`, on the other hand, cannot leak no matter what happens—that is the peace of mind RAII gives you.

## Exercises

### Exercise 1: Convert Raw Pointers to Smart Pointers

Rewrite the following code as a smart-pointer version: `unique_ptr` for the single object, `shared_ptr` for shared ownership.

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
    Logger* backup = logger;  // An alias; does not own
    delete logger;
    // backup is now a dangling pointer!
    return 0;
}
```

### Exercise 2: A Simple Memory Pool with a Custom Deleter

Implement a fixed-size memory pool class that manages objects allocated from the pool using `unique_ptr` combined with a custom deleter. Hint: the deleter does not have to `delete`—it can call `pool.deallocate()` to hand the memory back.
