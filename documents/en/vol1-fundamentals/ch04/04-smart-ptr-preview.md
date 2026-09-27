---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: Learn why smart pointers are needed, get a first look at how unique_ptr
  manages memory automatically, and set the stage for a deeper dive in Volume Two.
difficulty: beginner
order: 4
platform: host
prerequisites:
- References
reading_time_minutes: 10
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Smart Pointer Preview
translation:
  source: documents/vol1-fundamentals/ch04/04-smart-ptr-preview.md
  source_hash: 1146842cae17703a4869553547f7cf3190da266ef75747ee9cdb10944b54f398
  translated_at: '2026-09-25T10:38:42+00:00'
  engine: anthropic
  token_count: 5200
---
# Smart Pointer Preview: Stop Keeping Track of delete by Hand

By now we have spent several chapters dealing with raw pointers. Pointers are indeed powerful, but indeed dangerous too—every time we `new` a block of memory, we have to keep reminding ourselves to `delete` it, and any path that slips through means a memory leak. Modern C++ offers a systematic solution: **smart pointers**. In this chapter we won't go deep; we'll just walk you through what problems they solve and what their basic usage looks like. The full treatment comes in Volume Two, where we expand on them systematically alongside move semantics and RAII.

## The Three Classic Problems of Raw Pointers

We need to talk about the three classic problems of raw pointers in memory management (writing them down feels like filing an indictment).

**Memory leaks** are the most common case: we `new` but forget to `delete`. What's even more dangerous is forgetting on an early-exit path—in the normal flow `delete[]` does get executed, but once an error condition triggers and the function returns early, that memory is never coming back. (Ugh, my head already hurts.)

```cpp
void process_data()
{
    int* data = new int[1000];

    if (some_error_condition()) {
        return;  // Returned right here — where's the delete???
    }

    delete[] data;
}
```

> The key point here: **every line of code that can exit early (a return, a throw) is a potential leak point**. In a function with a dozen exits, we need to make sure the resource is properly released before every single one of them. The day someone adds a new return and forgets the delete, it leaks again.

**Double free** crashes the program outright—two pointers aim at the same memory, and each one `delete`s it once. The runtime usually reports `double free or corruption`, and this is especially easy to run into in a multi-person project.

