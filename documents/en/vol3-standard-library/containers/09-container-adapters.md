---
chapter: 7
cpp_standard:
- 11
- 20
- 23
description: 'A deep dive into three container adapters: they are not new containers,
  but rather restricted interfaces wrapped around underlying containers to provide
  LIFO/FIFO/heap semantics. We cover the essence of `priority_queue` as an underlying
  container combined with `std::push_heap`/`pop_heap`, defaulting to a max-heap, converting
  to a min-heap by swapping the comparator, and adding C++23''s `push_range`.'
difficulty: intermediate
order: 9
platform: host
prerequisites:
- vector 深入：三指针、扩容与迭代器失效
- deque、list 与 forward_list：vector 之外的三个选择
reading_time_minutes: 8
related:
- 容器选择指南：按操作、内存与失效规则挑对容器
tags:
- host
- cpp-modern
- intermediate
- 容器
title: 'Container Adapters: How stack, queue, and priority_queue Are "Wrapped'
translation:
  source: documents/vol3-standard-library/containers/09-container-adapters.md
  source_hash: 408ba324d603586059e2a72f5f9c08bc4ae2ed73b4cbabc735ff569d1855f30e
  translated_at: '2026-06-24T00:36:21.103083+00:00'
  engine: anthropic
  token_count: 1643
---
# Container Adapters: How `stack`, `queue`, and `priority_queue` Are Wrapped

## Adapters Are Not Containers: Putting a Restricted Shell on an Underlying Container

`stack`, `queue`, and `priority_queue` are technically called **container adaptors** in the standard, not independent containers. The difference is this: a true container (like `vector` or `deque`) owns the data and decides how it is stored; an adaptor does not invent its own storage. Instead, it **holds an underlying container** and wraps it in a restricted interface, forcing you to access data in a specific way (stack, queue, or priority queue).

This "restriction" is the key reason for adaptors to exist. `std::stack` only exposes `top`, `push`, and `pop`, all happening at the same end. Physically, it is impossible to steal an element from the middle—this turns "Last-In-First-Out" from a convention into a structural guarantee, blocking misuse at the compiler level. Similarly, `queue` guarantees First-In-First-Out, and `priority_queue` guarantees you always get the highest priority element. The cost is losing random access capabilities, but in return, you get "predictable element types and an interface that cannot be abused." So, choosing whether to use an adaptor is essentially asking yourself: **Do I only want to use this specific access pattern, and do I want the type system to block other operations?**

## stack and queue: Using End Operations to Build LIFO/FIFO

The interface of an adaptor is essentially a renaming of several operations from the underlying container. `std::stack` is Last-In-First-Out: `push` puts an element at the back, `top` looks at the back, and `pop` removes the back. Since all three actions occur at the `back` of the container, it requires the underlying container to support `back()`, `push_back()`, and `pop_back()`. `std::queue` is First-In-First-Out: `push` enters at `back`, while `front()`/`pop` exit at `front`. Thus, it additionally requires the underlying container to support `front()` and `pop_front()`.

| Adaptor | Semantics | Required Underlying Container Support | Default Underlying |
|---------|-----------|----------------------------------------|--------------------|
| `stack` | LIFO | `back`, `push_back`, `pop_back` | `deque` |
| `queue` | FIFO | `front`, `back`, `push_back`, `pop_front` | `deque` |
| `priority_queue` | Priority | `front`, `push_back`, `pop_back` + **Random Access Iterator** | `vector` |

Why is `deque` the default underlying container? Because insertion and deletion at both ends are O(1), which perfectly satisfies `stack` (which only uses `back`) and `queue` (which uses `front` and `back`). Furthermore, `deque` avoids the cost of moving entire memory blocks during reallocation that `vector` has. Here is a counter-intuitive point worth noting: **`std::queue` cannot use `vector` as the underlying container** because `vector` lacks `pop_front`. To pop from the front of a `vector`, you would have to use `erase(begin())`, which is O(n) and isn't provided as a member function by the standard library; forcing it would result in a compilation failure. To swap the underlying container for a `queue`, the only legal choices are `deque` and `list`. `stack` is much more flexible; `vector`, `deque`, and `list` all work because they satisfy its three requirements.

