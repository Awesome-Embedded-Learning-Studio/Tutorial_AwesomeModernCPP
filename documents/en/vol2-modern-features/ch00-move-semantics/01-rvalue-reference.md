---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: Understanding the C++ value category system, and mastering the binding
  rules and core semantics of rvalue references
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 卷一：C++ 基础入门
reading_time_minutes: 18
related:
- 移动构造与移动赋值
- 完美转发
tags:
- host
- cpp-modern
- intermediate
- 移动语义
title: 'Right Value References: From Copy to Move'
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/01-rvalue-reference.md
  source_hash: 28fe3e890ee8c015ac03e56fe7120756f4fdd7329e61acb475fd28419d1dc00d
  translated_at: '2026-06-16T03:54:24.148522+00:00'
  engine: anthropic
  token_count: 3203
---
# Rvalue References: From Copying to Moving

Welcome to modern C++! While "modern C++" generally refers to C++11 and later, the changes in features are significant enough to warrant a dedicated discussion.

> Some might disagree; I've actually been criticized for considering C++11 "modern." Well... it's a valid point. From the vantage point of 2026, when I am writing this, these features have existed for over a decade—so, temporally speaking, they aren't exactly "modern." However, compared to the antiquated C++98, the changes in features are substantial. That is precisely why this volume has been separated out!

When I first encountered C++ and read *Effective Modern C++*, I struggled to grasp the concept of "rvalue references." The term "rvalue reference" just seemed to exude an indescribable academic odor—what is `T&&`? How do you actually distinguish between lvalues and rvalues? Does `std::move` really "move" anything? Whenever I saw `std::move` in someone else's code, I would copy it over with a vague understanding, praying it would compile. Now that I'm writing this, I need to get to the bottom of these concepts, or at least avoid making rookie mistakes!

> Still rambling: I'm actually quite afraid of C++ language lawyers. Every time I pick up a pen to write, I fear being mocked by these experts. However, rigor is always a good thing—if you aren't rigorous with C++, you might get woken up by a memory explosion and get狠狠被你的ld艹一顿. But for teaching purposes, there is no need to obsess over details right from the start. Be careful not to miss the forest for the trees.

## Starting with a Blood-Pressure-Inducing Problem

Let's look at a scenario: string processing. Everyone knows this, right? Many people feel that `std::string` is sometimes too heavy and wish for a read-only string view. `const char*` is nice, but the NULL termination is annoying (relying on a `\0` as an end constraint is sometimes unreliable). So, let's create our own `StringWrapper`!

```cpp
struct StringWrapper {
    const char* ptr;
    size_t len;
    // Constructor, destructor, etc.
};
```

Then we write a piece of code that looks innocent enough:

```cpp
StringWrapper process(const std::string& raw) {
    return StringWrapper{raw.c_str(), raw.size()};
}

auto result = process("Hello, Modern C++");
```

In the absence of move semantics and when the compiler does not apply NRVO (Named Return Value Optimization), returning `wrapper` from `process` triggers the copy constructor—allocating a new block of memory and copying the string from `wrapper` byte by byte. Then `wrapper` is destroyed, releasing the original memory. Of course, in reality, GCC and MSVC in the C++03 era had widely implemented NRVO as a compiler extension, so this analysis discusses the "worst-case scenario" where NRVO does not apply. In other words, we pay for a memory allocation and a byte-by-byte copy just to "move" data from an object that is about to die to another location. If the string is long, say a few KB of JSON text, this copy is particularly wasteful—the source object is going to die anyway, so leaving the data in that memory is useless. Why not just take over ownership of the memory?

This is the core problem that move semantics solves. To understand move semantics, we must first understand how C++ classifies expressions—so-called **value categories**.

## The Panorama of Value Categories

Before C++11, things were relatively simple:

> An expression is either an lvalue or an rvalue.

It was just that simple. But with C++11, once resource ownership could be moved, the classification became more complex.

- Each expression belongs to exactly one of three categories: **lvalue**, **xvalue**, or **prvalue**.
- These three categories can be combined into broader categories: **glvalue** (generalized lvalue) = lvalue + xvalue, and **rvalue** = xvalue + prvalue.

If you find this classification system a bit convoluted, don't worry—I was tangled up for a while too. We can understand it through two attributes: **has identity** (the expression has a name, can have its address taken) and **can be moved from** (the expression is temporary and can have its resources safely "stolen").

**lvalue**: Has identity and cannot be moved.

For example, a variable `str` in `std::string str;` has a name, an address, and its lifecycle has not ended. You naturally cannot just steal its resources.

