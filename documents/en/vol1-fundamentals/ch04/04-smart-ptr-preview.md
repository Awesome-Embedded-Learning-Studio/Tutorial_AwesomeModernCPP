---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand why we need smart pointers, get a first look at how `unique_ptr`
  manages memory automatically, and lay the groundwork for deeper learning in Volume
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
  source: documents/vol1-fundamentals/ch04/04-smart-ptr-preview.md
  source_hash: 6b36a1613867f79fb379076365e10f9ce2064ba26741a7f9e52c0b543ee213b4
  translated_at: '2026-06-16T03:42:48.291566+00:00'
  engine: anthropic
  token_count: 1702
---
# A Preview of Smart Pointers

Up to this point, we have been working with raw pointers for several chapters. Pointers are indeed powerful, but they are also dangerous—every time you `new` a block of memory, you must remember to `delete` it. If you miss this in any code path, you have a memory leak. Modern C++ provides a systematic solution: **smart pointers**. We won't go too deep in this chapter; we just want to show you what problems they solve and what their basic usage looks like. The comprehensive explanation will come in Volume II, where we will systematically explore them alongside move semantics and RAII.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Understand the three classic problems of raw pointers regarding memory management
> - [ ] Grasp the basic idea of RAII—acquire in the constructor, release in the destructor
> - [ ] Use `unique_ptr` and `make_unique` for basic dynamic memory management
> - [ ] Know the zero-overhead advantage of `unique_ptr` compared to raw pointers

## The Three Sins of Raw Pointers

Raw pointers have three classic problems in memory management (which feels a bit like an indictment).

**Memory leaks** are the most common situation: you `new` but forget to `delete`. Even more dangerous is forgetting on an exception exit path—under normal flow, `delete` might be reached, but once an error condition triggers and the function returns early, the memory is never recovered. (Ugh, this is already a headache).

```cpp
void riskyFunction() {
    Resource* r = new Resource(); // Acquired
    // ... do some work ...
    if (error_condition) {
        return; // LEAK! Forgot to delete r
    }
    // ... do more work ...
    delete r; // Normal release
}
```

> The key here is: **every line of code that might exit early (return, throw) is a potential leak point**. In a function with a dozen exits, you need to ensure resources are correctly released before every exit. One day you add a new return, forget to write delete, and there is another leak.

**Double free** causes the program to crash directly—two pointers point to the same block of memory, and each calls `delete` once. The runtime usually reports `double free or corruption`, which is particularly common in multi-person collaborative projects.

**Dangling pointers** occur when you continue to access the original pointer after `delete`. This type of bug is the most nasty: it might not show up at all during development (the content of the just `delete`d memory is often not overwritten yet, and `*ptr` happens to still read the original value), but once in production and running for a long time, random problems will appear, making troubleshooting extremely painful.

## RAII—One Key for One Lock

The root of all three problems is the same: **resource acquisition and release are scattered in different places in the code**. The core idea to solve this is called **RAII (Resource Acquisition Is Initialization)**—acquire resources in the constructor and release them in the destructor. C++ guarantees that the destructor **will be called** when the object leaves the scope, whether it exits normally or by exception. This guarantee is provided by the **stack unwinding** mechanism.

You can imagine it as a key that returns itself: take the key (acquire on construction), walk out of the room (leave scope), and the key is automatically returned (release on destruction).

```cpp
#include <iostream>

class DoorKey {
public:
    DoorKey() { std::cout << "Key acquired, door opened.\n"; }
    ~DoorKey() { std::cout << "Key returned, door closed.\n"; }
};

void enterRoom() {
    DoorKey key; // RAII: Acquire resource
    std::cout << "Inside the room...\n";
    // No matter what happens here...
    // ...even if an exception is thrown
}

int main() {
    enterRoom();
    return 0;
}
```

Running result:

```text
Key acquired, door opened.
Inside the room...
Key returned, door closed.
```

