---
chapter: 7
cpp_standard:
- 11
- 20
description: 'Deep dive into the three alternatives to `vector` among sequential containers:
  `deque`''s segmented continuous double-ended structure, `list`''s doubly linked
  list and `splice`, and `forward_list`''s ultimate memory efficiency, along with
  the real-world trade-offs between cache locality during traversal and insertion
  complexity at the front.'
difficulty: intermediate
order: 5
platform: host
prerequisites:
- vector 深入：三指针、扩容与迭代器失效
reading_time_minutes: 8
related:
- 容器选择指南
tags:
- host
- cpp-modern
- intermediate
- 容器
title: 'deque, list, and forward_list: Three Alternatives to vector'
translation:
  source: documents/vol3-standard-library/containers/05-deque-list-forward-list.md
  source_hash: 62d31793dc2e51e2e7d56aba00cb2f0511f210d0a7714c8fdc80b4c19a77a080
  translated_at: '2026-06-16T06:10:16.927377+00:00'
  engine: anthropic
  token_count: 1646
---
# deque, list, and forward_list: Three Alternatives to vector

## Why do we need these three when vector is good enough?

We covered `vector` in the [previous article](03-vector-deep-dive.md). With contiguous memory, $O(1)$ random access, and amortized $O(1)$ insertion at the end, it is the optimal solution for most scenarios. However, it has a few blind spots: insertion at the head is $O(n)$ (shifting all elements), insertion in the middle is $O(n)$), reallocation moves all elements, and iterators/references are invalidated upon expansion. When we encounter requirements like "frequently adding items to the head" or "frequently inserting/deleting at known positions without invalidating iterators," `vector` is no longer suitable. `deque`, `list`, and `forward_list` exist to fill these gaps—they use different memory layouts to gain capabilities that `vector` cannot provide, at the cost of their own specific trade-offs.

Keep this in mind for now: `deque` is a "vector that can insert at both ends," `list` is a "linked list with $O(1)$ insertion/deletion in the middle," and `forward_list` is a "singly linked list that saves more memory than `list`."

## deque: $O(1)$ insertion at both ends and random access

The `deque` (pronounced "deck," short for double-ended queue) resembles `vector` the most, but it solves the $O(n)$ problem of head insertion in `vector`. Its underlying structure is not a single contiguous block of memory, but rather **segmented contiguity**: a central control array (a set of pointers), where each pointer points to a fixed-size chunk. Elements are stored in these chunks, and memory is contiguous within each chunk.

```cpp
// deque 分段连续的简化骨架（标准库内部，各厂细节不同）
struct Deque {
    std::vector<Block*> control;   // 中控数组，每项指向一个块
    // 每个 Block 是一段连续内存，装若干元素
};
// 随机访问：block = control[i / chunk_size]，元素 = block[i % chunk_size]
```

This structure brings three key characteristics. First, **pushing and popping at both the front and back are O(1)**: if the back is full, we add a new block; if the front is full, we add a block in front (or fill the current block backward). Neither approach moves existing elements—this is its biggest advantage over `vector`. Second, **random access is still O(1)**: `d[i]` calculates which block the element is in and then takes the offset within that block. It only involves one extra pointer indirection ("map → block") compared to `vector`, so it is slightly slower. Third, **reallocation does not move all elements**: when a `deque` is full, we only need to reallocate the central map (a small array of pointers) and attach new blocks. The addresses of existing elements remain unchanged—this is much gentler than `vector` reallocation (which moves everything and invalidates all iterators).

The trade-offs are: memory is not in a single contiguous chunk (unfriendly for scenarios requiring passing data to C interfaces or needing a continuous buffer), and the "map + multiple blocks" structure itself incurs a certain amount of space overhead.

## list: Doubly Linked List, O(1) Insert/Delete in Middle + Splice

`list` is a doubly linked list where each node stores `{prev pointer, data, next pointer}`. Its core selling point is: **insertion and deletion at a known position (having an iterator) is O(1)**—it only modifies a few pointers without moving any other elements. Furthermore, **iterators never become invalid** (insertion/deletion only affects the iterator of the removed element itself), which neither `deque` nor `vector` can achieve.

`list` also has a unique trick called **splice**: `l1.splice(pos, l2)` can directly "splice" the node chain of `l2` into `l1`. The entire process is O(1) and copies no elements—this is a capability unique to linked lists that contiguous containers cannot provide. It is suitable for scenarios like "moving a section of one list to another at zero cost."