**xvalue** (expiring value): Has identity and can be moved. For example, the result of `std::move(str)` tells you, "this object has an identity, but it is about to die, so you can safely steal its resources."

**prvalue** (pure rvalue): Has no identity but can be moved. For example, a literal like `42` or a temporary object returned by a function. It has no name to begin with, so you don't need to worry about who will access it after you steal from it.

Let's look at a specific set of examples to distinguish these three categories clearly.

```cpp
std::string str = "hello";    // str is an lvalue
std::string& lref = str;      // lref is an lvalue
std::string&& rref = std::move(str); // rref is an xvalue
std::string("world")          // This temporary object is a prvalue
```

Here, `str` is the most typical lvalue—it has a name, an address, and `&str` is a valid expression (you can certainly get the address of this variable on the stack!). `std::move(str)` produces an xvalue; it points to the same memory as `str`, but semantically it is marked as "expiring." `std::string("world")` and the literal `42` are prvalues—temporary, nameless values.

> ⚠️ **Pitfall Warning**: There is a classic misconception that "lvalues can appear on the left side of an assignment, and rvalues can only appear on the right." This statement mostly held in the C era, but in C++, it is neither sufficient nor necessary. In `const int a = 10;`, `a` is an lvalue, but `a = 20;` fails to compile—`const` restricts modification but doesn't change the value category. Conversely, `std::string("hello")` is a prvalue, but in C++11 and later, it can appear on the left side of an assignment in certain cases (e.g., when calling member functions).

## Binding Rules of Rvalue References

Now that we understand value categories, let's look at what an rvalue reference—`T&&`—can actually bind to. The rule is actually quite simple: **An rvalue reference can only bind to an rvalue (prvalue or xvalue), not to an lvalue**.

```cpp
std::string s = "hello";
std::string&& r1 = s;             // Error! s is an lvalue
std::string&& r2 = std::move(s);  // OK, std::move(s) is an xvalue
std::string&& r3 = std::string("world"); // OK, temporary is a prvalue
```

If you uncomment the first line, GCC will give you a fairly direct error message:

```text
cannot bind ‘std::string’ lvalue to ‘std::string&&’
```

The intuition behind this binding rule is: the design purpose of an rvalue reference is to allow you to "take over" the resources of a temporary object. If an object is an lvalue (has a name, an address, and is still being used), how can you safely steal its stuff? The compiler stops you here entirely for safety.

Now let's compare the binding behavior of rvalue references and `const` lvalue references, which is crucial for understanding move constructors later.

A `const` lvalue reference `const T&` is C++'s "universal receiver"—it can bind to anything: lvalues, rvalues, `const`, non-const, it accepts everything. An rvalue reference `T&&` is a "selective receiver"—it only accepts rvalues. This difference looks simple, but it leads to a very important practical distinction: when you receive an rvalue with `const T&`, you promise "I won't modify it," so you can't steal its resources; when you receive an rvalue with `T&&`, you have permission to modify it, so you can safely transfer the resources away.

```cpp
void foo(const std::string& s); // Can accept anything, but cannot steal
void bar(std::string&& s);      // Only accepts rvalues, CAN steal
```

You might ask: Why not let rvalue references bind to lvalues too? Good question. We know move semantics expresses the transfer of ownership. An lvalue is a variable with its own independent address that manages its own resources; this naturally conflicts with the semantics of "not managing, ready to move." So, you would never want `T&&` to bind to just anything! If that were the case, we would lose the ability to distinguish between "this object can be safely stolen" and "this object is still in use"—and this distinction is the very reason move semantics exists.

## The Essence of std::move — A Carefully Packaged Cast

`std::move` is arguably one of the most misleading names in C++ history. It sounds like it "moves" something, but in reality, it **moves absolutely nothing**. `std::move` does only one thing: **casts its argument to an rvalue reference**, specifically `static_cast<T&&>`. That's it. Nothing more, nothing less.

We can implement an equivalent `my_move` ourselves:

```cpp
template<typename T>
constexpr decltype(auto) my_move(T&& t) noexcept {
    return static_cast<std::remove_reference_t<T>&&>(t);
}
```

This code does something very direct: regardless of the type of the incoming `t`, it first uses `std::remove_reference_t` to strip any references that might be present, and then `static_cast`s it to an rvalue reference. Throughout this process, no data is moved, copied, or modified—it is purely a type conversion.

So what is its use? The key lies in the **signatures of move constructors and move assignment operators**. When you write `std::string s2 = std::move(s1);`, `std::move` converts `s1` (an lvalue) into an rvalue reference. This rvalue reference matches the move constructor of `std::string`, `string(string&&)`. The move constructor is the guy that actually performs the "resource transfer" operation—it steals `s1`'s internal buffer pointer and nulls `s1`'s pointer. `std::move` just hands over the key.