## priority_queue: Underlying Container Plus Heap Algorithms, This Is the Core

Of the three adaptors, `priority_queue` is the most worth dissecting because its implementation best embodies the pattern "adaptor = underlying container + standard library algorithms." It isn't some mysterious data structure; essentially, it is "a contiguous container + a few heap functions from `<algorithm>`." Specifically, `push` is equivalent to `c.push_back(x)` followed by `std::push_heap(c.begin(), c.end(), cmp)`. `pop` is equivalent to `std::pop_heap(c.begin(), c.end(), cmp)` followed by `c.pop_back()`. `top` simply returns `c.front()`. The "heap property" maintained by the heap algorithms guarantees that `c.front()` is always the current highest priority element.

We can derive the complexity directly from this implementation. `top()` reads the first element directly, so it is O(1). `push()` appends to the end in constant time, and `push_heap` floats the new element up the heap, traversing at most `log n` levels (the height of the tree), making it O(log n). In `pop()`, `pop_heap` swaps the first and last elements, then sinks the new first element down, again traversing at most `log n` levels, plus one `pop_back`, resulting in overall O(log n). This also explains why the underlying container for `priority_queue` **must have random access iterators**. Heap sinking and floating require jumping by index within an array (parent `i`, children `2i+1`/`2i+2`). A linked list cannot achieve this O(1) positioning, so the underlying container can only be `vector` or `deque`, defaulting to `vector` (contiguous memory, cache-friendly, faster heap operations).

The default comparator is `std::less`, resulting in a **max heap**—`top()` returns the current maximum. To get a min heap, simply swap the comparator for `std::greater`. This feature of "changing heap direction by swapping the comparator" is the most common use case for `priority_queue`.

## Let's Run It: Default Max Heap, Swap Comparator for Min Heap

Just saying "default max heap" isn't concrete enough, so let's run it to see exactly who `top` is.

```cpp
#include <cstdio>
#include <functional>
#include <queue>
#include <vector>

int main()
{
    // 默认：vector + less = 最大堆，top() 返回最大值
    std::priority_queue<int> pq;
    for (int x : {5, 1, 9, 3, 7}) {
        pq.push(x);
    }
    std::printf("默认（最大堆）依次 pop: ");
    while (!pq.empty()) {
        std::printf("%d ", pq.top());
        pq.pop();
    }
    std::printf("\n");

    // 换 greater = 最小堆，top() 返回最小值
    std::priority_queue<int, std::vector<int>, std::greater<int>> min_pq;
    for (int x : {5, 1, 9, 3, 7}) {
        min_pq.push(x);
    }
    std::printf("greater（最小堆）依次 pop: ");
    while (!min_pq.empty()) {
        std::printf("%d ", min_pq.top());
        min_pq.pop();
    }
    std::printf("\n");
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/pq_demo /tmp/pq_demo.cpp && /tmp/pq_demo
```

```text
默认（最大堆）依次 pop: 9 7 5 3 1
greater（最小堆）依次 pop: 1 3 5 7 9
```

For the same dataset, the default behavior pushes the largest element, 9, to the top of the heap. After swapping in `greater`, the smallest element, 1, rises to the top. Note that the order of elements popped is **sorted**—this is essentially the process of heap sort. Each `pop` from a `priority_queue` yields the current extreme value; continuously popping until empty yields a sorted sequence. Since the underlying structure is a heap, `priority_queue` is often used as an "online heap sort": we can push elements while retrieving the current maximum value at any time. With `top()` at $O(1)$ and insertion/deletion at $O(\log n)$, it is a primary data structure for many algorithms (Dijkstra, merging $k$ sorted sequences, and Top-K).

## C++23 Upgrade: `push_range` for Bulk Insertion

C++23 adds `push_range` to all three container adapters, allowing you to push an entire range at once. For `stack` and `queue`, this is just syntactic sugar for a loop of `push` calls. However, for `priority_queue`, it offers a tangible complexity advantage that is worth discussing separately.

