---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: Understanding the C++ value category system, and mastering the binding
  rules and core semantics of rvalue references.
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
title: 'Rvalue References: From Copying to Moving'
translation:
  engine: anthropic
  source: documents/vol2-modern-features/ch00-move-semantics/01-rvalue-reference.md
  source_hash: 17b5ddce9a0c55adbca888803b2617ad4a14da240d10c6c8a381fea9967b5082
  token_count: 3209
  translated_at: '2026-05-26T11:17:37.767432+00:00'
---
# Rvalue References: From Copying to Moving

Welcome to modern C++! Although the term "modern C++" generally refers to C++11 and later, the changes in features are significant enough to warrant a dedicated discussion.

> Some might argue about this. I've personally been called out for considering C++11 "modern." Well... it's a fair point. From the year 2026 when I started writing this, these features have existed for over a decade—so temporally, they aren't exactly "modern." But compared to ancient C++ like C++98, the changes in features are substantial. That's exactly why this volume has been separated out!

When I first encountered C++ and read *Effective Modern C++*, I never quite grasped the concept of "rvalue references." The four Chinese characters for "rvalue reference" always exuded an indescribable academic aura—what exactly is `T&&`? How do we actually distinguish between lvalues and rvalues? Is `std::move` really "moving" anything? Whenever I saw `std::move` in someone else's code, I would just copy it over with a half-understood shrug, praying it would compile. Now that I'm writing about this, I need to get to the bottom of it—or at least, avoid making rookie mistakes!

> Another quick rant: I'm honestly a bit scared of C++ language lawyers. Every time I write something, I worry about being mocked by these experts. But rigor is always a good thing—write C++ without rigor, and you might get woken up in the middle of the night by a memory explosion, only to be thoroughly chewed out by your linker. However, for teaching purposes, there's no need to obsess over details right out of the gate. Beware of missing the forest for the trees.

## Starting with a Blood-Pressure-Raising Problem

Consider a common scenario: string processing. Many people feel that `std::string` is sometimes too heavy, and wish for a read-only string view. A `const char*` is nice, but null termination is a pain (relying on a `\0` as a delimiter can be unreliable). So, let's build our own `StringWrapper`!

```cpp
class StringWrapper {
    char* data_;
    std::size_t size_;

public:
    StringWrapper(const char* str)
    {
        size_ = std::strlen(str);
        data_ = new char[size_ + 1];
        std::memcpy(data_, str, size_ + 1);
    }

    // 拷贝构造：深拷贝
    StringWrapper(const StringWrapper& other)
        : size_(other.size_)
    {
        data_ = new char[size_ + 1];
        std::memcpy(data_, other.data_, size_ + 1);
    }

    ~StringWrapper()
    {
        delete[] data_;
    }
};
```

Then we write some seemingly innocent code:

```cpp
StringWrapper build_greeting(const std::string& name)
{
    StringWrapper result(("Hello, " + name + "!").c_str());
    return result;
}

int main()
{
    StringWrapper greeting = build_greeting("World");
    return 0;
}
```

Without move semantics and when the compiler doesn't apply NRVO (Named Return Value Optimization), returning `result` from `build_greeting` triggers a copy construction—it allocates a new block of memory and copies the string from `result` byte by byte. Then `result` destructs itself, freeing the original memory. Of course, in reality, GCC and MSVC from the C++03 era had already widely implemented NRVO as a compiler extension, so this analysis discusses the worst-case scenario when "NRVO doesn't kick in." In other words, we pay for a memory allocation and a byte-by-byte copy just to "move" data from an object that is about to be destroyed to another location. If the string is very long, like a few KB of JSON text, this copy is especially wasteful—the source object is going to die anyway, so the data sitting in that memory is useless, so why not just take over ownership of the memory?

This is the core problem that move semantics aims to solve. To understand move semantics, we must first understand how C++ classifies expressions—known as **value categories**.

## The Big Picture of Value Categories

Before C++11, things were pretty simple:

> An expression is either an lvalue or an rvalue.

It was just that simple. But when C++11 arrived, along with the ability to transfer resource ownership, the classification became more complex.

- Every expression belongs to exactly one of three categories: **lvalue**, **xvalue**, or **prvalue**.
- These three categories can be combined into broader groups: **glvalue** (generalized lvalue) = lvalue + xvalue, and **rvalue** = xvalue + prvalue.

