---
chapter: 12
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the use of new/delete and its pitfalls, and understand why RAII sits at the core
difficulty: intermediate
order: 2
platform: host
prerequisites:
- Memory Layout
reading_time_minutes: 10
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Dynamic Memory Management
translation:
  source: documents/vol1-fundamentals/ch12/02-new-delete.md
  source_hash: ef5aa736b3a67837e296660a056e058c8a5697a61216f748f9162f762bbc4b15
  translated_at: '2026-09-27T04:12:46+00:00'
  engine: anthropic
  token_count: 6500
---
# Dynamic Memory Management: What Goes On Behind new and delete

In the previous chapter we split the program's memory space into four major areas—stack, heap, static storage, and the code segment—and worked out where data "lives" and how long it "survives". But one loose end was left hanging: how is dynamic memory on the heap actually managed? What do `new` and `delete` do behind the scenes? And why have almost all the earlier chapters kept nagging us to "use smart pointers, don't write bare `delete`s"?

This chapter answers those questions head-on. Dynamic memory is the greatest freedom C++ grants us: at runtime we can request memory of any size on demand, completely unconstrained by stack-space limits. But that freedom also brings the heaviest responsibility: every block of memory obtained with `new` must be correctly `delete`d, or it is a leak; every `delete` must correspond to the correct `new`, or it is undefined behavior.

## Starting with new and delete

Let's look at how C++ took over from C's `malloc` and `free` with `new` and `delete`. `new` does two things: first it finds a block of memory (under the hood it usually goes through `malloc`), and then **calls the constructor on it**; `delete` works the other way around: **it calls the destructor first, then hands the memory back**.

> You may crudely think of it—**crudely, I said!**—as: `new` is the combination **`malloc + T()`**, and `delete` is the combination **`~T() + free()`**.
> The finer distinctions were, I recall, explained at length by language-lawyer types; since this is a beginner's introduction, we'll skip them here—scaring off the newcomers won't do.
>
> Q: A fun topic to slip in here: does the memory that comes out of `new` need to be checked against NULL?
>
> A: One glance and you can tell this question comes from a C background—because I asked it myself once. A failed `new` does not return a null pointer; its choice is to hurl a rotten egg named `std::bad_alloc` straight at you—catch it, you rascal! That `bad_alloc` is exactly what we just learned in the exceptions chapter, and here it already comes into play. Ha!

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

When we allocate an array we must use `new[]`, and to free it we must use the matching `delete[]`:

```cpp
int* arr = new int[10];
for (int i = 0; i < 10; ++i) {
    arr[i] = i * i;
}
delete[] arr;  // Note: it is delete[], not delete
```

Mismatching `delete` and `delete[]` is a classic among classic errors. Using `delete` to free an array allocated with `new[]` is undefined behavior. For fundamental types like `int`, some platforms may "get away with it" by luck; but for an array of class types, `delete` (without `[]`) calls only the first element's destructor—the destructors of all the other elements simply never run, and if those destructors are responsible for releasing nested dynamic memory, the consequence is a resource leak. We should build an unshakable habit: `new` pairs with `delete`, `new[]` pairs with `delete[]`; better to type one extra `[]` than to trust your luck.

## Memory Leaks — the Failure That Reports No Error

Just how insidious is a memory leak? Let's look at the simplest possible scenario:

```cpp
void leak_example()
{
    int* p = new int(42);
    if (some_condition()) {
        return;  // Early return: the delete never runs
    }
    delete p;
}
```

Watch the function `return` midway: the `delete` gets skipped, and those 4 bytes are lost forever. But the sneakier scenario is exceptions: the code throws somewhere between the `new` and the `delete`, control flow jumps straight to the `catch` block, and the `delete` is bypassed entirely. Leaks like this often stay hidden during testing, but in production, once some rare condition triggers the exception, memory starts draining away bit by bit.

### Catching Leaks with AddressSanitizer

The good news is that modern compilers hand us powerful runtime detection tools. AddressSanitizer (ASan) is a memory-error detector built into GCC and Clang; add `-fsanitize=address` at compile time and it automatically catches leaks, out-of-bounds accesses, use-after-free, and the like.

```cpp
// leak_demo.cpp
// Compile: g++ -std=c++17 -O0 -fsanitize=address -g leak_demo.cpp
#include <iostream>

void create_leak()
{
    int* p = new int(42);
    std::cout << "分配了内存，值为: " << *p << "\n";
    // Deliberately no delete
}

int main()
{
    create_leak();
    std::cout << "函数返回了，但内存没有释放\n";
    return 0;
}
```

After we compile and run, ASan reports at program exit:

```text
=================================================================
==120445==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 4 byte(s) in 1 object(s) allocated from:
    #0 0x6ffc2d12d2a1 in operator new(unsigned long) (/usr/lib/libasan.so.8+0x12d2a1)
    #1 0x55f4a03ff1da in create_leak() /tmp/leak_demo.cpp:7
    #2 0x55f4a03ff2b6 in main /tmp/leak_demo.cpp:14
    #3 0x6ffc2c827780  (/usr/lib/libc.so.6+0x27780)
    #4 0x6ffc2c8278b8  __libc_start_main (/usr/lib/libc.so.6+0x278b8)
    #5 0x55f4a03ff0f4  _start (/tmp/leak_asan+0x10f4)

SUMMARY: AddressSanitizer: 4 byte(s) leaked in 1 allocation(s).
=================================================================
```

