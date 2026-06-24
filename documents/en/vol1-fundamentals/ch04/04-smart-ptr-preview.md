---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand why smart pointers are necessary, get a first look at how
  `unique_ptr` manages memory automatically, and lay the groundwork for deeper learning
  in Volume Two.
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
  source: documents/vol1-fundamentals/ch04/04-smart-ptr-preview.md
  source_hash: 12e5e7391f95318586446f851cc376345af38ff59c8db7b329ba3565336b2c1a
  translated_at: '2026-06-24T00:30:51.875192+00:00'
  engine: anthropic
  token_count: 1702
---
# A Preview of Smart Pointers

Up to this point, we have been working with raw pointers for several chapters. Pointers are indeed powerful, but they are also dangerous—every time we `new` a block of memory, we must remember to `delete` it. If we miss even a single path, we end up with a memory leak. Modern C++ provides a systematic solution to this: **smart pointers**. In this chapter, we won't go too deep; instead, we will simply introduce the problems they solve and their basic usage. The comprehensive explanation will come in Volume Two, where we will systematically cover them alongside move semantics and RAII.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Understand the three classic problems of raw pointers regarding memory management.
> - [ ] Grasp the basic concept of RAII—acquire at construction, release at destruction.
> - [ ] Use `std::unique_ptr` and `std::make_unique` for basic dynamic memory management.
> - [ ] Understand the zero-overhead advantage of `unique_ptr` compared to raw pointers.

## The Three Sins of Raw Pointers

Raw pointers suffer from three classic problems in memory management (which sounds a bit like an indictment).

**Memory leaks** are the most common scenario: we `new` memory but forget to `delete` it. Even more dangerous is forgetting it on an exception exit path—`delete[]` might be reached in the normal flow, but once an error condition triggers and the function returns early, the memory is lost forever. (Ugh, this is already giving me a headache.)

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

> The key point is this: **every line of code that might exit early (return, throw) is a potential leak point**. In a function with a dozen exits, we must ensure resources are released correctly before every single exit. If we add a new return later and forget to write delete, we have a leak again.

**Double free** causes the program to crash immediately—two pointers point to the same memory, and each calls `delete` once. The runtime usually reports `double free or corruption`, which is particularly common in collaborative projects.

**Dangling pointers** occur when we continue to access memory through the original pointer after `delete`. This bug is the most nasty: it might not show up at all during development (the content of the just-deleted memory is often not yet overwritten, so `*p` might still read the original value), but in production, after running for a long time, random issues will appear, making troubleshooting extremely painful.

## RAII—One Key for One Lock

The root of all three problems is the same: **resource acquisition and release are scattered in different parts of the code**. The core idea to solve this is called **RAII (Resource Acquisition Is Initialization)**—acquire resources in the constructor and release them in the destructor. C++ guarantees that the destructor **will be called** when the object leaves the scope, whether it exits normally or via an exception. This guarantee is provided by the **stack unwinding** mechanism.

We can think of it as an automatically returning key: take the key (acquire on construction), leave the room (leave scope), and the key is automatically returned (release on destruction).

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

**Output:**

```text
分配内存，值 = 42
内部值: 42
释放内存，值 = 42
```

Even if the function returns early, the destructor for `holder` is still called. This demonstrates the power of RAII—you do not need to manually write `delete` at every exit point; C++ scope rules handle the resource management automatically.

> Note the `explicit` keyword—it prevents implicit conversions like `IntHolder holder = 42;`. For single-argument constructors, adding `explicit` is a best practice.

## unique_ptr—A Smart Pointer with Exclusive Ownership

Once we understand RAII, smart pointers are straightforward—they are simply tool classes that wrap `new` and `delete` into the RAII pattern. The most fundamental and commonly used one is `std::unique_ptr`, with the core semantic of **exclusive ownership**: a block of memory can be held by only one `unique_ptr` at a time. It cannot be copied, but it can be **moved**.

### Creation and Basic Operations

C++14 introduced `std::make_unique`, which is the recommended way to create a `unique_ptr`. We will use a custom type to demonstrate the complete lifecycle:

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

**Output:**

```text
Alice 登场！
Alice Lv.5
Alice
Alice 退场。
继续执行...
```

"Alice exits." appears before "Continuing execution..."—the destructor was automatically invoked when the brace scope ended. There are only three basic operations for `unique_ptr`: `*p` for dereferencing, `p->member` for member access, and `p.get()` to obtain the raw pointer (useful when passing to C interfaces).