If you find this classification system a bit convoluted, don't worry—I was tangled up by it for a long time too. We can understand it through two properties: **has identity** (the expression has a name and can have its address taken) and **can be moved from** (the expression is temporary and its resources can be safely "stolen").

Having identity but not being movable is an **lvalue**.

For example, `x` in a regular variable `int x = 10;` has a name, has an address, and its lifetime hasn't ended, so of course you can't just steal its resources. Having identity and being movable is an **xvalue** (expiring value)—like the result of `std::move(x)`, which tells you "this object has an identity, but it's about to die, so you can safely steal its resources." Having no identity but being movable is a **prvalue** (pure rvalue)—like the literal `42` or a temporary object returned by a function. It has no name to begin with, so you don't need to worry about someone else accessing it after you steal from it.

Let's look at a set of concrete examples to clearly distinguish these three categories.

```cpp
int x = 10;            // x 是 lvalue
int&& r = std::move(x); // std::move(x) 是 xvalue
int y = x + 1;         // x + 1 是 prvalue
int z = 42;            // 42 是 prvalue
```

Here, `x` is the most typical lvalue—it has a name, has an address, and `&x` is a valid expression (you can certainly take the address of this variable on the stack!). `std::move(x)` produces an xvalue; it points to the same memory as `x`, but semantically it is marked as "expiring soon." `x + 1` and `42` are both prvalues—temporary, nameless values.

> ⚠️ **Pitfall Warning**: A classic misconception is that "lvalues can appear on the left side of an assignment, and rvalues can only appear on the right." This statement mostly held true in the C era, but in C++, it is neither sufficient nor necessary. In `const int cx = 10;`, `cx` is an lvalue, but `cx = 20;` fails to compile—`const` restricts modification but doesn't change the value category. Conversely, `std::string("hello")` is a prvalue, but in certain cases after C++11, it can also appear on the left side of an assignment (such as when calling a member function).

## Binding Rules of Rvalue References

Now that we understand value categories, let's look at what rvalue references—`T&&`—can actually bind to. The rule is quite simple: **an rvalue reference can only bind to an rvalue (prvalue or xvalue), and cannot bind to an lvalue**.

```cpp
int x = 10;

int&& r1 = 42;           // OK：42 是 prvalue
int&& r2 = x + 1;        // OK：x + 1 是 prvalue
int&& r3 = std::move(x); // OK：std::move(x) 是 xvalue

// int&& r4 = x;         // 编译错误：x 是 lvalue，不能绑定到右值引用
```

If you uncomment the last line, GCC will give you a pretty straightforward error message:

```text
error: cannot bind rvalue reference of type 'int&&' to lvalue of type 'int'
```

The intuition behind this binding rule is that rvalue references are designed to let you "take over" the resources of a temporary object. If an object is an lvalue (has a name, has an address, and is still being used), how can you safely steal its stuff? The compiler stops you here entirely for safety.

Now let's compare the binding behavior of rvalue references and const lvalue references, which is crucial for understanding the move constructor that follows.

A const lvalue reference `const T&` is the "universal receiver" in C++—it can bind to anything: lvalues, rvalues, const, non-const, it takes them all. An rvalue reference `T&&` is the "picky receiver"—it only accepts rvalues. This difference seems simple, but it leads to a very important practical distinction: when you use `const T&` to receive an rvalue, you are promising "I won't modify it," so you can't steal its resources; when you use `T&&` to receive an rvalue, you have the permission to modify it, so you can safely transfer the resources away.

```cpp
void process_const_ref(const std::string& s)
{
    // 可以读取 s，但不能修改它
    // 所以无法"偷走" s 的内部缓冲区
    std::cout << s.size() << "\n";
}

void process_rvalue_ref(std::string&& s)
{
    // s 是非 const 的右值引用，可以修改它
    // 所以可以安全地转移 s 的内部资源
    std::string stolen = std::move(s);
    // 此时 s 处于"有效但未指定"的状态
}
```

You might ask: why not let rvalue references bind to lvalues too? Good question. We know that move semantics expresses the transfer of ownership. An lvalue is a variable with its own independent address that manages its own resources—this naturally conflicts with the semantics of "not managing anything and preparing to move away." So you really wouldn't want `T&&` to be able to bind to just anything! If it could, we would lose the ability to distinguish between "this object can be safely stolen from" and "this object is still in use"—and this distinction is the fundamental reason move semantics exists.