The reason lies in the cost of maintaining heap order. If you take a range of $N$ elements and loop `push` $N$ times, each `push_heap` is $O(\log n)$, resulting in a total of $O(n \log n)$. `push_range`, on the other hand, first appends the entire range to the underlying container in one go (`append_range`, $O(n)$), and then performs a single `make_heap` on the whole structure (also $O(n)$), resulting in a total of only $O(n)$. When dealing with a large number of elements, this difference is significant.

```cpp
#include <queue>
#include <vector>

int main()
{
    std::vector<int> data{5, 1, 9, 3, 7, 2, 8, 4, 6, 0};
    std::priority_queue<int> pq;

#if __cplusplus >= 202302L
    pq.push_range(data);   // C++23：整体 append_range + make_heap，O(n)
#else
    for (int x : data) {   // C++20 退路：循环 push，O(n log n)
        pq.push(x);
    }
#endif
    return 0;
}
```

Requires C++23 standard library support (a newer version of libstdc++ or libc++). Compile with `-std=c++23`. For older environments, falling back to a loop with `push` works fine; the behavior is consistent, just slower when dealing with large amounts of data.

## The Nuances of Choosing an Underlying Container

In most cases, the defaults work well—using `deque` for `stack`/`queue` and `vector` for `priority_queue` represents the optimal choices selected by the committee. If we need to swap them, it is usually for one of two reasons. One is that we want to avoid the default `vector` reallocation copies in a `priority_queue`, so we can reserve space for the underlying vector—but since the adapter doesn't directly expose `reserve`, we must construct the underlying container first and then move it in (`std::priority_queue<int> pq{less{}, my_reserved_vector}`). The other is if the element type is not friendly to `vector` (for example, if it is very large or expensive to move); in that case, `priority_queue` can switch to `deque` as the underlying container. Scenarios where `stack`/`queue` need a different underlying container are even rarer. Unless we explicitly want to save memory (using `list` to avoid pre-allocation), the default `deque` is perfectly fine.

```cpp
// 给 priority_queue 预留容量：先 reserve 底层 vector，再 move 进去
std::vector<int> buf;
buf.reserve(10'000);
std::priority_queue<int> pq{std::less<int>{}, std::move(buf)};
```

## Wrapping Up

The core idea of container adapters can be summed up in one phrase: **underlying container + restricted interface, trading flexibility for semantic guarantees**. `stack` and `queue` expose one or both ends of a container to behave like a stack or queue; `priority_queue` goes a step further, wrapping a sequence container into a priority queue using heap algorithms from `<algorithm>`—`top` is O(1), insertion and deletion are O(log n), it defaults to a max-heap, and swapping the comparator turns it into a min-heap. Keep two practical caveats in mind: first, `top()` only peeks at the element; to actually remove it, we must immediately follow it with `pop()`. Second, `priority_queue` lacks interfaces for "erase arbitrary element" or "find by value"; if we need these (for example, to cancel an element midway through), we should use `set` or `multiset` instead of `priority_queue`. In the next article, we will shift our focus from classic containers to the new members added to the container family in C++23/26—`flat_map`, `inplace_vector`, and `mdspan`.

Want to try it out yourself? Check out the online example below (runnable and viewable assembly):

<OnlineCompilerDemo
  title="stack / queue / priority_queue: Default max-heap, greater for min-heap"
  source-path="code/examples/vol3/09_container_adapters.cpp"
  description="Semantics of the three adapters, changing heap direction with priority_queue comparators, and heap algorithms behind push/pop"
  allow-run
  allow-x86-asm
/>

## References

- [std::stack — cppreference](https://en.cppreference.com/w/cpp/container/stack)
- [std::queue — cppreference](https://en.cppreference.com/w/cpp/container/queue)
- [std::priority_queue — cppreference](https://en.cppreference.com/w/cpp/container/priority_queue)
- [std::priority_queue::push_range (C++23) — cppreference](https://en.cppreference.com/w/cpp/container/priority_queue/push_range)
- [std::push_heap / std::make_heap (heap algorithms) — cppreference](https://en.cppreference.com/w/cpp/algorithm/push_heap)