> Why do we recommend `make_unique` over `unique_ptr<int>(new int(42))`? First, it is more concise, as we do not need to write `new`. Second, when composing function arguments, writing `new` directly can lead to memory leaks due to unspecified evaluation order; we will expand on this detail in Volume Two.

### Cannot Copy, Only Move

`unique_ptr` **cannot be copied**—`auto p2 = p1;` will result in a direct compilation error. This is an intentional design: allowing copying would imply two `unique_ptr` instances pointing to the same memory, leading to a double delete when they go out of scope. If you need to transfer ownership, use `std::move`:

```cpp
auto p1 = std::make_unique<int>(42);
auto p2 = std::move(p1);  // 所有权从 p1 转移到 p2
// p1 变成 nullptr，p2 持有那块内存
```

We will cover the detailed mechanism of `std::move` in Volume Two. For now, just remember that it is the standard way to transfer ownership of a `unique_ptr`.

### Zero Overhead — Safety Without Performance Cost

At runtime, `unique_ptr` has **zero performance overhead** — it essentially holds a single pointer, has no virtual functions, and the code generated after compiler optimization is nearly identical to manual `new/delete`. Modern C++ has a clear rule: **use `unique_ptr` instead of raw `new/delete` whenever possible**.

## Practice: Raw Pointers vs unique_ptr

Let's implement the memory leak scenario using two approaches. The core contrast is intuitive: the raw pointer version leaks on the error path, while the `unique_ptr` version is automatically immune.

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

Want to verify the leak yourself? Compile with AddressSanitizer: `g++ -Wall -Wextra -std=c++17 -fsanitize=address -g unique_ptr_intro.cpp`. ASan will report the size and allocation location of the memory leaked by the raw pointer version when the program exits. This is a standard tool for diagnosing memory issues in daily development.

## More Smart Pointers—Saved for Volume Two

The smart pointer family still has `shared_ptr` (shared ownership, reference counting) and `weak_ptr` (weak reference, breaking circular dependencies) waiting in the wings. `unique_ptr` also has advanced uses like custom deleters. These all require move semantics and rvalue references as a foundation, which are core topics in Volume Two. For now, remember two things: first, **avoid writing `new` and `delete` directly** and prefer `std::make_unique`; second, `unique_ptr` is zero-overhead—it won't slow down your program, but it will protect it from a whole class of memory bugs.

## Summary

- The three major memory issues with raw pointers: **leaks** (forgetting `delete`), **double free**, and **dangling pointers** (use-after-free). The root cause is that resource acquisition and release are scattered in different places.
- **RAII** leverages the automatic invocation mechanism of C++ destructors to bind the resource lifecycle to the object's scope.
- `std::unique_ptr` provides a smart pointer with exclusive ownership; it automatically releases memory when it goes out of scope, cannot be copied, but can be moved.
- `std::make_unique<T>(args...)` is the recommended way to create a `unique_ptr`; it is safer and more concise than writing `new` directly.
- `unique_ptr` is **zero-overhead** compared to raw pointers, so there is no reason not to use it in new code.

### Common Pitfalls

| Error | Cause | Solution |
|------|------|----------|
| Attempting to copy a `unique_ptr` | Exclusive semantics prohibit copying | Use `std::move()` to transfer ownership |
| `make_unique` unavailable under C++11 | Introduced in C++14 | Upgrade the standard or use `unique_ptr<T>(new T(...))` |
| Dereferencing `unique_ptr<int[]>` with `*p` | Array version does not support `*` | Use subscript access `p[i]` or `p.get()` |

## Exercises

### Exercise 1: Refactor a Raw Pointer Program

The following code leaks when `early_exit` is `true`. Please rewrite it using `unique_ptr` to ensure no leaks occur on any execution path. Hint: Just replace `Sensor* s = new Sensor(1)` with `auto s = std::make_unique<Sensor>(1)`, delete the `delete s` line, and leave everything else untouched.

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

### Exercise 2: Identifying Memory Leak Patterns

The code below contains two leak points (one in each of the `choice == 1` and `choice == 2` branches). Consider this: if we wrap `a` and `b` using `unique_ptr`, will early returns and exceptions still be an issue?

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

> **Next Stop**: With this, we conclude the chapter on pointers and references. From the basic concepts of raw pointers, to pointer arithmetic and its relationship with arrays, and finally a preview of references and smart pointers—we have established a comprehensive framework for understanding C++ memory operations. Next, we move on to Chapter Five to explore arrays and strings, examining the safer and more convenient tools that C++ provides compared to C-style arrays.
