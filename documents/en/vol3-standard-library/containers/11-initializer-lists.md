---
chapter: 7
cpp_standard:
- 11
- 14
- 17
description: 'A thorough guide to std::initializer_list — the compiler-generated read-only view for {...}, shallow copies and const elements, the "move trap" where elements cannot be moved into containers, the overload priority of brace initialization, and its relationship with container constructors'
difficulty: intermediate
order: 11
platform: host
reading_time_minutes: 6
related:
- 'Deep Dive into vector: Three Pointers, Reallocation, and Iterator Invalidation'
- 'span: A Non-owning Contiguous View'
tags:
- host
- cpp-modern
- intermediate
- 容器
title: 'std::initializer_list: The Lightweight Sequence Behind the Braces'
translation:
  source: documents/vol3-standard-library/containers/11-initializer-lists.md
  source_hash: 42799cf6df7141670397ceb4407ba82559b071934f878db2ec4421e84c738ef7
  translated_at: '2026-09-26T02:31:56+00:00'
  engine: anthropic
  token_count: 1900
---
# std::initializer_list: The Lightweight Sequence Behind the Braces

## What initializer_list Is: The Compiler-Generated Read-Only View for `{...}`

`std::initializer_list` is the standard library type C++11 pairs with brace list initialization. When you write `vector<int>{1, 2, 3}` or `f({1, 2, 3})`, the compiler quietly constructs a `std::initializer_list<int>` behind the scenes to represent the sequence `{1, 2, 3}`. The object itself is featherweight—roughly a pointer plus a length—and, like `span`, it is a view that owns no data.

```cpp
std::initializer_list<int> il = {1, 2, 3};   // compiler-constructed, points at an underlying const int[3]
il.size();        // 3
il.begin();       // points at the first element
il.end();         // one past the end
```

Three properties are key: it does **not own** its elements (they live in a compiler-generated underlying const array), its elements are **const** (read-only), and copying it is a **shallow copy** (you copy that pointer and the length, never the elements). These three decide everything about how it behaves, and they also plant its most famous trap.

## How Lightweight Is It: Shallow Copies, Read-Only Elements

Copying an initializer_list is shallow—copying one just copies its internal pointer (and the length); the underlying const array doesn't budge. That's why passing an initializer_list to a function costs almost nothing, about the same as passing a pointer.

```cpp
void f(std::initializer_list<int> il);   // pass by value, really a shallow copy (pointer + size)
f({1, 2, 3, 4, 5});   // no 5-int copy, just a view being passed
```

But the "elements are const" point deserves a firm spot in your memory: the elements of an initializer_list are `const T`, and non-const access is simply unavailable. It looks harmless, yet combined with move semantics it digs a huge pit—the next section is devoted to it.

## The Move Trap: Elements in `{...}` Can Only Be Copied into Containers

This is the most classic initializer_list trap. You want to tuck a few objects into a vector, so you casually write `vector<T>{a, b, c}`, trusting modern C++ to move them in efficiently—except they get **copied** in.

The root cause is exactly that the elements are const: an initializer_list's elements are `const T`, while a move constructor needs `T&&` (non-const). Constructing a vector from an initializer_list means ferrying every const element into its own storage, and a const can't produce a move—copy is the only option. Even if you write `std::move` inside the braces, that only gets the objects "moved into the initializer_list" (because the construction step there receives rvalues); once inside, they're const, and hauling them on into the vector can only be done by copy.

Let's measure it and count the copies for real, using a type that tallies its copies and moves:

