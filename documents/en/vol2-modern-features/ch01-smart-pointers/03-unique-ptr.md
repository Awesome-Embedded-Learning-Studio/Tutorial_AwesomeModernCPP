---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: A deep dive into unique_ptr's implementation, usage, and best practices
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 1: Deep Dive into RAII: The Cornerstone of Resource Management'
reading_time_minutes: 17
related:
- 'Deep Dive into shared_ptr: Shared Ownership and Reference Counting'
- 'Custom Deleters and Intrusive Reference Counting'
tags:
- host
- cpp-modern
- intermediate
- unique_ptr
- 智能指针
title: 'Deep Dive into unique_ptr: The Zero-Overhead Smart Pointer with Exclusive Ownership'
translation:
  source: documents/vol2-modern-features/ch01-smart-pointers/03-unique-ptr.md
  source_hash: 766493127089bfa6685e51d4c41740e4bde914682b00a93b9543c2586de177e4
  translated_at: '2026-09-27T04:52:52+00:00'
  engine: anthropic
  token_count: 3900
---
# Deep Dive into unique_ptr: The Zero-Overhead Smart Pointer with Exclusive Ownership

In the previous article we set up the ownership model: exclusive, shared, and borrowed, each in its proper place. Now let's look at the most direct realization of exclusive ownership: `std::unique_ptr`.

The design philosophy of this class fits in one sentence: **one object, one owner, zero overhead**. No reference counting, no atomic operations, no extra control block allocated—you hand it an object, and it takes good care of it for you; you leave the scope, and it deletes the object for you. It's that simple. (btw, why do interviews love quizzing this thing so much... it's always, always this one...)

But note: simple != shallow. The topics behind `unique_ptr`—ownership semantics, move semantics, custom deleters, the empty base optimization (EBO)—every one of them deserves a deep understanding. So, let's begin our journey!

## Exclusive Ownership: Why It Can't Be Copied

The most essential semantic of `unique_ptr` is "**exclusivity**". Take a calm breath, and think about what exclusivity means.

In other words, **at any given moment**, **only one `unique_ptr` owns the object**. That means copy construction and copy assignment are off the table; only **moving** is allowed. Let's think it through: if we allowed copying, both `unique_ptr`s would believe they own the object, and both would attempt to delete it when leaving scope—a double release. What a Double Free indeed! That goes straight into undefined behavior<RefLink :id="1" preview="cppreference std::unique_ptr — non-copyable, movable-only exclusive ownership" />.

```cpp
#include <memory>
#include <iostream>

struct Widget {
    int value;
    explicit Widget(int v) : value(v) {
        std::cout << "Widget(" << value << ") 构造\n";
    }
    ~Widget() {
        std::cout << "~Widget(" << value << ") 析构\n";
    }
};

void ownership_demo() {
    auto p1 = std::make_unique<Widget>(42);
    // auto p2 = p1;              // compile error! unique_ptr is not copyable
    auto p2 = std::move(p1);      // OK: ownership transfers from p1 to p2

    // now p1 == nullptr, and p2 owns the object
    std::cout << "p1: " << p1.get() << "\n";  // prints: 0 or nullptr
    std::cout << "p2: " << p2.get() << "\n";  // prints: a valid address
    std::cout << "p2->value: " << p2->value << "\n";  // prints: 42
}   // p2 is destroyed, and the Widget is deleted automatically
```