Even if the function returns early or throws an exception, `DoorKey`'s destructor is still called. This is the power of RAII—you don't need to manually write `delete` at every exit; C++ scope rules help you manage it automatically.

> Note the `explicit` keyword—it prevents implicit conversions like `DoorKey k = {};`. For single-argument constructors, adding `explicit` is a good habit.

## unique_ptr—A Smart Pointer with Exclusive Ownership

Understanding RAII, smart pointers are easy to understand—they are just tool classes that wrap `new` and `delete` into RAII. The most basic and most common is `unique_ptr`, whose core semantic is **exclusive ownership**: a block of memory can only be held by one `unique_ptr` at a time, it cannot be copied, but it can be **moved**.

### Creation and Basic Operations

C++14 introduced `make_unique`, which is the recommended way to create `unique_ptr`. We use a custom type to demonstrate the complete lifecycle:

```cpp
#include <iostream>
#include <memory> // Header for smart pointers

struct Actor {
    std::string name;
    Actor(std::string n) : name(std::move(n)) {
        std::cout << name << " 登场。\n";
    }
    ~Actor() { std::cout << name << " 退场。\n"; }
};

int main() {
    // Recommended way to create unique_ptr (C++14)
    auto actor = std::make_unique<Actor>("Alice");

    // Use -> to access members
    std::cout << "Current actor: " << actor->name << "\n";

    // Use * to dereference
    // auto& ref = *actor;

    std::cout << "Continuing execution...\n";
    // actor goes out of scope here, automatically deleted
    return 0;
}
```

Running result:

```text
Alice 登场。
Current actor: Alice
Continuing execution...
Alice 退场。
```

"Alice 退场。" appears before "Continuing execution..."—the destructor is called automatically when the brace scope ends. The basic operations of `unique_ptr` are just three: `*` to dereference, `->` to access members, and `get()` to get the raw pointer (useful when passing to C interfaces).

> Why recommend `make_unique` instead of `new`? First, it's more concise, no need to write `new Type`. Second, when involving function arguments, writing `new` directly can lead to leaks due to unspecified evaluation order; this detail will be expanded in Volume II.

### Cannot Copy, Only Move

`unique_ptr` **cannot be copied**—`auto p2 = p1;` will directly cause a compilation error. This is intentional design: allowing copying implies two `unique_ptr`s pointing to the same block of memory, leading to a double delete when they leave the scope. If you need to transfer ownership, use `std::move`:

```cpp
std::unique_ptr<Actor> createActor() {
    auto p = std::make_unique<Actor>("Bob");
    return p; // Move is implicit here
}

int main() {
    auto mainActor = createActor(); // Ownership moved
    // auto copy = mainActor;       // ERROR! Cannot copy
    auto stolen = std::move(mainActor); // OK, move
    // mainActor is now nullptr
    return 0;
}
```

The detailed mechanism of `std::move` will be systematically explained in Volume II. For now, just remember it is the standard way to transfer `unique_ptr` ownership.

### Zero Overhead—Safety Without Performance Cost

`unique_ptr` has **no additional performance overhead** at runtime—it stores just one pointer internally, has no virtual functions, and the code generated after compiler optimization is almost identical to manually `delete`ing. Modern C++ has a clear rule: **use `unique_ptr` instead of raw pointers whenever possible**.

## Real-world Comparison: Raw Pointer vs unique_ptr

Let's implement the memory leak scenario in two ways. The core comparison is intuitive: the raw pointer version leaks on the error path, while the `unique_ptr` version is automatically immune.

```cpp
#include <iostream>
#include <memory>
#include <vector>

struct Data { int value = 42; };

void rawPointerVersion(bool error) {
    Data* data = new Data();
    // Simulate some work
    std::vector<int> v(1000);

    if (error) {
        return; // LEAK! 'data' is not deleted
    }

    delete data; // Normal release
}

void smartPointerVersion(bool error) {
    auto data = std::make_unique<Data>();
    std::vector<int> v(1000);

    if (error) {
        return; // SAFE! 'data' is automatically deleted
    }
    // Automatic release when function ends
}

int main() {
    rawPointerVersion(true);  // Memory leaked
    smartPointerVersion(true); // Memory safe
    return 0;
}
```