## The Essence of std::move — A Carefully Packaged Type Cast

The name `std::move` is arguably one of the most misleading names in C++ history. It sounds like it "moves" something, but it actually **moves absolutely nothing**. `std::move` does exactly one thing: **casts its argument to an rvalue reference**, which is `static_cast<T&&>`. Nothing more, nothing less.

We can implement an equivalent `move` ourselves:

```cpp
template<typename T>
constexpr typename std::remove_reference<T>::type&&
my_move(T&& t) noexcept
{
    return static_cast<typename std::remove_reference<T>::type&&>(t);
}
```

What this code does is very straightforward: regardless of what type `T` is, it first uses `remove_reference` to strip away any existing references, and then `static_cast` it into an rvalue reference. Throughout this entire process, no data is moved, copied, or modified—it is purely a type cast.

So what is it actually good for? The key lies in the **signatures of the move constructor and move assignment operator**. When you write `std::string a = std::move(b);`, `std::move(b)` converts `b` to `std::string&&`, and this rvalue reference matches the move constructor `std::string(std::string&& other)` of `std::string`. The move constructor is the one that actually performs the "resource transfer" operation—it steals the internal buffer pointer of `other` and nullifies `other`'s pointer. `std::move` merely hands over a key.

```cpp
std::string a = "Hello";
std::string b = std::move(a);  // std::move 只是转换类型
                                  // 移动构造函数做了实际的资源转移
// 此刻 a 处于"有效但未指定"的状态
// 在大多数实现中 a 变成空字符串，但你不应该依赖这个行为
```

