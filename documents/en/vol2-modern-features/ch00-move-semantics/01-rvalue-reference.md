---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: Understand the C++ value category system and master the binding rules and core semantics of rvalue references
difficulty: intermediate
order: 1
platform: host
prerequisites:
- Volume One: C++ Fundamentals
reading_time_minutes: 24
related:
- Move Construction and Move Assignment
- 'Perfect Forwarding: Preserving Value Categories Exactly'
tags:
- host
- cpp-modern
- intermediate
- 移动语义
title: 'Rvalue References: From Copy to Move'
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/01-rvalue-reference.md
  source_hash: e69fb2afda1c34dad7d8189930e7e508a1d87d94d765a02c2f6382fbcbae8517
  translated_at: '2026-09-25T14:15:17+00:00'
  engine: anthropic
  token_count: 6500
---
# Rvalue References: From Copy to Move

Welcome to modern C++! The term "modern C++" generally refers to C++11 and everything after it—and the feature changes since then are more than worth a proper deep dive.

> Some folks will take issue with this. I've been flamed in conversation myself for asking whether C++11 even counts as "modern." Well... fair point. From 2026, when I'm writing this, these features have already been around for over a decade—chronologically speaking, they really aren't modern anymore. But compared with relics like C++98, the feature changes are substantial. And that is exactly why this volume gets its own separate treatment!

When I first got into C++ and read that book, *Effective Modern C++*, I never quite understood the concept of "rvalue references." The very phrase "rvalue reference" exuded an indescribably academic odor—what is `T&&`? How do you actually tell lvalues from rvalues? Does `std::move` really "move" anything? Every time I saw `std::move` in other people's code, I would copy it over with half an understanding, praying it would compile. Now that I'm the one doing the writing, I want to get these things straight—or at the very least, avoid embarrassingly basic mistakes!

> Still rambling: I'm honestly a bit scared of C++ language lawyers. Every time I pick up the pen to write something, I'm afraid these big shots will mock me. But rigor is always a good thing—write C++ without rigor, and beware being shaken awake at night by a memory explosion and then getting thoroughly worked over by your linker. That said, for teaching purposes there is no need to obsess over every detail right from the start. Otherwise you risk missing the forest for the trees.

## Starting from a Problem That Spikes Your Blood Pressure

Consider a scenario everyone knows: string processing. Quite a few people feel that std::string is sometimes too heavy and wish for a read-only view of a string. const char* isn't a bad fit, but NULL termination is a mess (designating a `\0` as the boundary constraint is sometimes unreliable). Fine then—let's build our own StringWrapper!

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

    // Copy constructor: deep copy
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

Then we write a piece of code that looks perfectly innocent:

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