This demo program is right below—click "Try It Out" and it runs directly (the address of `p2` differs on every run; just check that it's non-zero):

<OnlineCompilerDemo
  title="Hands-On: Transferring Exclusive Ownership"
  source-path="code/examples/vol2/24_unique_ptr_ownership.cpp"
  description="Watch the ownership transfer online: after std::move, p1 goes null, p2 takes over the object, and the object is destroyed automatically when the scope ends."
  run-options="-std=c++17"
  allow-run
/>

The three phases—exclusive ownership, copy rejected, move transferred—have been turned into an animation. You can play it, pause it, or single-step through it with the step controls and see every phase clearly:

<Anim id="unique-ownership" />

This "non-copyable, movable" design maps perfectly onto ownership transfer in real life—hand your key to someone else, and you no longer have that key. At the code level, `std::move` hands the raw pointer inside `p1` over to `p2`, then sets `p1` to null. The whole process involves no extra memory allocation and no reference-counting overhead.

## make_unique vs new: Why C++14 Added This Function

C++11 introduced `std::unique_ptr` but forgot to provide `std::make_unique` (widely regarded as an oversight—it feels like the folks on the C++ committee were busy arguing and this slipped their minds...), and our standard library didn't patch the gap until C++14<RefLink :id="2" preview="Herb Sutter, GotW #89 Solution: Smart Pointers, 2013" />. So what advantages does `make_unique` have over direct `new`?

First, **exception safety**. Consider this function call:

```cpp
// suppose we have this function signature
void process(std::unique_ptr<Widget> ptr, int computed_value);

// dangerous style (C++11 era)
process(std::unique_ptr<Widget>(new Widget(42)), compute_something());

// safe style (C++14 era)
process(std::make_unique<Widget>(42), compute_something());
```

In the dangerous version, before calling `process`, the C++ compiler has to get several things done one after another: `new Widget(42)`, constructing the `unique_ptr`, and calling `compute_something()`. Think about it—isn't that a bit dangerous? Yes, it is!

Because **before C++17**, the C++ standard **did not specify the evaluation order of function arguments**—the compiler might `new` first, then call `compute_something()`, and construct the `unique_ptr` last. If `compute_something()` throws, the `new`ed `Widget` leaks—because the `unique_ptr` never got the chance to take it over.

> PS! **Starting with C++17**, the standard requires that the evaluation of arguments **must not interleave**—each argument (including the `unique_ptr` construction) must be fully evaluated before evaluation of the next one begins<RefLink :id="3" preview="cppreference Order of evaluation — C++17: parameters are indeterminately sequenced, order still unspecified" />. Which side goes first is still unspecified, but that is already enough to plug the hole: the `unique_ptr` has either been fully constructed, or the `new` hasn't started yet—the intermediate state of "newed, but nobody has taken it over" no longer exists. So in C++17 and later, the dangerous version is actually safe. Still, `make_unique` keeps its other advantages (terser code, no repeated type names), and it works with older standards, so it remains the recommended practice.

So, let's give `make_unique` the respect it deserves. This thing **wraps allocation and construction in a single function call**—no such "intermediate state" exists—so it is exception-safe<RefLink :id="4" preview="cppreference std::make_unique — Notes on exception safety vs direct new" />.

Second, **code brevity**. `make_unique` keeps bare `new` out of your code, reducing the chance of mistakes:

```cpp
// comparison
auto p1 = std::unique_ptr<Widget>(new Widget(42));  // verbose, and easy to forget the unique_ptr part
auto p2 = std::make_unique<Widget>(42);              // concise, impossible to forget the management
```

That said, `make_unique` has one limitation: it does not support **custom deleters**. If you need a custom deleter (say, to manage a `FILE*` or `malloc`-allocated memory), you have to construct the `unique_ptr` directly. We'll discuss this in detail in the later "Custom Deleters" chapter.

## The Deep Bond Between Move Semantics and unique_ptr

`unique_ptr` and move semantics are tightly bound. Before C++11, C++ had only copy semantics—making a "replica" of an object. But for `unique_ptr`, copying would mean "two pointers pointing at the same object", which violates exclusive ownership. The introduction of move semantics solved exactly this problem: moving is not "copying" but "transferring"—the source object gives up ownership, and the destination takes over.

This is what allows `unique_ptr` to live inside standard containers:

```cpp
#include <memory>
#include <vector>
#include <iostream>

struct Sensor {
    int id;
    explicit Sensor(int i) : id(i) {}
};

int main() {
    std::vector<std::unique_ptr<Sensor>> sensors;

    // push_back needs to move, because unique_ptr is not copyable
    sensors.push_back(std::make_unique<Sensor>(1));
    sensors.push_back(std::make_unique<Sensor>(2));
    sensors.push_back(std::make_unique<Sensor>(3));

    // when the vector reallocates, the internal unique_ptrs transfer via move construction
    // this is also why unique_ptr's move operations are marked noexcept
    for (const auto& s : sensors) {
        std::cout << "Sensor id: " << s->id << "\n";
    }

    // returning a unique_ptr from a function also goes through a move (or RVO)
    auto make_sensor = [](int id) -> std::unique_ptr<Sensor> {
        return std::make_unique<Sensor>(id);
    };

    auto s = make_sensor(99);
    std::cout << "Created sensor " << s->id << "\n";
}
```

Notice, everyone: both the move constructor and the move assignment operator of `unique_ptr` are marked `noexcept`. This has a direct effect on how `std::vector` behaves—when the vector reallocates, if the element's move constructor is `noexcept`, the vector prefers to move; otherwise it falls back to copying (but `unique_ptr` is not copyable, so it must move). Therefore, `noexcept` move operations are the key guarantee that lets `unique_ptr` be stored in containers safely.

## unique_ptr<T[]>: The Array Version

`unique_ptr` has a partial specialization for arrays, `unique_ptr<T[]>`, which calls `delete[]` instead of `delete` on destruction.

```cpp
auto arr = std::make_unique<int[]>(64);  // allocates 64 ints
arr[0] = 42;
arr[1] = 17;
// automatically calls delete[] on destruction
```

But honestly, scenarios in C++ where you need to hand-manage dynamic arrays are already very rare. If you need a fixed-size array, `std::array` or `std::vector` is almost always the better choice. `unique_ptr<T[]>` is mainly for interfacing with C APIs that return dynamically allocated arrays, for example:

```cpp
// suppose some C API returns a malloc-allocated array
extern "C" int* create_buffer(size_t size);
extern "C" void free_buffer(int* buf);

auto buffer = std::unique_ptr<int[], void(*)(int*)>(
    create_buffer(1024),
    [](int* p) { free_buffer(p); }
);
buffer[0] = 42;
```

My strong advice: do not use `unique_ptr<T[]>` as a replacement for `std::vector`. A `vector` gives you `size()`, iterators, bounds checking (via `at()`), and more, while `unique_ptr<T[]>` gives you nothing but automatic release<RefLink :id="5" preview="C++ Core Guidelines R.20-24 — smart pointer rules" />.

## Custom Deleter Basics

The second template parameter of `unique_ptr` is the deleter's type. The default is `std::default_delete<T>`, which simply does `delete ptr` inside. But you can swap in any callable—function pointer, lambda, function object—as long as it fits the `void operator()(T*)` signature.

The most common scenario is managing resources returned from C APIs:

```cpp
#include <cstdio>
#include <memory>

// function pointer as the deleter
using FilePtr = std::unique_ptr<FILE, decltype(&std::fclose)>;

FilePtr open_file(const char* path, const char* mode) {
    FILE* f = std::fopen(path, mode);
    return FilePtr(f, &std::fclose);
}

// lambda as the deleter (no captures → stateless → zero overhead)
auto make_closer = []() {
    auto deleter = [](FILE* f) noexcept { if (f) std::fclose(f); };
    return std::unique_ptr<FILE, decltype(deleter)>(std::fopen("/tmp/log", "w"), deleter);
};
```

Function objects (functors) as deleters are also a common choice, especially when you want the deleter type to have a name:

```cpp
struct FreeDeleter {
    void operator()(void* p) noexcept {
        std::free(p);
    }
};

// manages malloc-allocated memory
auto buf = std::unique_ptr<char, FreeDeleter>(
    static_cast<char*>(std::malloc(256))
);
```

We'll go deeper on custom deleters (stateful deleters, the EBO optimization, deleters in `shared_ptr`, and so on) in the dedicated "Custom Deleters and Intrusive Reference Counting" article.

## Proving Zero Overhead: sizeof and Assembly Analysis

`unique_ptr` is often advertised as a "zero-overhead abstraction", but this is no marketing slogan—we can verify it with actual code. First, the `sizeof` comparison:

```cpp
#include <memory>
#include <iostream>

struct EmptyDeleter {
    void operator()(int* p) noexcept { delete p; }
};

// stateful deleter: carries a data member, so no EBO
struct StatefulDeleter {
    int extra;
    void operator()(int* p) noexcept { delete p; }
};

int main() {
    std::cout << "sizeof(int*):                             " << sizeof(int*) << "\n";
    std::cout << "sizeof(unique_ptr<int>):                  " << sizeof(std::unique_ptr<int>) << "\n";
    std::cout << "sizeof(unique_ptr<int, EmptyDeleter>):    " << sizeof(std::unique_ptr<int, EmptyDeleter>) << "\n";
    std::cout << "sizeof(unique_ptr<int, void(*)(int*)>):   " << sizeof(std::unique_ptr<int, void(*)(int*)>) << "\n";
    std::cout << "sizeof(unique_ptr<int, StatefulDeleter>): " << sizeof(std::unique_ptr<int, StatefulDeleter>) << "\n";
}
```

This verification program is right below—click "Try It Out" and run it directly (on a 64-bit platform):

<OnlineCompilerDemo
  title="Hands-On: The Zero Overhead of unique_ptr"
  source-path="code/examples/vol2/25_unique_ptr_sizeof.cpp"
  description="Compare sizeof online: a unique_ptr with the default deleter or a stateless deleter is 8 bytes (same size as a raw pointer); a function pointer or a stateful deleter doubles it to 16 bytes."
  run-options="-std=c++17"
  allow-run
/>

With the default deleter or a stateless function object, `unique_ptr` is the same size as a raw pointer—8 bytes. That is the work of the empty base optimization (EBO)<RefLink :id="6" preview="Bartlomiej Filipek, Empty Base Class Optimisation, no_unique_address and unique_ptr, C++ Stories, 2021" />: internally, `unique_ptr` usually inherits from the deleter type, and when the deleter is an empty class (no data members), the compiler optimizes its size down to 0, so `unique_ptr` only needs to store that one raw pointer. Once the deleter carries state—a function pointer has to store an address, `StatefulDeleter` has to store `extra`—EBO can't kick in, and the size grows to 16 bytes.

And when a function pointer is used as the deleter, `unique_ptr` has to store that extra function pointer, so the size doubles—16 bytes. There's the precondition for "zero overhead": **the deleter must be stateless**.

Let's verify it from the assembly angle as well. Here is a simple example:

```cpp
// manage an int with unique_ptr
int use_unique_ptr() {
    auto p = std::make_unique<int>(42);
    return *p;
}

// the equivalent raw-pointer version
int use_raw_ptr() {
    int* p = new int(42);
    int v = *p;
    delete p;
    return v;
}
```

With optimizations enabled (`-O2`), the assembly generated for these two functions is almost identical. Save the two functions above into a file, compile with `g++ -std=c++17 -O2 -S`, and you'll see that both produce:

```asm
movl    $42, %eax
ret
```

The compiler inlined and optimized away the `unique_ptr`'s construction and destruction—even the `new` and `delete` were eliminated (because the object's lifetime is very short and has no side effects). This is the power of C++ abstraction: you gain safety and readability at the source level, yet pay nothing at the machine-code level.

