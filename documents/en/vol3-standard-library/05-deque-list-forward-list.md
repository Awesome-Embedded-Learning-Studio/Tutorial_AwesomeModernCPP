---
chapter: 7
cpp_standard:
- 11
- 20
description: 'A deep dive into the three alternatives to `vector` among sequential
  containers: `deque`''s segmented continuous double-ended structure, `list`''s doubly
  linked list and `splice`, and `forward_list`''s extreme memory efficiency, along
  with the real-world trade-offs between cache locality and front insertion complexity.'
difficulty: intermediate
order: 5
platform: host
prerequisites:
- vector 深入：三指针、扩容与迭代器失效
reading_time_minutes: 9
related:
- 容器选择指南
tags:
- host
- cpp-modern
- intermediate
- 容器
title: 'deque, list, and forward_list: Three Alternatives to vector'
translation:
  engine: anthropic
  source: documents/vol3-standard-library/05-deque-list-forward-list.md
  source_hash: 6261f8c5044326c92a14489fabfac97598fdad483b776f39a6a7fb43619a5888
  token_count: 1650
  translated_at: '2026-06-15T09:12:38.685009+00:00'
---
# deque, list, and forward_list: Three Alternatives to vector

## Why do we need these three when vector is already good enough?

We covered `vector` in the [previous article](03-vector-deep-dive.md). It has contiguous memory, O(1) random access, and amortized O(1) insertion at the end. For most scenarios, it is the optimal solution. However, it has a few blind spots: insertion at the head is O(n) (shifting everything forward), insertion in the middle is also O(n), reallocation moves all elements, and iterators/references are invalidated upon expansion. When we encounter requirements like "frequently adding items to the head" or "frequently inserting/deleting at known positions without invalidating iterators," `vector` is no longer suitable. `deque`, `list`, and `forward_list` exist to fill these gaps—they use different memory layouts to gain capabilities that `vector` cannot provide, at the cost of their own specific trade-offs.

Remember this rule of thumb for now: `deque` is a "vector that can insert at both ends," `list` is a "linked list with O(1) insertion/deletion in the middle," and `forward_list` is a "singly linked list that saves more memory than `list`."

## deque: O(1) insertion at both ends, plus random access

`deque` (pronounced "deck," short for double-ended queue) looks the most like `vector`, but it solves the O(n) problem of head insertion in `vector`. Its underlying structure is not a single contiguous block of memory, but **segmented continuity**: a central control array (a map of pointers), where each pointer points to a fixed-size chunk. Elements are stored in these chunks, and memory is contiguous within each chunk.

```cpp
// deque 分段连续的简化骨架（标准库内部，各厂细节不同）
struct Deque {
    std::vector<Block*> control;   // 中控数组，每项指向一个块
    // 每个 Block 是一段连续内存，装若干元素
};
// 随机访问：block = control[i / chunk_size]，元素 = block[i % chunk_size]
```

This structure brings three characteristics. First, **push/pop at both the head and tail are O(1)**: when the tail is full, a new chunk is added; when the head is full, a chunk is added in front (or filled backward within the chunk). Existing elements are not moved—this is its biggest advantage over `vector`. Second, **random access is still O(1)**; `deque` calculates which chunk the element is in, then takes the offset within that chunk. It only involves one extra pointer dereference ("central control → chunk") compared to `vector`, so it is slightly slower. Third, **reallocation does not move all elements**: when a `deque` is full, we only need to expand the central control array (a set of pointers, which is small) and hang new chunks. The addresses of existing elements remain unchanged—this is much gentler than `vector` reallocation (which moves everything and invalidates all iterators).

The price is that memory is not in a single block (unfriendly for scenarios requiring passing data to C interfaces or needing a continuous buffer), and the "central control + multiple chunks" structure itself has some space overhead.

## list: Doubly linked list, O(1) insertion/deletion in the middle + splice

`list` is a doubly linked list, where each node stores `prev` and `next` pointers. Its core selling point is: **insertion and deletion at known positions (having an iterator) is O(1)**—it only changes a few pointers and does not move any other elements. Furthermore, **iterators never become invalid** (insertion/deletion only affects the iterator of the deleted node itself), something even `deque` and `vector` cannot achieve.

`list` also has a unique skill called **splice**: `l1.splice(pos, l2)` can directly "splice" the node chain from `l2` into `l1`. The whole process is O(1) and copies no elements—this is a capability unique to linked lists that contiguous containers cannot offer. It is suitable for scenarios like "moving a segment of one list to another at zero cost."

However, the shortcomings of `list` are also critical. First, **it does not support random access**, there is no `operator[]`, so finding the 1000th element requires walking 1000 steps from the head (O(n)). Second, **it is extremely cache-unfriendly**: nodes are scattered all over the heap. During traversal, CPU prefetching fails and cache misses occur frequently. Later, we will run a demo to show you that traversing a `list` is several times slower than a `vector`, and this is the reason. Therefore, the advantage of "O(1) insertion in the middle" is often offset by "O(n) to find the position" plus "slow traversal"—unless you are actually holding an iterator and inserting/deleting frequently, it might not be worth it.

## forward_list: The extremely memory-efficient singly linked list

`forward_list` is a singly linked list, where each node only stores a `next` pointer, saving one predecessor pointer compared to `list`. It was introduced in C++11 with a clear goal: to match the "zero overhead" of hand-written C singly linked lists—when you only need forward traversal and are memory sensitive (e.g., in embedded systems), there is no need to pay the price of an extra pointer for reverse capabilities you don't use.