Without move semantics and with the compiler not applying NRVO (named return value optimization), returning `result` from `build_greeting` triggers the copy constructor—a fresh block of memory is allocated, and the string inside `result` is copied over byte by byte. Then `result` itself is destroyed, releasing its original block of memory. (Think about it—doesn't that make your scalp tingle?)

> Of course, in reality the GCC and MSVC of the C++03 era already shipped NRVO widely as a compiler extension, so this analysis is discussing the worst case where NRVO doesn't kick in.

We spend one memory allocation plus one byte-by-byte copy just to "relocate" the data of an object that is about to be destroyed anyway. If the string is long—say, a JSON payload of several KB—this copy looks downright wasteful: **the source object is dying anyway; the data sitting in that memory will simply go to waste—why not just take over control of that memory directly?**

This is the core problem move semantics set out to solve. And to understand move semantics, we must first understand how C++ classifies expressions—the so-called **value category**.

## A Panorama of Value Categories

Before C++11, things were fairly simple: **an expression was either an lvalue or an rvalue.** That's it. Then C++11 arrived, and once ownership of resources could be moved, the classification became more elaborate.

- Every expression belongs to exactly one of **lvalue**, **xvalue**, or **prvalue**.
- These three can in turn be pairwise combined into broader categories: **glvalue** (generalized lvalue) = lvalue + xvalue, and **rvalue** = xvalue + prvalue.

If this taxonomy feels a bit convoluted, don't worry—it took me a long time to untangle it at first, too. We can approach it through two properties: **has identity** (the expression has a name and you can take its address) and **can be moved from** (the expression is temporary and its resources can be safely "stolen").

Something with identity that cannot be moved from is an **lvalue**.

For example, take `x` in the ordinary variable `int x = 10;`—it has a name, has an address, and its lifetime hasn't ended yet, so of course you can't just steal its resources. Something with identity that can be moved from is an **xvalue** (expiring value)—for instance, the result of `std::move(x)`, which tells you "this object has identity, but it's about to die, and you can safely steal its resources." Something without identity that can be moved from is a **prvalue** (pure rvalue)—for instance, the literal `42` or a temporary object returned by a function; it never had a name in the first place, so you don't need to worry about who might access it after the theft.

Let's look at a concrete set of examples to sort the three categories out clearly.

```cpp
int x = 10;            // x is an lvalue
int&& r = std::move(x); // std::move(x) is an xvalue
int y = x + 1;         // x + 1 is a prvalue
int z = 42;            // 42 is a prvalue
```

Here `x` is the most typical lvalue—it has a name, has an address, and `&x` is a valid expression (of course you can grab this variable's address on the stack!). `std::move(x)` produces an xvalue: it points to the same memory as `x`, but it is semantically marked as "about to expire." `x + 1` and `42` are both prvalues—temporary, unnamed values.

A classic misconception is that "lvalues can appear on the left of the assignment sign, while rvalues can only appear on the right." That claim was roughly true in the C era, but in C++ it is neither sufficient nor necessary. In `const int cx = 10;`, `cx` is an lvalue, yet `cx = 20;` won't compile—const restricts modification but doesn't change the value category. Conversely, `std::string("hello")` is a prvalue, yet after C++11 it can appear on the left of the assignment sign in certain situations (for example, when calling a member function on it).

### Giving Any Expression a Value Category Checkup

So the rules are clear now—but when you're handed an unfamiliar expression, how do you actually confirm whether it's an lvalue, an xvalue, or a prvalue? You can't just wing it every time. Here's a ready-made trick: `decltype` deduces different types for an **identifier** than for a **parenthesized expression**.

- `decltype(x)` (the unparenthesized identifier) yields the **declared type** of `x`;
- `decltype((x))` (the parenthesized expression) goes by value category: an lvalue yields `T&`, an xvalue yields `T&&`, and a prvalue yields `T` itself.

Plug this difference into `is_lvalue_reference_v` / `is_rvalue_reference_v`, and you can give any expression a "value category checkup":

```cpp
// value_category_probe.cpp -- use decltype to give any expression a value category checkup
// Standard: C++17

#include <iostream>
#include <type_traits>
#include <utility>

template <class T>
constexpr const char* value_category()
{
    if constexpr (std::is_lvalue_reference_v<T>) {
        return "lvalue";
    } else if constexpr (std::is_rvalue_reference_v<T>) {
        return "xvalue";
    } else {
        return "prvalue";
    }
}

// decltype((expr)) deduces the type by the expression's value category: lvalue gets T&, xvalue gets T&&, prvalue gets T
#define SHOW(expr) \
    std::cout << "  " #expr "  ->  " << value_category<decltype((expr))>() << "\n"

int g = 100;  // global variable

int main()
{
    int x = 10;        // ordinary variable
    int& lref = x;     // lvalue reference
    int&& rref = 20;   // rvalue reference (but the name rref itself is an lvalue!)

    std::cout << "--- 变量与引用 ---\n";
    SHOW(x);
    SHOW(lref);
    SHOW(rref);          // counterintuitive: a named rvalue reference is an lvalue

    std::cout << "\n--- 字面量与运算 ---\n";
    SHOW(42);
    SHOW(x + 1);
    SHOW(std::move(x));  // the product of std::move is an xvalue

    std::cout << "\n--- 解引用与成员 ---\n";
    SHOW(*(&x));         // dereferencing yields an lvalue
    SHOW(g);

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -O0 -Wall -o value_category_probe value_category_probe.cpp
./value_category_probe
```

```text
--- 变量与引用 ---
  x  ->  lvalue
  lref  ->  lvalue
  rref  ->  lvalue

--- 字面量与运算 ---
  42  ->  prvalue
  x + 1  ->  prvalue
  std::move(x)  ->  xvalue

--- 解引用与成员 ---
  *(&x)  ->  lvalue
  g  ->  lvalue
```

The line to stare at is the `rref` one—it is declared as an rvalue reference `int&& rref`, yet the checkup says `lvalue`. This is not a bug: `rref` is a variable **with a name**, and C++'s rule is "an expression with a name is an lvalue." The xvalue produced by `std::move(x)`, once you give it a name (assign it to a `T&&` variable, or pass it through as a function parameter), "demotes" back to an lvalue and will never automatically trigger a move again. Perfect forwarding is what solves this—we'll take it apart in the fourth article.

With this probe in hand, any expression you're unsure about can be confirmed by running code. No more guessing.

## The Binding Rules of Rvalue References

With value categories under our belt, let's see what an rvalue reference—`T&&`—can actually bind to. The rule is really simple: **an rvalue reference can only bind to rvalues (prvalues or xvalues), never to lvalues**.

```cpp
int x = 10;

int&& r1 = 42;           // OK: 42 is a prvalue
int&& r2 = x + 1;        // OK: x + 1 is a prvalue
int&& r3 = std::move(x); // OK: std::move(x) is an xvalue

// int&& r4 = x;         // compile error: x is an lvalue and cannot bind to an rvalue reference
```

If you uncomment the last line, GCC will hand you a rather blunt error message:

```text
error: cannot bind rvalue reference of type 'int&&' to lvalue of type 'int'
```

The value category taxonomy and the rvalue reference binding rules have been turned into an animation—you can play it, pause it, or single-step through it with the step controls:

<Anim id="lvalue-rvalue" />

The intuition behind this binding rule: rvalue references are designed to let you "take over" a temporary object's resources. If an object is an lvalue (it has a name, has an address, and someone is still using it), how could you safely steal from it? The compiler stopping you here is entirely for safety's sake.

But let's put a pin in something: this "T&& binds only to rvalues" rule refers to rvalue references with a **hard-coded type**, such as `int&&` or `std::string&&`. When we get to perfect forwarding in the fourth article, we'll meet `T&&` inside templates—called a **forwarding reference**—which binds to both lvalues and rvalues and plays by a different set of rules entirely. Don't take this article's conclusions and apply them to `T&&` in templates; you'll hit a wall.

Now let's compare the binding behavior of rvalue references versus const lvalue references—this is crucial for understanding the move constructor coming up later.

The const lvalue reference `const T&` is C++'s "universal receiver"—it binds to anything: lvalues, rvalues, const, non-const, no questions asked. The rvalue reference `T&&` is the "picky receiver"—it accepts rvalues only. The difference looks simple, but it leads to a very important practical distinction: when you receive an rvalue through `const T&`, you have promised not to modify it, so there is no way to steal its resources; when you receive an rvalue through `T&&`, you hold the permission to modify it, so you can safely transfer the resources away.

```cpp
void process_const_ref(const std::string& s)
{
    // s can be read, but not modified
    // so there is no way to "steal" s's internal buffer
    std::cout << s.size() << "\n";
}

void process_rvalue_ref(std::string&& s)
{
    // s is a non-const rvalue reference and can be modified
    // so s's internal resources can be safely transferred away
    std::string stolen = std::move(s);
    // at this point s is in a "valid but unspecified" state
}
```

You might ask: why not let rvalue references bind to lvalues too? Good question. We know move semantics is all about expressing a transfer of ownership. An lvalue is a variable with its own independent address, in charge of its own resources—it inherently conflicts with the semantics of "not intended to be moved from." So deep down you never wanted `T&&` to bind to everything in the first place! If it did, there would be no way to distinguish "this object is safe to steal from" from "this object is still in use"—and that distinction is precisely the fundamental reason move semantics exists.

## The Essence of std::move—A Carefully Wrapped Type Conversion

The name `std::move` is probably one of the most misleading names in the history of C++. It sounds like it "moves" something, but in fact it **moves nothing at all**. `std::move` does exactly one thing: **convert its argument to an rvalue reference**, that is, `static_cast<T&&>`. That's it—no more, no less.

We can implement an equivalent `move` ourselves:

```cpp
template<typename T>
constexpr typename std::remove_reference<T>::type&&
my_move(T&& t) noexcept
{
    return static_cast<typename std::remove_reference<T>::type&&>(t);
}
```

What this code does is very direct: whatever type `T` comes in as, first strip off any reference it might carry with `remove_reference`, then `static_cast` it to an rvalue reference. Throughout the whole process, no data is moved, copied, or modified—it is purely a type conversion.

So what good is it? The key lies in **the signatures of the move constructor and the move assignment operator**. When you write `std::string a = std::move(b);`, `std::move(b)` converts `b` into a `std::string&&`, and that rvalue reference goes on to match `std::string`'s move constructor `std::string(std::string&& other)`. The move constructor is the one that actually performs the "resource transfer"—it steals `other`'s internal buffer pointer and nulls out `other`'s own pointer. `std::move` merely hands over the key from the sidelines.

```cpp
std::string a = "Hello";
std::string b = std::move(a);  // std::move only converts the type
                                  // the move constructor does the actual resource transfer
// at this moment a is in a "valid but unspecified" state
// in most implementations a becomes an empty string, but you should not rely on this behavior
```

Here's a pitfall that is remarkably easy to step into: **using `std::move` on fundamental types yields no performance gain in logical terms** (out of fear of compiler optimizations, I dare not issue a verdict). `std::move(42)` merely converts an `int` into an `int&&`, but for an `int`, "moving" and "copying" are the same thing—both come down to copying four bytes. The power of move semantics only shows in **classes that manage resources**—classes holding dynamic memory, file handles, network connections, and the like.

## The Lifetime of Temporary Objects—What Rvalue References Extend

In C++, the lifetime of a temporary object (a prvalue) normally ends when the full expression containing it finishes. But rvalue references and const lvalue references have a special ability: when bound to a temporary object, they extend that temporary's lifetime, letting it live until the end of the reference's scope.

```cpp
const int& cr = 42;       // the const reference extends the lifetime of 42
std::cout << cr << "\n";   // OK: 42 is still alive

int&& rr = 100;            // the rvalue reference also extends the lifetime of 100
std::cout << rr << "\n";   // OK: 100 is still alive
```

The two behave identically when it comes to lifetime extension; the difference is that `rr` is non-const—you can modify it. This looks a bit strange: how can the literal `100` be modified? In reality, behind the scenes the compiler puts this temporary value into a piece of storage, and `rr` points at exactly that storage.

```cpp
int&& rr = 100;
rr = 200;                  // legal! The storage rr points to has been modified
std::cout << rr << "\n";   // prints 200
```

This feature rarely comes up in practice, but understanding it helps you shake off the fear of "does an rvalue reference dangle right away?" When you write `std::string&& ref = std::move(name);`, the object `ref` points to won't vanish on the next line—it lives all the way to the end of `ref`'s scope.

But there's a boundary here to watch closely—**lifetime extension only applies to "direct binding" and does not cross function boundaries**. Once a temporary is relayed through a function, the extension rule no longer applies. The classic way to faceplant is this:

```cpp
// lifetime_dangle.cpp -- the boundary of temporary object lifetime extension
// Standard: C++17
// To compile (enable ASan's use-after-scope to catch the dangling read):
//   g++ -std=c++17 -O0 -g -fsanitize=address -fsanitize-address-use-after-scope \
//       -o lifetime_dangle lifetime_dangle.cpp

#include <iostream>
#include <string>

// returns the parameter reference as-is
const std::string& pass_through(const std::string& s)
{
    return s;
}

int main()
{
    std::cout << std::unitbuf;  // flush eagerly so output is visible before ASan aborts

    // Case 1: const& directly binds a temporary -- lifetime extended, safe
    const std::string& safe = std::string("I am a temp");
    std::cout << "1) 直接绑定临时对象: \"" << safe << "\"\n";

    // Case 2: const& binds "a reference returned from a function" that points to a temporary -- no extension, dangling
    const std::string& dead = pass_through(std::string("I am passed"));
    std::cout << "2) 经函数返回的引用: \"" << dead << "\n";  // use-after-scope

    return 0;
}
```

Case 1 is fine: the `const&` catches the temporary directly, and the temporary lives until `safe` leaves scope. Case 2 is where it goes wrong—the temporary `"I am passed"` is bound to the function parameter `s`, and per the standard it only lives until **the end of this full expression**; meanwhile `dead` receives the reference returned by `pass_through`, and this kind of "relayed through a function" binding triggers no extension. So the moment the expression ends, the temporary is destroyed, and what the next line reads out of `dead` is a "corpse."

It's hard to see with the naked eye—without sanitizers it might "happen to" still print the old contents, because that memory hasn't been overwritten yet, and that is exactly what makes dangling references so insidious. Turn on ASan's use-after-scope, and the truth shows itself instantly:

```text
1) 直接绑定临时对象: "I am a temp"
2) 经函数返回的引用: "=================================================================
==PID==ERROR: AddressSanitizer: stack-use-after-scope on address 0x... at pc 0x...
    #2 ... in main lifetime_dangle.cpp:26
SUMMARY: AddressSanitizer: stack-use-after-scope ... in std::__ostream_insert
```

(The process ID `PID` and the address `0x...` differ on every run, so they're elided here; the error type, the line numbers, and the SUMMARY are fixed.) Case 1 prints normally; Case 2 gets caught red-handed by ASan the instant it tries to read `dead`. Boil the rule down to one sentence: **a temporary's lifetime extension recognizes "direct binding" only—a reference returned from a function is never extended.**

## A Worked Example—Copy and Move in String Concatenation

Let's put what we've learned together and look at a realistic example. Suppose we are building a log message:

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

The expression `"[" + level + "] " + module + ": " + detail` produces a whole pile of temporary `std::string` objects—every `+` creates a new temporary string. In the C++03 world, every `+` meant one memory allocation plus one data copy. After C++11, things improved: if `operator+` takes its parameter by value and returns a named local variable, the compiler automatically triggers an **implicit move** on the return, so the subsequent concatenation stages pass around the moved temporary, transferring internal pointers instead of copying character data. And of course, C++17's guaranteed copy elision goes one step further: when `operator+` returns a prvalue, even the move construction can be omitted.

The more direct payoff comes from returning from a function. `build_log_message` returns `msg`, and the compiler has two optimization tools here: NRVO (named return value optimization) can eliminate the copy outright; and failing that, even if NRVO doesn't apply, C++11 automatically treats `msg` as an rvalue (implicit move), invoking `std::string`'s move constructor—transferring the internal pointer only, without copying character data.

Now for an example of transferring elements into a container:

```cpp
std::vector<std::string> names;

std::string name = "Alice";
names.push_back(std::move(name));  // move: name's internal data is transferred into the vector
// name is now in a valid but unspecified state; do not use it again

names.push_back("Bob");   // first constructs a temporary from const char*, then moves it into the vector
```

The first `push_back` uses move semantics: `std::move(name)` turns `name` into an rvalue reference, and the vector calls `std::string`'s move constructor to build the new element—the cost is transferring one pointer and two `size_t`s, instead of copying the entire string's content. The second `push_back("Bob")` looks like "direct construction," but what actually happens is: `"Bob"` first creates a temporary via `std::string`'s `const char*` constructor, and then that temporary, as an rvalue, goes into the `push_back(T&&)` overload and is move-constructed into the vector's storage. In other words, compared with `push_back(std::move(name))` it has one extra step—the temporary's construction—but it still performs exactly one move and no deep copy. If you truly want to skip the temporary's construction and achieve genuine in-place construction, use `emplace_back("Bob")`—it calls `std::string`'s constructor directly in the vector's storage.

We can verify this with a tracing class:

```cpp
// push_back_vs_emplace.cpp -- construction/move/destruction tracing for push_back vs emplace_back
// Standard: C++17

#include <iostream>
#include <string>
#include <utility>
#include <vector>

class TrackedString
{
    std::string data_;

public:
    explicit TrackedString(const char* s) : data_(s)
    {
        std::cout << "  [ctor from const char*] \"" << data_ << "\"\n";
    }

    TrackedString(const TrackedString& other) : data_(other.data_)
    {
        std::cout << "  [copy ctor] \"" << data_ << "\"\n";
    }

    TrackedString(TrackedString&& other) noexcept : data_(std::move(other.data_))
    {
        std::cout << "  [move ctor] \"" << data_ << "\"\n";
    }

    ~TrackedString()
    {
        std::cout << "  [dtor] \"" << data_ << "\"\n";
    }
};

int main()
{
    std::cout << "=== push_back(TrackedString(\"Bob\")) ===\n";
    {
        std::vector<TrackedString> v;
        v.push_back(TrackedString("Bob"));
        std::cout << "=== done ===\n";
    }

    std::cout << "\n=== emplace_back(\"Alice\") ===\n";
    {
        std::vector<TrackedString> v;
        v.emplace_back("Alice");
        std::cout << "=== done ===\n";
    }

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -O0 -Wall -o push_back_vs_emplace push_back_vs_emplace.cpp
./push_back_vs_emplace
```

```text
=== push_back(TrackedString("Bob")) ===
  [ctor from const char*] "Bob"
  [move ctor] "Bob"
  [dtor] ""
=== done ===
  [dtor] "Bob"

=== emplace_back("Alice") ===
  [ctor from const char*] "Alice"
=== done ===
  [dtor] "Alice"
```

The output is crystal clear: `push_back(TrackedString("Bob"))` has three lines before `=== done ===`—first the temporary is constructed, then it is moved into the vector, and then the temporary is destroyed: a two-step construction. The `[dtor] "Bob"` line after `=== done ===` is the vector destroying the element it holds when it leaves the `{ }` scope—it has nothing to do with the construction process. Meanwhile, `emplace_back("Alice")` has only one `ctor` line before `=== done ===`: it constructs in place, directly in the vector's storage, skipping even the move. Back in the article's `std::string` scenario, `push_back("Bob")` goes through the same process: `"Bob"` first implicitly converts to a temporary `std::string`, then gets moved into the vector. If you are after the ultimate zero overhead, `emplace_back` is the right choice.

## Hands-On Experiment—rvalue_demo.cpp

Let's write a complete program that exercises the rvalue reference binding rules, the behavior of `std::move`, and the lifetimes of temporary objects, all in one go.

```cpp
// rvalue_demo.cpp -- rvalue references and value categories demo
// Standard: C++17

#include <iostream>
#include <string>
#include <utility>

class Tracker
{
    std::string name_;

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

/// @brief returns a temporary object (prvalue)
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

The expected output looks like this:

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

Let's analyze this output step by step. In step 2, `Tracker b = a;` triggers the copy constructor—`a` is an lvalue, so it can only match the copy constructor, and `b`'s name becomes `"A_copy"`. In step 3, `std::move(a)` converts `a` into an rvalue reference, matching the move constructor—`c`'s name becomes `"A"` (stolen from `a`), while `a`'s name becomes `"(moved-from)"`.

Step 4 is the most interesting one. `make_tracker("D")` constructs a `Tracker("D")` inside the function and then returns it. Notice there is exactly one construction in the output—no copy, and no move. This is C++17's **guaranteed copy elision**: when returning a prvalue, the compiler constructs the object directly in the caller's space, skipping even the move. That is exactly why the next article is dedicated to RVO and NRVO.

The move assignment in step 5 is worth a look too. `d = std::move(b);` transfers `b`'s resources to `d`—`d`'s original name `"D"` gets overwritten with `"A_copy"`, and `b` becomes `"(moved-from)"`. During this process, `d`'s original resource (the memory holding `"D"`) is properly released, because the move assignment operator must ensure the old resources are cleaned up before overwriting.

## Run It Online

Run the rvalue reference example online and trace the full sequence of construction, copy, move, and destruction:

<OnlineCompilerDemo
  title="Rvalue References and Value Categories: Tracing Construction, Copy, Move, and Destruction"
  source-path="code/examples/vol2/01_rvalue_reference.cpp"
  description="Run online and observe the order in which Tracker objects are constructed, copy-constructed, move-constructed, and destroyed."
  allow-run
/>

The next article puts these rules into code—equipping `StringWrapper` with move construction and move assignment, so that `build_greeting`'s return goes from a copy to a zero-cost transfer.
