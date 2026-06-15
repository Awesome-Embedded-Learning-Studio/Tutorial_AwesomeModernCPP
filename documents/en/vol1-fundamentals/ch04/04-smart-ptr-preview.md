---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: Learn why we need smart pointers, get an initial look at how `unique_ptr`
  automatically manages memory, and lay the groundwork for deeper exploration in Volume
  Two.
difficulty: beginner
order: 4
platform: host
prerequisites:
- 引用
reading_time_minutes: 9
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Smart Pointer Preview
translation:
  engine: anthropic
  source: documents/vol1-fundamentals/ch04/04-smart-ptr-preview.md
  source_hash: 81b571dbabba1e5c271539224cb5709d99359a8957b29d510530735c36b43b67
  token_count: 1706
  translated_at: '2026-05-26T10:48:42.337533+00:00'
---
# Smart Pointer Preview

So far, we've been working with raw pointers for several chapters. Pointers are indeed powerful, but they are also dangerous—every time you `new` a block of memory, you must constantly remember to `delete` it. If you miss it along any code path, you get a memory leak. Modern C++ provides a systematic solution: **smart pointers**. We won't dive deep in this chapter; instead, we'll just introduce the problems they solve and what their basic usage looks like. The comprehensive explanation will come in Volume Two, where we'll systematically explore them alongside move semantics and RAII.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Understand the three classic problems of raw pointers in memory management
> - [ ] Grasp the basic idea of RAII—acquire on construction, release on destruction
> - [ ] Use `std::unique_ptr` and `std::make_unique` for basic dynamic memory management
> - [ ] Know the zero-overhead advantage of `unique_ptr` over raw pointers

## The Three Sins of Raw Pointers

Raw pointers have three classic problems in memory management (this sounds a bit like an indictment).

**Memory leaks** are the most common scenario: you `new` but forget to `delete`. What's more dangerous is forgetting on an exception exit path—under normal flow, the `delete[]` might execute, but once an error condition triggers and the function returns early, the memory is lost forever. (Ugh, my head is already spinning.)

```cpp
void process_data()
{
    int* data = new int[1000];

    if (some_error_condition()) {
        return;  // 直接 return 了，delete 呢？？？
    }

    delete[] data;
}
```

> The key point here is: **every line of code that might exit early (return, throw) is a potential leak point**. In a function with a dozen exits, you need to ensure the resource is properly released before every single one. If one day you add a new return and forget to write delete, you have a leak again.

**Double free** causes the program to crash directly—two pointers point to the same block of memory, and each `delete` it once. The runtime usually reports `double free or corruption`, which is especially common in multi-developer projects.

**Dangling pointers** occur when you continue to access memory through the original pointer after it has been `delete`. This type of bug is the most nasty: it might not surface at all during development (the content of freshly `delete` memory is often not yet overwritten, so `*p` might coincidentally still read the original value). But once it's in production and runs for a longer time, it causes random issues that are extremely painful to track down.

## RAII—One Key per Lock

The root cause of all three problems is the same: **resource acquisition and release are scattered across different parts of the code**. The core idea to solve this is called **RAII (Resource Acquisition Is Initialization)**—acquire resources in the constructor, and release them in the destructor. C++ guarantees that when an object goes out of scope, its destructor **will definitely be called**, whether it exits normally or via an exception. This guarantee is provided by the **stack unwinding** mechanism.

You can think of it as a key that automatically returns itself: take the key (acquire on construction), walk out of the room (leave the scope), and the key returns itself automatically (release on destruction).

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
        return;  // 即使提前 return，holder 的析构函数也会被调用
    }
}
```

Output:

```text
分配内存，值 = 42
内部值: 42
释放内存，值 = 42
```

Even though the function exited early via `return`, the destructor of `holder` was still called. This is the power of RAII—you don't need to manually write `delete` at every exit; C++'s scoping rules handle the management automatically for you.

> Note the `explicit` keyword—it prevents implicit conversions like `IntHolder holder = 42;`. For single-argument constructors, adding `explicit` is a good habit.

## unique_ptr—A Smart Pointer with Exclusive Ownership

Once you understand RAII, smart pointers are easy to grasp—they are simply utility classes that wrap `new` and `delete` into RAII. The most fundamental and commonly used one is `std::unique_ptr`, with the core semantic of **exclusive ownership**: a block of memory can only be held by one `unique_ptr` at any given time. It cannot be copied, but it can be **moved**.

### Creation and Basic Operations

C++14 introduced `std::make_unique`, which is the recommended way to create a `unique_ptr`. We'll use a custom type to demonstrate the complete lifecycle:

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
        hero->show_status();   // -> 访问成员，和裸指针一样
        std::cout << (*hero).name << "\n";  // * 解引用也行
    }
    // hero 在这里离开作用域，自动 delete

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

"Alice exits the stage." appears before "Continuing execution..."—the destructor was automatically called when the curly brace scope ended. The basic operations of `unique_ptr` come down to three: `*p` to dereference, `p->member` to access members, and `p.get()` to get the raw pointer (useful when passing to C interfaces).

> Why do we recommend `make_unique` over `unique_ptr<int>(new int(42))`? First, it's more concise—you don't need to write `new`. Second, when dealing with combinations of function arguments, writing `new` directly can lead to leaks due to unspecified evaluation order. We'll expand on this detail in Volume Two.

### No Copying, Only Moving

A `unique_ptr` **cannot be copied**—attempting to `auto p2 = p1;` will result in a direct compilation error. This is an intentional design choice: allowing copies would mean two `unique_ptr` pointing to the same block of memory, leading to a double delete when they go out of scope. If you need to transfer ownership, use `std::move`:

```cpp
auto p1 = std::make_unique<int>(42);
auto p2 = std::move(p1);  // 所有权从 p1 转移到 p2
// p1 变成 nullptr，p2 持有那块内存
```

The detailed mechanism of `std::move` will be systematically explained in Volume Two. For now, just remember that it's the standard way to transfer `unique_ptr` ownership.

### Zero Overhead—Safety Without a Performance Cost

A `unique_ptr` has **no additional runtime performance overhead**—it stores just a pointer internally, has no virtual functions, and after compiler optimization, the generated code is almost identical to manual `new/delete`. Modern C++ has a clear rule: **use `unique_ptr` instead of a raw `new/delete` whenever possible**.

## Hands-on: Raw Pointer vs. unique_ptr

Let's implement the memory leak scenario in two different ways. The core comparison is very intuitive: the raw pointer version leaks on the error path, while the `unique_ptr` version is automatically immune.

```cpp
#include <iostream>
#include <memory>