## Some Other Interfaces, Say release(), reset(), and get(): Three Key Operations

`unique_ptr` provides a few methods for manually managing ownership, and understanding the differences between them is important.

`get()` returns the internal raw pointer without transferring ownership. This is useful when you need to pass the pointer to a function that only uses it but doesn't own it:

```cpp
void print_widget(const Widget* w);

auto p = std::make_unique<Widget>(42);
print_widget(p.get());  // passed to a read-only function; p still owns the object
```

`release()` gives up ownership and returns the raw pointer—the `unique_ptr` becomes empty, but the object is not deleted. It's the equivalent of saying "I've handed the object to you; releasing it is your responsibility":

```cpp
auto p = std::make_unique<Widget>(42);
Widget* raw = p.release();  // p becomes nullptr, raw points to the object
// ... use raw ...
delete raw;  // you must release it manually
```

`release()` is an operation that calls for caution. The moment you call it, you're back in the raw-pointer world—if you forget the `delete`, you leak memory. In most cases, transferring ownership to another `unique_ptr` with `std::move()` is the better choice.

`reset()` replaces the currently managed object. Called with no argument, it simply releases the current object and empties the pointer:

```cpp
auto p = std::make_unique<Widget>(1);
p.reset(new Widget(2));  // releases Widget(1), takes over Widget(2)
p.reset();               // releases Widget(2), p becomes nullptr
```