`==120445==` is the process ID; the addresses and paths will certainly differ on your machine, and they change from run to run. What you actually want to read are the clues like `leak_demo.cpp:7`: which line allocated the memory, and where it was called from—the stack frames, read top to bottom, are the call chain.

ASan noticeably slows the program down (typically 2-5x) and inflates memory usage (roughly 3-5x), so it should only be used during debugging and testing. Always strip `-fsanitize=address` from production builds. Also, ASan can conflict with some parallel debugging tools; when we run into a weird segfault, try dropping ASan to see whether the tooling itself is the problem.

MSVC folks? Save yourself the trouble—it isn't supported there. If you want in, turn left and consider clang-cl (side-eye smirk).

## RAII: Binding Heap Resources to the Stack

The core problem with using raw `new`/`delete` is that we must manually guarantee every block of memory is released exactly once—on a normal return, an early `return`, or an exceptional exit alike. C++'s answer is RAII—Resource Acquisition Is Initialization. The core idea is to bind the lifetime of a heap resource to a stack object: `new` in the constructor, `delete` in the destructor, leaning on the mechanism that destructors run automatically when a stack object leaves its scope to guarantee the release.

```cpp
class AutoInt {
public:
    explicit AutoInt(int value) : ptr_(new int(value)) {}
    ~AutoInt() {
        delete ptr_;
        std::cout << "AutoInt 析构，内存已释放\n";
    }

    // Copying forbidden (why, we explain later)
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

`AutoInt`'s destructor guarantees the `delete` will execute, no matter whether `safe_function` returns normally or exits because of an exception. In real projects, though, we don't hand-write an `AutoXxx` wrapper class for every type—the standard library has already done it for us, and done it more thoroughly. These are smart pointers.

## Smart Pointers — RAII the Standard Library Wrote for Us

In real life we don't hand-write an `AutoXxx` wrapper for every type—the standard library long ago stocked `<memory>` with ready-made ones: `std::unique_ptr` for exclusive ownership (no copying, moving only), `std::shared_ptr` for shared ownership (reference counting: copy increments, destruction decrements, release at zero), and `std::weak_ptr` for observing without owning, the specialist at breaking circular references. The most commonly used, `unique_ptr`, looks like this:

```cpp
auto p = std::make_unique<int>(42);   // make_unique, from C++14
std::cout << *p << "\n";              // 42
// Leaving the scope, p is destroyed and delete runs automatically
```

The `new` and `delete` are both gone—that is the style RAII buys us. Each of these three pointers has plenty of depth to it: where the zero overhead comes from, what the control block looks like, how circular references get broken, what you can do with custom deleters. We won't unpack all that in this volume; Chapter 1 of Volume 2 spends six full articles dissecting them. For now, one rule of conduct is enough: **if `unique_ptr` can be used, don't write a bare `new`; if `make_*` can be used, don't write `new` at all**.

## Placement new — Constructing an Object at a Chosen Address

Ordinary `new` automatically finds memory on the heap, whereas `placement new` is responsible only for calling the constructor—the address is entirely up to you.

```cpp
#include <new>  // placement new requires this header

alignas(int) unsigned char buffer[sizeof(int)];
int* p = new (buffer) int(42);  // Construct an int in buffer
std::cout << *p << "\n";        // 42

// No delete allowed! This memory was not allocated by new
p->~int();  // Explicit destructor call (a no-op for int)
```

`placement new` doesn't come up much in host-side development, but in embedded systems it is extremely valuable: it lets us construct C++ objects inside preallocated memory pools or shared memory. Three things to watch when using it. The buffer alignment must satisfy the object's requirement—`alignas` takes care of that. The memory did not come from `new`, so `delete` must not be called; only an explicit destructor call will do. And explicitly calling a destructor is something exceedingly rare in C++—it shows up almost exclusively in this scenario.

## Hands-On — Raw Pointers vs Smart Pointers

Let's fold the preceding material into one complete example: a comparison of raw pointers, smart pointers, and a custom deleter. How that deleter in the last part works internally gets dissected in Volume 2; here we just observe the behavior.

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
    // No matter how we leave (normal return, early return, exception)
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

The complete code sits below—hit "Try it" and run it directly (the compile options are already set to -O0):

<OnlineCompilerDemo
  title="Hands-On: dynamic.cpp"
  source-path="code/examples/vol1/28_new_delete.cpp"
  description="Run the three-part comparison of raw pointers, smart pointers, and a custom deleter online. Want to see ASan report leaks? Uncomment the early return and add -fsanitize=address to the compile options."
  run-options="-O0 -std=c++17"
  allow-run
/>

If we uncomment the early return inside `raw_pointer_demo`, ASan reports two leak sites totaling 24 bytes. `smart_pointer_demo`, on the other hand, cannot leak no matter what happens—that is the peace of mind RAII gives.

## Exercises

### Exercise 1: Switch to unique_ptr, Then Get Stuck

Please convert the code below to `std::unique_ptr`, creating `logger` with `make_unique`. When you reach the `backup` line you will get stuck—`unique_ptr` is not copyable, and that is exactly it pressing the question on your behalf: does `backup` actually have ownership? If not, how should the object be accessed? The answer is revealed in Volume 2; feel free to carry the question there for now.

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
    Logger* backup = logger;  // An alias, owns nothing
    delete logger;
    // backup is a dangling pointer at this moment!
    return 0;
}
```

### Exercise 2: Replace AutoInt with unique_ptr

Please replace the hand-written `AutoInt` in the earlier `safe_function` with `std::unique_ptr`: keep the behavior identical—how many lines did you save? Then change `risky_operation()` into something that really throws, and verify that the destructor is still invoked.