Here is a very easy trap to fall into: **using `std::move` on fundamental types does not logically bring any performance benefits** (out of fear of compiler optimizations, I can't even make a definitive statement). `std::move(42)` simply converts `int` to `int&&`, but "moving" and "copying" an `int` are the exact same thing—both copy four bytes. The power of move semantics only manifests in **classes that manage resources**, such as classes holding dynamic memory, file handles, or network connections.

## Lifetime of Temporary Objects — What Rvalue References Extend

In C++, the lifetime of a temporary object (prvalue) typically ends when the full expression containing it is finished. However, rvalue references and const lvalue references have a special ability: when bound to a temporary object, they extend the lifetime of that temporary, keeping it alive until the end of the reference's scope.

```cpp
const int& cr = 42;       // const 引用延长了 42 的生命周期
std::cout << cr << "\n";   // OK：42 还活着

int&& rr = 100;            // 右值引用也延长了 100 的生命周期
std::cout << rr << "\n";   // OK：100 还活着
```

These two behave identically in terms of extending lifetime, but the difference is that `rr` is non-const—you can modify it. This might look a bit weird; how can a literal like `100` be modified? In reality, the compiler places this temporary value into a storage location behind the scenes, and `rr` points to that space.

```cpp
int&& rr = 100;
rr = 200;                  // 合法！rr 指向的存储空间被修改了
std::cout << rr << "\n";   // 输出 200
```

This feature isn't used much in practice, but understanding it helps dispel the fear that "an rvalue reference will immediately dangle." When you write `std::string&& ref = std::move(name);`, the object pointed to by `ref` won't disappear on the very next line—it stays alive until the end of `ref`'s scope.

## Practical Example — Copying vs. Moving in String Concatenation

Let's put together what we've learned so far and look at a real-world example. Suppose we are building a log message:

```cpp
#include <iostream>
#include <string>
#include <vector>

std::string build_log_message(
    const std::string& level,
    const std::string& module,
    const std::string& detail)
{
    std::string msg = "[" + level + "] " + module + ": " + detail;
    return msg;
}

int main()
{
    std::string log = build_log_message("ERROR", "Network", "Connection timeout");
    std::cout << log << "\n";
    return 0;
}
```

The `"[" + level + "] " + module + ": " + detail` here generates a large number of temporary `std::string` objects—each `+` creates a new temporary string. In the C++03 world, every `+` resulted in a memory allocation and a data copy. After C++11, things improved—if `operator+` takes a value parameter and returns a named local variable, the compiler will automatically trigger an **implicit move** upon return, and subsequent concatenation steps pass around moved temporary objects, transferring only the internal pointer instead of copying the character data. Of course, C++17's guaranteed copy elision goes a step further: when `operator+` returns a prvalue, even the move construction can be eliminated.

A more direct benefit comes from function returns. `build_log_message` returns `msg`, and the compiler has two optimization mechanisms here: NRVO (Named Return Value Optimization) can directly eliminate this copy; failing that, even if NRVO doesn't kick in, C++11 will automatically treat `msg` as an rvalue (implicit move), invoking the move constructor of `std::string`—transferring only the internal pointer without copying the character data.

Let's look at another example of transferring container elements:

```cpp
std::vector<std::string> names;

std::string name = "Alice";
names.push_back(std::move(name));  // 移动：name 的内部数据转移到 vector 中
// name 现在处于有效但未指定的状态，不要再使用它

names.push_back("Bob");   // 先从 const char* 构造临时对象，再移动进 vector
```

The first `push_back` uses move semantics: `std::move(name)` converts `name` to an rvalue reference, and the vector calls the move constructor of `std::string` to construct the new element—the cost is transferring one pointer and two `size_t` values, rather than copying the entire string contents. The second `push_back("Bob")` looks like it "constructs directly," but what actually happens is: `"Bob"` first creates a temporary object through `std::string`'s `const char*` constructor, and then this temporary object is passed as an rvalue into the `push_back(T&&)` overload, where it is move-constructed into the vector's storage. In other words, it involves one extra step of temporary object construction compared to `push_back(std::move(name))`, but it still only performs a move without a deep copy. If you truly want to skip the temporary object construction and achieve genuine in-place construction, you should use `emplace_back("Bob")`—it directly calls `std::string`'s constructor in the vector's storage space.

We can use a class with tracing to verify this:

```cpp
// push_back_inplace.cpp -- push_back vs emplace_back 行为对比
// GCC 15, -O0 -std=c++17
```

```bash
g++ -std=c++17 -O0 -o /tmp/push_back_inplace push_back_inplace.cpp && /tmp/push_back_inplace
```

```text
=== push_back(TrackedString("Bob")) ===
  [ctor from const char*] "Bob"
  [move ctor] "Bob"
  [dtor] ""
=== done ===

=== emplace_back("Alice") ===
  [ctor from const char*] "Alice"
=== done ===
```

The output is clear: `push_back(TrackedString("Bob"))` first constructs a temporary object, then moves it in, and the temporary object destructs—two construction steps. `emplace_back("Alice")` only has one line of output; it constructs directly in-place in the vector's storage, skipping the move step entirely. Returning to the `std::string` scenario in this article, the process for `push_back("Bob")` is the same: `"Bob"` is first implicitly converted to a temporary `std::string`, which is then moved into the vector. If you are pursuing ultimate zero-overhead, `emplace_back` is the correct choice.

## Hands-on Experiment — rvalue_demo.cpp

Let's write a complete program to run through the binding rules of rvalue references, the behavior of `std::move`, and the lifetime of temporary objects.

```cpp
// rvalue_demo.cpp -- 右值引用与值类别演示
// Standard: C++17

#include <iostream>
#include <string>
#include <utility>

class Tracker
{
    std::string name_;
    static int kDefaultId;

public:
    explicit Tracker(std::string name)
        : name_(std::move(name))
    {
        std::cout << "  [" << name_ << "] 构造\n";
    }

    Tracker(const Tracker& other)
        : name_(other.name_ + "_copy")
    {
        std::cout << "  [" << name_ << "] 拷贝构造\n";
    }

    Tracker(Tracker&& other) noexcept
        : name_(std::move(other.name_))
    {
        other.name_ = "(moved-from)";
        std::cout << "  [" << name_ << "] 移动构造\n";
    }

    ~Tracker()
    {
        std::cout << "  [" << name_ << "] 析构\n";
    }

    Tracker& operator=(const Tracker& other)
    {
        name_ = other.name_ + "_copy";
        std::cout << "  [" << name_ << "] 拷贝赋值\n";
        return *this;
    }

    Tracker& operator=(Tracker&& other) noexcept
    {
        name_ = std::move(other.name_);
        other.name_ = "(moved-from)";
        std::cout << "  [" << name_ << "] 移动赋值\n";
        return *this;
    }

    const std::string& name() const { return name_; }
};

int Tracker::kDefaultId = 0;

/// @brief 返回临时对象（prvalue）
Tracker make_tracker(std::string name)
{
    return Tracker(std::move(name));
}

int main()
{
    std::cout << "=== 1. 基本构造 ===\n";
    Tracker a("A");
    std::cout << '\n';

    std::cout << "=== 2. 拷贝构造 ===\n";
    Tracker b = a;
    std::cout << "  a.name = " << a.name() << "\n";
    std::cout << "  b.name = " << b.name() << "\n\n";

    std::cout << "=== 3. 移动构造（显式 std::move）===\n";
    Tracker c = std::move(a);
    std::cout << "  a.name = " << a.name() << "\n";
    std::cout << "  c.name = " << c.name() << "\n\n";

    std::cout << "=== 4. 返回临时对象 ===\n";
    Tracker d = make_tracker("D");
    std::cout << "  d.name = " << d.name() << "\n\n";

    std::cout << "=== 5. 移动赋值 ===\n";
    d = std::move(b);
    std::cout << "  b.name = " << b.name() << "\n";
    std::cout << "  d.name = " << d.name() << "\n\n";

    std::cout << "=== 6. 程序结束，析构顺序 ===\n";
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o rvalue_demo rvalue_demo.cpp
./rvalue_demo
```

Expected output is similar to:

```text
=== 1. 基本构造 ===
  [A] 构造

=== 2. 拷贝构造 ===
  [A_copy] 拷贝构造
  a.name = A
  b.name = A_copy

=== 3. 移动构造（显式 std::move）===
  [A] 移动构造
  a.name = (moved-from)
  c.name = A

=== 4. 返回临时对象 ===
  [D] 构造
  d.name = D

=== 5. 移动赋值 ===
  [A_copy] 移动赋值
  b.name = (moved-from)
  d.name = A_copy

=== 6. 程序结束，析构顺序 ===
  [A_copy] 析构
  [A] 析构
  [(moved-from)] 析构
  [(moved-from)] 析构
```

Let's analyze this output step by step. In step 2, `Tracker b = a;` triggers copy construction—`a` is an lvalue, so it can only match the copy constructor, and `b`'s name becomes `"A_copy"`. In step 3, `std::move(a)` converts `a` to an rvalue reference, matching the move constructor—`c`'s name becomes `"A"` (stolen from `a`), while `a`'s name becomes `"(moved-from)"`.

Step 4 is the most interesting. `make_tracker("D")` constructs a `Tracker("D")` inside the function and then returns it. Notice there is only one construction in the output—no copy, no move. This is because of C++17's **guaranteed copy elision**: when returning a prvalue, the compiler directly constructs the object in the caller's space, eliminating even the move. This is why we will dedicate the next article to discussing RVO and NRVO.

The move assignment in step 5 is also worth noting. `d = std::move(b);` transfers the resources of `b` to `d`—`d`'s original name `"D"` is overwritten with `"A_copy"`, and `b` becomes `"(moved-from)"`. During this process, `d`'s original resources (the memory holding `"D"`) are correctly released, because the move assignment operator must ensure old resources are cleaned up before overwriting them.

## Run Online

Run the rvalue reference example online to trace the complete process of construction, copying, moving, and destruction:

<OnlineCompilerDemo
  title="Rvalue References and Value Categories: Construction, Copy, Move, and Destruction Tracing"
  source-path="code/examples/vol2/01_rvalue_reference.cpp"
  description="Run online and observe the order of construction, copy construction, move construction, and destruction of Tracker objects."
  allow-run
/>

## Summary

In this article, we laid a solid foundation for rvalue references. C++'s value category system is divided into three categories—lvalue, xvalue, and prvalue—which intersect along the two dimensions of "has identity" and "can be moved from." An rvalue reference `T&&` can only bind to an rvalue (prvalue or xvalue), which ensures we don't accidentally steal resources from an lvalue that is still in use. `std::move` is essentially a `static_cast<T&&>`; it doesn't perform any move operation—the ones actually moving resources are the move constructor and move assignment operator. When a temporary object is bound to an rvalue reference, its lifetime is extended until the end of the reference's scope.

These concepts might seem abstract, but they form the foundation of the entire move semantics edifice. In the next article, we will build on this foundation—implementing the move constructor and move assignment operator to truly achieve zero-copy resource transfers.