```cpp
#include <iostream>
#include <vector>
#include <string>
#include <utility>

struct Counted {
    std::string s;
    inline static int copies = 0;
    inline static int moves = 0;
    Counted(std::string x) : s(std::move(x)) {}
    Counted(const Counted& o) : s(o.s) { ++copies; }
    Counted(Counted&& o) noexcept : s(std::move(o.s)) { ++moves; }
};

int main()
{
    // Scenario 1: lvalues construct the initializer_list → vector
    {
        Counted a{"a"}, b{"b"}, c{"c"};
        Counted::copies = 0;
        Counted::moves = 0;
        std::vector<Counted> v{a, b, c};
        std::cout << "vector{a,b,c}        : copies=" << Counted::copies
                  << " moves=" << Counted::moves << "\n";
    }
    // Scenario 2: move into the initializer_list → vector (the trap: the hop into the vector is still a copy)
    {
        Counted a{"a"}, b{"b"}, c{"c"};
        Counted::copies = 0;
        Counted::moves = 0;
        std::vector<Counted> v{std::move(a), std::move(b), std::move(c)};
        std::cout << "vector{move(a),...}  : copies=" << Counted::copies
                  << " moves=" << Counted::moves << "\n";
    }
    // Scenario 3: no initializer_list, push_back(move) → all moves
    {
        Counted a{"a"}, b{"b"}, c{"c"};
        Counted::copies = 0;
        Counted::moves = 0;
        std::vector<Counted> v;
        v.reserve(3);
        v.push_back(std::move(a));
        v.push_back(std::move(b));
        v.push_back(std::move(c));
        std::cout << "push_back(move)      : copies=" << Counted::copies
                  << " moves=" << Counted::moves << "\n";
    }
    return 0;
}
```

```bash
g++ -std=c++17 -O2 -o /tmp/init_list_test /tmp/init_list_test.cpp && /tmp/init_list_test
```

```text
vector{a,b,c}        : copies=6 moves=0
vector{move(a),...}  : copies=3 moves=3
push_back(move)      : copies=0 moves=3
```

Read the three scenarios side by side. First, `vector{a, b, c}` (lvalues): 6 copies, 0 moves—3 copies to build the initializer_list's elements, then 3 more copies into the vector. Second, `vector{std::move(a), ...}`: 3 copies, 3 moves—`std::move` gets the objects moved into the initializer_list (saving 3 copies), but the hop into the vector is still 3 copies; const won't move. Third, `push_back(std::move(...))`: 0 copies, 3 moves—sidestep the initializer_list, move straight into the vector, zero copies.

So remember this performance trap: **stuffing several objects into a container, `vector{move(a), ...}` still copies into the vector; only `push_back(move)` is copy-free**. When T is a heavyweight type (a big string, a big vector), this gap is genuine, real-money copying cost.

## Braces First: Why `{...}` Always Grabs the initializer_list Constructor

initializer_list carries one more quirk—an "overload preference": the moment a class's constructors include an `initializer_list` version, brace initialization picks it first, even when another constructor looks like the better fit. The most classic crash site is `vector<int>`:

```cpp
std::vector<int> v1(10, 0);    // parentheses: ten 0s (count + value constructor)
std::vector<int> v2{10, 0};    // braces: two elements, 10 and 0 (initializer_list constructor!)
```

`v1` is ten 0s; `v2` is the two elements `{10, 0}`—identical intent, yet parentheses and braces hand back completely different results, all because braces preferentially matched the `initializer_list<int>` constructor. Not a bug—a rule: brace initialization prefers the `initializer_list` constructor whenever one exists. So when constructing containers, don't blur `(count, value)` and `{a, b}` together; different intent, different brackets.

## A Few Parting Words

`std::initializer_list` is the lightweight view behind brace list initialization: non-owning, const elements, shallow copies. It lets `{1, 2, 3}` travel elegantly into functions and containers, but "const elements" buries two things to remember—one, the move trap (`vector{...}` always copies into the container, so heavyweight types call for `push_back(move)`), and two, brace priority (`{}` will elbow its way to the match whenever an `initializer_list` constructor exists). In the next article we leave initialization behind and look at the memory layout of the types themselves: object size and trivial types.

Want to get your hands on it right away? Open the online demo below (it runs, and you can inspect the assembly too):

<OnlineCompilerDemo
  title="std::initializer_list: Read-Only View and the Move Trap"
  source-path="code/examples/vol3/11_initializer_lists.cpp"
  description="The read-only view the braces generate, the copy trap where const elements cannot be moved, and brace overload priority"
  allow-run
/>

## References

- [std::initializer_list — cppreference](https://en.cppreference.com/w/cpp/utility/initializer_list)
- [List initialization — cppreference](https://en.cppreference.com/w/cpp/language/list_initialization)