void raw_version(bool error)
{
    int* data = new int[100];
    data[0] = 42;

    if (error) {
        return;  // 泄漏！忘记 delete[]
    }

    delete[] data;
}

void smart_version(bool error)
{
    auto data = std::make_unique<int[]>(100);
    data[0] = 42;

    if (error) {
        return;  // 不泄漏——析构函数自动调用 delete[]
    }
}

int main()
{
    std::cout << "=== 错误场景 ===\n";
    raw_version(true);    // 泄漏 400 字节
    smart_version(true);  // 安全

    std::cout << "=== 正常场景 ===\n";
    raw_version(false);   // 正常释放
    smart_version(false); // 正常释放
    return 0;
}
```

Want to verify the leak yourself? Compile with AddressSanitizer: `g++ -Wall -Wextra -std=c++17 -fsanitize=address -g unique_ptr_intro.cpp`. ASan will point out the size and allocation location of the memory leaked by the raw pointer version when the program ends. This is also a standard tool for tracking down memory issues in daily development.

## More Smart Pointers—Saved for Volume Two

The smart pointer family also includes `shared_ptr` (shared ownership, reference counting) and `weak_ptr` (weak reference, breaking circular references), which haven't made an appearance yet. `unique_ptr` also has advanced usages like custom deleters. All of these require move semantics and rvalue references as a foundation, which are core topics in Volume Two. For now, just remember two things: first, **try to avoid writing `new` and `delete` directly**, and default to `std::make_unique`; second, `unique_ptr` is zero-overhead—it won't slow down your program, but it will protect it from a whole class of memory bugs.

## Summary

- The three major memory problems with raw pointers: **leaks** (forgetting delete), **double free**, and **dangling pointers** (use-after-free). The root cause is that resource acquisition and release are scattered in different places.
- **RAII** leverages C++'s automatic destructor invocation mechanism to bind a resource's lifecycle to an object's scope.
- `std::unique_ptr` provides a smart pointer with exclusive ownership that automatically releases memory when it goes out of scope. It cannot be copied but can be moved.
- `std::make_unique<T>(args...)` is the recommended way to create a `unique_ptr`, which is safer and more concise than writing `new` directly.
- `unique_ptr` is **zero-overhead** compared to raw pointers—there is no reason not to use it in new code.

### Common Mistakes

| Mistake | Cause | Solution |
|---------|-------|----------|
| Trying to copy a `unique_ptr` | Exclusive semantics prohibit copying | Use `std::move()` to transfer ownership |
| `make_unique` is unavailable under C++11 | It was only introduced in C++14 | Upgrade the standard or use `unique_ptr<T>(new T(...))` |
| Dereferencing `unique_ptr<int[]>` with `*p` | The array version does not support `*` | Use `p[i]` subscript access or `p.get()` |

## Exercises

### Exercise 1: Refactor a Raw Pointer Program

The following code leaks when `early_exit` is `true`. Please rewrite it using `unique_ptr` to ensure no leaks occur on any code path. Hint: just replace `Sensor* s = new Sensor(1)` with `auto s = std::make_unique<Sensor>(1)`, delete `delete s`, and leave everything else unchanged.

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

### Exercise 2: Identify Memory Leak Patterns

The following code has two leak points (one in each of the `choice == 1` and `choice == 2` branches). Think about it: after wrapping `a` and `b` with `unique_ptr`, are early returns and throws still a problem?

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

---

> **Next Stop**: With this, we have completed the Pointers and References chapter. From the basic concepts of raw pointers, to the relationship between pointer arithmetic and arrays, and finally to references and this preview of smart pointers—we've built a complete cognitive framework for C++ memory manipulation. Next, we'll move on to Chapter Five to explore arrays and strings, and see what safer, more usable tools C++ provides compared to C-style arrays.