The next article moves on to `shared_ptr`—a completely different ownership model: shared ownership. That's where the real complexity begins.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="std::unique_ptr"
    url="https://en.cppreference.com/w/cpp/memory/unique_ptr"
  />
  <ReferenceItem
    :id="2"
    author="Herb Sutter"
    title="GotW #89 Solution: Smart Pointers"
    publisher="herbsutter.com"
    :year="2013"
    url="https://herbsutter.com/2013/05/29/gotw-89-solution-smart-pointers/"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="Order of Evaluation"
    chapter="Rule 14: function argument evaluation (C++17)"
    url="https://en.cppreference.com/w/cpp/language/eval_order"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="std::make_unique"
    chapter="Notes: exception safety"
    url="https://en.cppreference.com/w/cpp/memory/unique_ptr/make_unique"
  />
  <ReferenceItem
    :id="5"
    author="Bjarne Stroustrup / Herb Sutter (eds.)"
    title="C++ Core Guidelines — R.20-R.24: Smart Pointer Rules"
    publisher="isocpp.org"
    url="https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#rr-owner"
  />
  <ReferenceItem
    :id="6"
    author="Bartlomiej Filipek"
    title="Empty Base Class Optimisation, no_unique_address and unique_ptr"
    publisher="C++ Stories"
    :year="2021"
    url="https://www.cppstories.com/2021/no-unique-address/"
  />
</ReferenceCard>