**Dangling pointers** mean continuing to access memory through the original pointer after `delete`. This is the nastiest bug of the bunch: during development it may not show up at all (the freshly `delete`d memory often hasn't been overwritten yet, so `*p` happens to still read the original value), but once it reaches production and runs long enough, random problems start popping up—and tracking them down is pure misery.

## RAII: Acquire in the Constructor, Release in the Destructor

Look at all three problems together and the root cause is the same: **the acquisition and the release of the resource are scattered across different places in the code**. The core idea that fixes this is called **RAII (Resource Acquisition Is Initialization)**—acquire the resource in the constructor, release it in the destructor. C++ guarantees that when an object leaves its scope, the destructor **will be invoked**, whether the exit is normal or exceptional; this guarantee is provided by **stack unwinding**.

```cpp
#include <iostream>

struct IntHolder
{
    int* ptr;

    explicit IntHolder(int val) : ptr(new int(val))
    {
        std::cout << "分配内存，值 = " << *ptr << "\n";
    }

    ~IntHolder()
    {
        std::cout << "释放内存，值 = " << *ptr << "\n";
        delete ptr;
    }
};

void demo()
{
    IntHolder holder(42);
    std::cout << "内部值: " << *holder.ptr << "\n";
    if (true) {
        return;  // Even with an early return, holder's destructor still runs
    }
}
```

Output:

```text
分配内存，值 = 42
内部值: 42
释放内存，值 = 42
```

Even though the function returned early, `holder`'s destructor was still called. That is the power of RAII: we don't need to hand-write `delete` at every exit; C++'s scope rules manage it for us automatically.

> Note the `explicit` keyword: it forbids implicit conversions like `IntHolder holder = 42;`. Adding `explicit` to single-argument constructors is a good habit.

## unique_ptr: A Smart Pointer with Exclusive Ownership

Once RAII makes sense, smart pointers are easy to understand—they are simply tool classes that wrap `new` and `delete` into RAII for us. The most fundamental and most commonly used one is `std::unique_ptr`, whose core semantic is **exclusive ownership**: a block of memory can be held by exactly one `unique_ptr` at a time. It cannot be copied, but it can be **moved**.

### Creation and Basic Operations

C++14 introduced `std::make_unique`, which is the recommended way to create a `unique_ptr`. Let's use a custom type to walk through the complete lifecycle:

```cpp
#include <iostream>
#include <memory>
#include <string>

struct Player
{
    std::string name;
    int level;

    Player(const std::string& n, int lv) : name(n), level(lv)
    {
        std::cout << name << " 登场！\n";
    }

    ~Player() { std::cout << name << " 退场。\n"; }

    void show_status() const
    {
        std::cout << name << " Lv." << level << "\n";
    }
};

int main()
{
    {
        auto hero = std::make_unique<Player>("Alice", 5);
        hero->show_status();   // -> accesses members, same as a raw pointer
        std::cout << (*hero).name << "\n";  // * dereferences too
    }
    // hero leaves scope here; delete happens automatically

    std::cout << "继续执行...\n";
    return 0;
}
```

Output:

```text
Alice 登场！
Alice Lv.5
Alice
Alice 退场。
继续执行...
```

We can see that "Alice 退场。" appears before "继续执行..."—the destructor was invoked automatically when the curly-brace scope ended. `unique_ptr` has just three basic operations: `*p` to dereference, `p->member` to access a member, and `p.get()` to grab the raw pointer (useful when passing it to a C interface).

> Why do we recommend `make_unique` over `unique_ptr<int>(new int(42))`? First, it's cleaner—no need to write `new`. Second, writing `new` directly can leak when function arguments are combined, due to unspecified evaluation order; we'll unpack that detail in Volume Two.

### No Copying, Only Moving

`unique_ptr` **cannot be copied**—`auto p2 = p1;` is a straight compile error. That is a deliberate design: allowing copies would mean two `unique_ptr`s pointing at the same memory, and both would delete it on leaving scope. If you need to transfer ownership, use `std::move`:

```cpp
auto p1 = std::make_unique<int>(42);
auto p2 = std::move(p1);  // Ownership moves from p1 to p2
// p1 becomes nullptr; p2 now owns that memory
```

The detailed mechanics of `std::move` will be covered systematically in Volume Two. For now, we only need to remember that it is the standard way to transfer ownership of a `unique_ptr`.

### Zero Overhead: Safety at No Performance Cost

At runtime, `unique_ptr` carries **no extra performance overhead**—it stores exactly one pointer inside, has no virtual functions, and after compiler optimization the generated code is nearly identical to manual `new/delete`. Remember one clear rule: **whenever you can use `unique_ptr`, don't use raw `new/delete`**.

## In Practice: Raw Pointers vs unique_ptr

Let's implement the memory leak scenario both ways. The core contrast is plain to see: the raw-pointer version leaks on the error path, while the `unique_ptr` version releases automatically on every path.

```cpp
#include <iostream>
#include <memory>

void raw_version(bool error)
{
    int* data = new int[100];
    data[0] = 42;

    if (error) {
        return;  // Leak! Forgot delete[]
    }

    delete[] data;
}

void smart_version(bool error)
{
    auto data = std::make_unique<int[]>(100);
    data[0] = 42;

    if (error) {
        return;  // No leak — the destructor calls delete[] automatically
    }
}

int main()
{
    std::cout << "=== 错误场景 ===\n";
    raw_version(true);    // Leaks 400 bytes
    smart_version(true);  // Safe

    std::cout << "=== 正常场景 ===\n";
    raw_version(false);   // Released normally
    smart_version(false); // Released normally
    return 0;
}
```

Want to verify the leak yourself? Compile with AddressSanitizer: `g++ -Wall -Wextra -std=c++17 -fsanitize=address -g unique_ptr_intro.cpp`, and ASan will report the size and allocation location of the memory leaked by the raw-pointer version when the program ends. This is also our everyday go-to tool for tracking down memory issues in development.

## More Smart Pointers — Saved for Volume Two

The smart pointer family still has `shared_ptr` (shared ownership, reference counting) and `weak_ptr` (weak references, breaking circular references) waiting in the wings, and `unique_ptr` itself has advanced uses such as custom deleters. All of these need move semantics and rvalue references as their foundation, and all are core Volume Two material. For now, remembering two things is enough: **avoid writing `new` and `delete` directly**, preferring `std::make_unique`; and `unique_ptr` is zero-overhead—it won't slow your program down, but it will spare it from an entire class of memory bugs.

## Exercises

### Exercise 1: Refactor the Raw-Pointer Program

The code below leaks when `early_exit` is `true`. Rewrite it as a `unique_ptr` version and make sure no path leaks. Hint: just replace `Sensor* s = new Sensor(1)` with `auto s = std::make_unique<Sensor>(1)`, drop the `delete s`, and leave everything else untouched.

```cpp
struct Sensor
{
    int id;
    Sensor(int i) : id(i) { std::cout << "Sensor " << id << " 初始化\n"; }
    ~Sensor() { std::cout << "Sensor " << id << " 关闭\n"; }
    void read() { std::cout << "Sensor " << id << " 读取数据\n"; }
};

void use_sensor(bool early_exit)
{
    Sensor* s = new Sensor(1);
    s->read();
    if (early_exit) { return; }
    s->read();
    delete s;
}
```

::: details Reference answer

```cpp
#include <iostream>
#include <memory>

struct Sensor
{
    int id;
    Sensor(int i) : id(i) { std::cout << "Sensor " << id << " 初始化\n"; }
    ~Sensor() { std::cout << "Sensor " << id << " 关闭\n"; }
    void read() { std::cout << "Sensor " << id << " 读取数据\n"; }
};

void use_sensor(bool early_exit)
{
    auto s = std::make_unique<Sensor>(1);
    s->read();
    if (early_exit)
    {
        return;
    }
    s->read();
}
```

:::

### Exercise 2: Spot the Memory-Leak Patterns

The code below has two leak points (one in each of the `choice == 1` and `choice == 2` branches). Think it over: once `a` and `b` are wrapped in `unique_ptr`, are early returns and throws still a problem?

```cpp
void process(int choice)
{
    int* a = new int(10);
    int* b = new int(20);
    if (choice == 1) { return; }
    delete a;
    if (choice == 2) { throw std::runtime_error("error"); }
    delete b;
}
```

::: details Reference answer

Look at the two leak points in the original code: when `choice == 1`, the function returns immediately, so neither `a` nor `b` gets `delete`d; when `choice == 2`, `a` has already been released, but throwing the exception skips `delete b`, so `b` leaks.

Once `unique_ptr` takes over ownership, early `return` and `throw` are no longer problems. When the function leaves its scope, the local `unique_ptr`s destruct automatically and release the memory they manage; whether the exit is a normal return or a stack unwind caused by an exception, the destructors get called—so we no longer need to write `delete` by hand:

```cpp
#include <iostream>
#include <memory>
#include <stdexcept>

void process(int choice)
{
    auto a = std::make_unique<int>(10);
    auto b = std::make_unique<int>(20);
    if (choice == 1) { return; }
    if (choice == 2) { throw std::runtime_error("error"); }
}
```

Here both `a` and `b` are managed exclusively by `unique_ptr`. Watch the order at function exit: `b`, created last, is destroyed first, then `a`—both blocks of memory are released correctly.

:::

---

> **Next up**: With this, we wrap up the pointers-and-references chapter: from the basic concepts of raw pointers, through pointer arithmetic and its relationship with arrays, to references and this preview of smart pointers. Next we move on to Chapter Five to meet arrays and strings, and see what safer, friendlier tools C++ offers compared to C-style arrays.