Want to verify the leak yourself? Compile with AddressSanitizer: `g++ -fsanitize=address -g main.cpp`, ASan will report the size and location of the leaked memory at the end of the program. This is also a standard tool for troubleshooting memory issues in daily development.

## More Smart Pointers—Saved for Volume II

The smart pointer family also has `shared_ptr` (shared ownership, reference counting) and `weak_ptr` (weak reference, breaks circular references) that haven't appeared yet. `unique_ptr` also has advanced usages like custom deleters. These require move semantics and rvalue references as a foundation, which are core contents of Volume II. For now, just remember two things: first, **try not to write `new` and `delete` directly**, prefer `unique_ptr`; second, `unique_ptr` is zero-overhead—it won't slow down your program, but it will save you from a whole class of memory bugs.

## Summary

- Three major memory problems with raw pointers: **Leaks** (forgot delete), **Double Free**, **Dangling Pointers** (use-after-free); the root cause is that resource acquisition and release are scattered in different places.
- **RAII** utilizes C++'s automatic destructor invocation mechanism to bind the resource lifecycle to the object's scope.
- `unique_ptr` provides a smart pointer with exclusive ownership, automatically releasing memory when leaving scope, cannot be copied but can be moved.
- `make_unique` is the recommended way to create `unique_ptr`, safer and more concise than writing `new` directly.
- `unique_ptr` is **zero-overhead** compared to raw pointers; there is no reason not to use it in new code.

### Common Pitfalls

| Error | Cause | Solution |
|------|------|----------|
| Attempting to copy `unique_ptr` | Exclusive semantics forbid copying | Use `std::move` to transfer ownership |
| `make_unique` unavailable under C++11 | Introduced in C++14 | Upgrade standard or use `new` |
| `unique_ptr<T[]>` dereferenced with `*` | Array version doesn't support `*` | Use `[]` subscript access or `get()` |

## Exercises

### Exercise 1: Refactor a Raw Pointer Program

The following code leaks when `fail` is `true`. Please rewrite it to a `unique_ptr` version to ensure no leaks under any path. Hint: Just replace `new` with `make_unique`, delete `delete`, and leave the rest untouched.

```cpp
#include <iostream>
#include <memory>

struct Widget {
    int id;
    Widget(int i) : id(i) { std::cout << "Widget " << id << " created.\n"; }
    ~Widget() { std::cout << "Widget " << id << " destroyed.\n"; }
};

void process(bool fail) {
    // TODO: Replace raw pointer with unique_ptr
    Widget* w = new Widget(10);

    if (fail) {
        std::cout << "Operation failed, returning early.\n";
        return; // Leak happens here
    }

    std::cout << "Operation succeeded.\n";
    delete w;
}

int main() {
    process(true);
    return 0;
}
```

### Exercise 2: Identify Memory Leak Patterns

The following code has two leak points (one in the `if` branch and one in the `try` branch). Think about it: after wrapping `ptr1` and `ptr2` with `unique_ptr`, are early returns and throws still a problem?

```cpp
#include <iostream>
#include <memory>
#include <stdexcept>

void complexLogic() {
    int* ptr1 = new int(100);
    int* ptr2 = new int(200);

    try {
        // Simulate some operation that might throw
        if (true) {
            throw std::runtime_error("Simulated error");
        }
        delete ptr1;
        delete ptr2;
    } catch (...) {
        // Leak: ptr1 and ptr2 not deleted here
        throw;
    }
}
```

---

> **Next Stop**: At this point, we have fully completed the chapter on pointers and references. From the basic concepts of raw pointers, to pointer arithmetic and arrays, to references and a preview of smart pointers—we have established a complete cognitive framework for C++ memory operations. Next, we enter Chapter Five to learn about arrays and strings, and see what tools C++ provides that are safer and more useful than C-style arrays.