However, the weaknesses of `list` are also critical. First, **it does not support random access**; there is no `operator[]`. To find the 1000th element, we must traverse 1000 steps from the head (O(n)). Second, **it is extremely cache-unfriendly**: nodes are scattered across the heap. During traversal, CPU prefetching fails and cache misses occur frequently. Later, we will run benchmarks to show you that traversing a `list` is several times slower than a `vector`, precisely for this reason. Therefore, the advantage of "O(1) insertion in the middle" is often negated by "O(n) to find the position" plus "slow traversal"—unless you actually hold an iterator and perform frequent insertions and deletions, it might not be worth it.

## forward_list: The Extremely Fringe Singly Linked List

`forward_list` is a singly linked list where each node only stores `{next pointer, data}`, saving one predecessor pointer compared to `list`. It was introduced in C++11 with a clear goal: to match the "zero overhead" of hand-written C singly linked lists—when you only need forward traversal and are sensitive to memory (e.g., in embedded systems), there is no need to pay the price of an extra pointer for backward capabilities you won't use.

The cost is naturally the inability to traverse backward, and **there is no O(1) `push_back`** (you must walk O(n) to the end); only `push_front` is O(1). The interface is also more streamlined than `list`: it **deliberately does not provide `size()`**—because the standard requires `size()` to be O(1), and a singly linked list cannot maintain this in O(1) without extra cost, so it simply omits it. If you need the size, you must count it yourself.

## Let's Run It: Traversal vs. Front Insertion, Two Completely Different Faces

Saying that `list` traversal is slow and `vector` front insertion is slow is too abstract; let's just run it. First, let's look at traversal: we fill `vector`, `deque`, and `list` with one million integers each and traverse them to calculate the sum.

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

(GCC 16.1.1, native; the relative performance is stable.) `std::list` is six times slower than `std::vector` and four times slower than `std::deque` — this is the real cost of scattered nodes and poor cache locality. Since `std::deque` is segmented-contiguous, it retains locality within chunks, making it significantly faster than `std::list`, though still slightly slower than the fully contiguous `std::vector`.

Now let's look at the opposite scenario: inserting one hundred thousand elements at the front.

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

Now the results are completely reversed: `vector` takes 246 ms for front insertion, while `deque` takes only 0.2 ms—a difference of over one thousand times. This is because every `insert(begin)` in a `vector` requires shifting all existing elements back by one position; doing this one hundred thousand times results in O(n²) complexity. In contrast, front insertion in both `deque` and `list` is O(1). Note that `deque` is even faster than `list` (since `list` needs to `malloc` a node for every element, whereas `deque` mostly fills within existing chunks and only allocates new chunks occasionally). This is also why `deque` outperforms `list` in "double-ended modification" scenarios.

Looking at these two sets of data together, one thing becomes clear: **there is no silver bullet**. Use `vector` or `deque` for traversal-heavy workloads, and `deque` or `list` for frequent front or middle insertions. Choosing the wrong container leads to order-of-magnitude performance differences.

## Summary: How to Choose

| Requirement | Choice |
|-------------|--------|
| Random access + mostly tail modifications | `vector` |
| Modifications at both ends (queue / double-ended) | `deque` |
| Frequent insert/delete at known positions / need `splice` / iterator stability | `list` |
| Extreme memory savings + forward-only traversal (embedded) | `forward_list` |

Here is a quick rule of thumb: use `vector` if you can; use `deque` if you truly need double-ended operations; use `list` or `forward_list` only when you specifically need linked list characteristics. Among sequential containers, `vector` is almost always the default answer, while the other three are specialized tools to swap in "when there is a clear requirement." We have previously covered associative containers like `map` and `unordered_map`. In the next article, we will step away from containers and explore the standard library's iterator and algorithm system.

Want to try running it yourself right now? Open the online example below (supports execution and viewing assembly):

<OnlineCompilerDemo
  title="deque / list / forward_list: O(1) Front Insertion and splice"
  source-path="code/examples/vol3/05_deque_list_forward_list.cpp"
  description="Comparison of front insertion complexity, sizeof memory overhead, and list::splice zero-copy node moving"
  allow-run
/>

## References

- [std::deque — cppreference](https://en.cppreference.com/w/cpp/container/deque)
- [std::list — cppreference](https://en.cppreference.com/w/cpp/container/list)
- [std::forward_list — cppreference](https://en.cppreference.com/w/cpp/container/forward_list)
- [Container Iterator Invalidation Rules Summary — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