```cpp
std::string s1 = "Hello";
std::string s2 = std::move(s1); // s1 is now empty (valid but unspecified state)
```

Here is a very easy trap to fall into: **using `std::move` on basic types like `int` or `double` brings no logical performance benefit** (I can't conclude definitively out of fear of compiler optimizations). `std::move(42)` just converts `42` to `int&&`, but "moving" and "copying" an `int` are the same thing—both copy four bytes. The power of move semantics is only evident in **classes that manage resources**, such as classes holding dynamic memory, file handles, or network connections.

## Lifetime of Temporary Objects — What Rvalue References Extend

In C++, the lifetime of a temporary object (prvalue) usually ends at the end of the full expression containing it. However, rvalue references and `const` lvalue references have a special ability: when bound to a temporary object, they extend the lifetime of that temporary object, allowing it to live until the end of the reference's scope.

```cpp
const int& r1 = 42;        // Temporary int materialized and lifetime extended
int&& r2 = 42;             // Same here, r2 modifies the temporary
```

Both behave the same way in terms of extending lifetime. The difference is that `int&&` is non-const—you can modify it. This looks a bit weird; how can a literal `42` be modified? Actually, the compiler puts this temporary value into a storage location behind the scenes, and `r2` points to that space.

```cpp
int&& r = 42;
r = 100; // Valid! Modifies the temporary materialized storage
```

This feature isn't used much in practice, but understanding it helps eliminate the fear that "rvalue references will dangle immediately." When you write `auto&& r = get_temp();`, the object `r` points to won't disappear on the next line—it lives until the end of `r`'s scope.

## General Example — Copying vs. Moving in String Concatenation

Let's put what we've learned together and look at a real-world example. Suppose we are building log messages:

```cpp
std::string build_log(const std::string& base) {
    return base + " [INFO]" + " " + "User logged in";
}
```

Here, `base + " [INFO]"` generates a large number of temporary `std::string` objects—every `+` operation creates a new temporary string. In the C++03 world, every `+` resulted in a memory allocation and a data copy. After C++11, the situation improved—if `operator+` accepts a value parameter and returns a named local variable, the compiler will automatically trigger **implicit move** upon return, passing along the moved temporary object in subsequent concatenations, transferring only the internal pointer without copying character data. Of course, C++17's guaranteed copy elision goes even further: when a function returns a prvalue, even the move constructor can be omitted.

The benefit is more direct in function returns. `build_log` returns a `std::string`. The compiler has two optimization methods here: NRVO (Named Return Value Optimization) can eliminate this copy directly. Failing that, even if NRVO doesn't kick in, C++11 will automatically treat the return value as an rvalue (implicit move), calling `std::string`'s move constructor—transferring the internal pointer without copying character data.

Let's look at another example of container element transfer:

```cpp
std::vector<std::string> vec;
vec.push_back(std::move(str1));      // (1) Move
vec.emplace_back("literal");         // (2) Emplace
```

The first `push_back` uses move semantics: `std::move(str1)` converts `str1` to an rvalue reference, and the vector calls `std::string`'s move constructor to construct the new element—the cost is transferring a pointer and two `size_t`s, not copying the entire string content. The second `emplace_back` looks like it "directly constructs," but what actually happens is: `"literal"` first creates a temporary object via `std::string`'s `const char*` constructor, and then this temporary object is passed as an rvalue into the `push_back(T&&)` overload to be move-constructed into the vector's storage. In other words, it has one extra step of temporary object construction compared to the first method, but still only performs a move without a deep copy. If you really want to skip the temporary object construction and achieve true in-place construction, you should use `emplace_back`—it calls `std::string`'s constructor directly in the vector's storage.

We can verify this with a tracking class:

```cpp
struct Tracker {
    std::string name;
    Tracker(std::string n) : name(std::move(n)) { std::cout << "CTOR " << name << "\n"; }
    Tracker(const Tracker& t) : name(t.name) { std::cout << "COPY " << name << "\n"; }
    Tracker(Tracker&& t) noexcept : name(std::move(t.name)) { std::cout << "MOVE " << name << "\n"; }
    ~Tracker() { std::cout << "DTOR " << name << "\n"; }
};
```

```cpp
std::vector<Tracker> vec;
vec.push_back(Tracker("A")); // Tracker("A") creates a temporary, then moves it
vec.emplace_back("B");       // Directly constructs Tracker("B") in place
```

Output:

```text
CTOR A
MOVE A
DTOR A
CTOR B
DTOR B
DTOR A
```

The output is clear: `push_back` first constructed a temporary object, then moved it in, and the temporary object was destroyed—two construction steps. `emplace_back` has only one line of output; it constructed directly in the vector's storage, saving the move step. Returning to the `vec.push_back("literal")` scenario in the article, the process is the same: `"literal"` is first implicitly converted to a temporary `std::string`, which is then moved into the vector. If you pursue extreme zero overhead, `emplace_back` is the correct choice.

## Hands-on Experiment — rvalue_demo.cpp

Let's write a complete program to run through the binding rules of rvalue references, the behavior of `std::move`, and the lifetime of temporary objects.

```cpp
#include <iostream>
#include <string>
#include <utility>

class Tracker {
public:
    std::string name;
    Tracker(std::string n) : name(std::move(n)) { std::cout << "CTOR " << name << "\n"; }
    Tracker(const Tracker& t) : name(t.name) { std::cout << "COPY " << name << "\n"; }
    Tracker(Tracker&& t) noexcept : name(std::move(t.name)) { std::cout << "MOVE " << name << "\n"; }
    Tracker& operator=(const Tracker& t) {
        name = t.name;
        std::cout << "COPY ASSIGN " << name << "\n";
        return *this;
    }
    Tracker& operator=(Tracker&& t) noexcept {
        name = std::move(t.name);
        std::cout << "MOVE ASSIGN " << name << "\n";
        return *this;
    }
    ~Tracker() { std::cout << "DTOR " << name << "\n"; }
};

Tracker get_tracker() {
    return Tracker("Returned"); // C++17 guaranteed elision
}

int main() {
    // 1. Basic construction
    Tracker t1("T1");

    // 2. Copy construction (lvalue)
    Tracker t2 = t1;

    // 3. Move construction (std::move)
    Tracker t3 = std::move(t1);

    // 4. Function return (prvalue)
    Tracker t4 = get_tracker();

    // 5. Move assignment
    t2 = std::move(t4);

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -o rvalue_demo rvalue_demo.cpp && ./rvalue_demo
```

Expected output:

```text
CTOR T1
COPY T1
MOVE T1
CTOR Returned
MOVE ASSIGN Returned
DTOR Returned
DTOR Returned
DTOR T1
DTOR T1
DTOR T1
```

Let's analyze this output step by step. In step 2, `Tracker t2 = t1;` triggered the copy constructor—`t1` is an lvalue, so it can only match the copy constructor, and `t2`'s name became `T1`. In step 3, `std::move(t1)` converted `t1` to an rvalue reference, matching the move constructor—`t3`'s name became `T1` (stolen from `t1`), and `t1`'s name became empty.

Step 4 is the most interesting. `get_tracker()` constructed a `Tracker` inside the function and returned it. Note that there is only one construction in the output—no copy, no move. This is due to C++17's **guaranteed copy elision**: when returning a prvalue, the compiler constructs the object directly in the caller's space, eliminating even the move. This is why we will dedicate the next article to discussing RVO and NRVO.

The move assignment in step 5 is also worth noting. `t2 = std::move(t4);` transferred `t4`'s resources to `t2`—`t2`'s original name `T1` was overwritten to `Returned`, and `t4` became empty. In this process, `t2`'s original resource (the memory holding `T1`) was correctly released, because the move assignment operator ensures old resources are cleaned up before overwriting.

## Run Online

Run the rvalue reference example online and trace the complete process of construction, copying, moving, and destruction:

<OnlineCompilerDemo
  title="Rvalue References & Value Categories: Construction, Copy, Move, Destruction Trace"
  source-path="code/examples/vol2/01_rvalue_reference.cpp"
  description="Run online and observe the order of Tracker object construction, copy construction, move construction, and destruction."
  allow-run
/>

## Summary

In this article, we laid the groundwork for rvalue references. C++'s value category system is divided into three categories: lvalue, xvalue, and prvalue, which intersect based on the dimensions of "has identity" and "can be moved." An rvalue reference `T&&` can only bind to rvalues (prvalue or xvalue), which ensures we don't accidentally steal resources from an lvalue that is still in use. `std::move` is essentially a `static_cast<T&&>`; it performs no move operation—the ones actually moving resources are the move constructor and move assignment operator. When a temporary object is bound to an rvalue reference, its lifetime is extended to the end of the reference's scope.

These concepts may seem abstract, but they form the foundation of the entire edifice of move semantics. In the next article, we will build on this foundation—implementing move constructors and move assignment operators to truly achieve zero-copy resource transfer.