The trade-off is naturally that you cannot traverse backwards, and **there is no O(1) `size()`** (you have to walk O(n) to the end), only `empty()` is O(1). The interface is also more streamlined than `list`: it **deliberately does not provide a `size()` member function**—because the standard requires `size()` to be O(1), and a singly linked list cannot maintain this in O(1), so it simply isn't provided. If you need it, you have to count yourself.

## Let's run it: Traversal vs. Head Insertion, Two Completely Opposite Faces

Saying "list traversal is slow" and "vector head insertion is slow" is too abstract. Let's just run it. First, look at traversal: `vector`, `deque`, and `list` each hold one million `int`s, and we traverse to sum them up.

```cpp
#include <iostream>
#include <vector>
#include <deque>
#include <list>
#include <chrono>

int main()
{
    const int N = 1000000;
    std::vector<int> v(N);
    std::deque<int> d(N);
    std::list<int> l;
    for (int i = 0; i < N; ++i) {
        v[i] = i;
        d[i] = i;
        l.push_back(i);
    }

    volatile long long sink = 0;
    auto bench = [&](auto& c, const char* name) {
        auto t0 = std::chrono::high_resolution_clock::now();
        long long s = 0;
        for (auto x : c) {
            s += x;
        }
        sink = s;
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << name << ": "
                  << std::chrono::duration<double, std::milli>(t1 - t0).count() << " ms\n";
    };

    bench(v, "vector ");
    bench(d, "deque  ");
    bench(l, "list   ");
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/traversal /tmp/traversal.cpp && /tmp/traversal
```

```text
vector : 0.3 ms
deque  : 0.44 ms
list   : 1.9 ms
```

(GCC 16.1.1, local machine; the magnitude relationship is stable.) `list` is six times slower than `vector` and four times slower than `deque`—this is the real cost of scattered nodes and cache unfriendliness. Because `deque` is segmented continuous, there is still locality within chunks, so it is significantly faster than `list`, but still slightly slower than `vector` which is one single contiguous block.

Now look at a reversed scenario: inserting one hundred thousand elements at the head.

```cpp
#include <iostream>
#include <vector>
#include <deque>
#include <list>
#include <chrono>

int main()
{
    const int N = 100000;
    volatile int sink = 0;

    {
        std::vector<int> v;
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < N; ++i) {
            v.insert(v.begin(), i);   // 每次 O(n)
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "vector front insert: "
                  << std::chrono::duration<double, std::milli>(t1 - t0).count() << " ms\n";
        sink = v.size();
    }
    {
        std::deque<int> d;
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < N; ++i) {
            d.push_front(i);   // O(1)
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "deque  front insert: "
                  << std::chrono::duration<double, std::milli>(t1 - t0).count() << " ms\n";
        sink = d.size();
    }
    {
        std::list<int> l;
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < N; ++i) {
            l.push_front(i);   // O(1)
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "list   front insert: "
                  << std::chrono::duration<double, std::milli>(t1 - t0).count() << " ms\n";
        sink = l.size();
    }
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/front_insert /tmp/front_insert.cpp && /tmp/front_insert
```

```text
vector front insert: 246 ms
deque  front insert: 0.2 ms
list   front insert: 4.8 ms
```

This time it is completely reversed: `vector` head insertion takes 246ms, while `deque` only takes 0.2ms—a difference of over a thousand times. This is because every `vector::insert` at the head has to move all elements back by one position. Doing this 100,000 times results in O(n²); `deque` and `list` head insertions are both O(1). Note that `deque` is even faster than `list` (`list` has to `malloc` a node every time, while `deque` just fills within a chunk and occasionally adds a chunk). This is also why `deque` beats `list` in "double-ended addition/deletion" scenarios.

Putting these two sets of data together makes it clear: **there is no silver bullet**. Use `vector`/`deque` for traversal-intensive tasks, and `deque`/`list` for frequent head/middle insertion. Choosing wrong leads to order-of-magnitude performance differences.

## Finally, a summary: How to choose

| Requirement | Choice |
|------|----|
| Random access + mainly tail add/delete | `vector` |
| Add/delete at both ends (queue / double-ended) | `deque` |
| Frequent insert/delete at known positions / need splice / iterators must not invalidate | `list` |
| Extreme memory saving + forward traversal only (embedded) | `forward_list` |

A mnemonic: use `vector` if you can, use `deque` if you really need double-ended, and use `list` / `forward_list` only if you really need linked list features. Among sequential containers, `vector` is almost always the default answer; the other three are specialized tools to "swap in only when there is a clear requirement." We have finished covering associative containers like `map` and `unordered_map`. In the next article, we will leave containers behind and look at the standard library's iterator and algorithm system.

Want to try running it directly to see the effect? Click the online example below (you can run it and see the assembly):

<OnlineCompilerDemo
  title="deque / list / forward_list: Head Insertion O(1) and splice"
  source-path="code/examples/vol3/05_deque_list_forward_list.cpp"
  description="Comparison of head insertion complexity, sizeof memory overhead, and list::splice zero-copy node moving"
  allow-run
/>

## Reference Resources

- [std::deque — cppreference](https://en.cppreference.com/w/cpp/container/deque)
- [std::list — cppreference](https://en.cppreference.com/w/cpp/container/list)
- [std::forward_list — cppreference](https://en.cppreference.com/w/cpp/container/forward_list)
- [Container Iterator Invalidation Rules Summary Table — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
