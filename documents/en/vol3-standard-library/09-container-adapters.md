---
chapter: 7
cpp_standard:
- 11
- 20
- 23
description: 'A deep dive into the three container adapters: they are not new containers,
  but rather wrappers around underlying containers with restricted interfaces to provide
  LIFO/FIFO/heap semantics. We explore the essence of `priority_queue` as an underlying
  container combined with `std::push_heap`/`pop_heap`, defaulting to a max-heap (switchable
  to a min-heap by changing the comparator), plus the C++23 `push_range` feature.'
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
  source: documents/vol3-standard-library/09-container-adapters.md
  source_hash: 408ba324d603586059e2a72f5f9c08bc4ae2ed73b4cbabc735ff569d1855f30e
  translated_at: '2026-06-16T06:13:05.073345+00:00'
  engine: anthropic
  token_count: 1643
---
# Container Adaptors: How stack, queue, and priority_queue Are "Wrapped"

## Adaptors are not Containers: They are Restricted Shells around Underlying Containers

`stack`, `queue`, and `priority_queue` are officially called **container adaptors** in the standard, not independent containers. The distinction is that a true container (like `vector` or `deque`) owns its data and determines its own storage strategy; whereas an adaptor does not invent its own storage. Instead, it **holds an underlying container** and wraps it in a restricted interface, only allowing you to access data in a specific way (stack, queue, or priority queue).

This "restriction" is the key reason for their existence. `std::stack` only exposes `top`, `push`, and `pop`, all occurring at the same end. It is physically impossible to steal an element from the middle—this turns "Last-In, First-Out" from a convention into a structural guarantee, blocking misuse at the compiler level. Similarly, `queue` guarantees First-In, First-Out, and `priority_queue` guarantees you always get the highest priority element. The cost is the loss of random access, but in return, you get "predictable element types and an interface that cannot be abused." So, choosing an adaptor boils down to asking yourself: **Do I only want to use this specific access mode and want the type system to block other operations?**

## stack and queue: Building LIFO/FIFO with Tail Operations

An adaptor's interface is essentially a renaming of the underlying container's operations. `std::stack` is Last-In, First-Out: `push` adds an element to the back, `top` looks at the back, and `pop` removes the back. Since all three actions occur at the container's `back`, it requires the underlying container to support `back()`, `push_back()`, and `pop_back()`. `std::queue` is First-In, First-Out: `push` enters at `back`, while `front()`/`pop` exit at `front`, so it additionally requires `front()` and `pop_front()`.

| Adaptor | Semantics | Required Underlying Container Support | Default Underlying |
|---------|-----------|----------------------------------------|--------------------|
| `stack` | LIFO | `back`, `push_back`, `pop_back` | `deque` |
| `queue` | FIFO | `front`, `back`, `push_back`, `pop_front` | `deque` |
| `priority_queue` | Priority | `front`, `push_back`, `pop_back` + **Random Access Iterator** | `vector` |

Why is `deque` the default? Because insertion and deletion at both ends are O(1), which perfectly suits `stack` (which only uses `back`) and `queue` (which uses `front` and `back`). Also, `deque` avoids the cost of moving entire memory blocks during reallocation, unlike `vector`. Here is a counter-intuitive point worth noting: **`std::queue` cannot use `vector` as the underlying container** because `vector` lacks `pop_front`. To pop from the front of a `vector`, you would need `erase(begin())`, which is O(n) and isn't provided as a member function in the standard; forcing it would result in a compilation error. Valid underlying containers for `queue` are limited to `deque` and `list`. `stack` is more flexible; `vector`, `deque`, and `list` all work because they satisfy its three requirements.

## priority_queue: Underlying Container Plus Heap Algorithms, This is the Core

Of the three adaptors, `priority_queue` is the most worth dissecting, as its implementation best demonstrates the pattern "adaptor = underlying container + standard library algorithms." It isn't some mysterious data structure; essentially, it is "a contiguous container + several heap functions from `<algorithm>`." Specifically, `push` is equivalent to `c.push_back(x)` followed by `std::push_heap(c.begin(), c.end(), cmp)`. `pop` is equivalent to `std::pop_heap(c.begin(), c.end(), cmp)` followed by `c.pop_back()`. `top` simply returns `c.front()`. The "heap property" maintained by the heap algorithms guarantees that `c.front()` is always the current highest priority element.

We can derive the complexity directly from this implementation. `top()` reads the first element directly, so it is O(1). `push()` appends to the end in constant time, and `push_heap` floats the new element up at most `log n` levels (the height of the tree), making it O(log n). In `pop()`, `pop_heap` swaps the first and last elements, then sinks the new first element down at most `log n` levels, plus one `pop_back`, resulting in overall O(log n). This also explains why the underlying container for `priority_queue` **must have random access iterators**. Heap sinking and floating require jumping by index within an array (parent `i`, children `2i+1`/`2i+2`). A linked list cannot achieve this O(1) positioning, so the underlying choices are limited to `vector` or `deque`, defaulting to `vector` (contiguous memory is cache-friendly and faster for heap operations).

The default comparator is `std::less`, resulting in a **max heap**—`top()` returns the current maximum. To get a min heap, simply swap the comparator for `std::greater`. This "changing heap direction by swapping comparator" feature is the most common usage pattern for `priority_queue`.

## Let's Run It: Default Max Heap, Swap Comparator for Min Heap

Just saying "default max heap" isn't concrete enough, so let's run it and see exactly who `top` is.

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

For the same dataset, the default behavior pushes the largest value (9) to the top of the heap. By swapping in `greater`, the smallest value (1) rises to the top. Note that the order of elements popped out is **sorted**—this is essentially the process of heap sort. Each `pop` from a `priority_queue` yields the current extremum; continuously popping until empty yields a sorted sequence. Since the underlying structure is a heap, `priority_queue` is often used as an "online heap sort": we can obtain the current extremum at any time while pushing elements. With `top()` at O(1) and insertions/deletions at O(log n), it is a core data structure for many algorithms (Dijkstra, merging K sorted sequences, Top-K).

## C++23 Upgrade: `push_range` for Bulk Insertion

C++23 adds `push_range` to all three adapters, allowing us to push an entire range at once. For `stack` and `queue`, this is essentially syntactic sugar for a loop calling `push`, but for `priority_queue`, it offers a tangible complexity advantage that is worth discussing separately.

The reason is that maintaining heap order in a `priority_queue` comes at a cost. If you take a range of N elements and loop `push` N times, each `push_heap` is O(log n), resulting in a total of O(n log n). `push_range`, however, first appends the entire range to the underlying container in one shot (`append_range`, O(n)), and then performs a single `make_heap` on the whole set (also O(n)), resulting in a total of only O(n). When dealing with a large number of elements, this difference is significant.

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

## The Art of Choosing Underlying Containers

In most cases, the defaults work best—`stack` and `queue` use `deque`, while `priority_queue` uses `vector`. These are the optimal defaults chosen by the Committee. If we want to change them, it is usually for one of two reasons. One is that a `priority_queue` might want to avoid default `vector` reallocation copies by reserving space for the underlying vector. However, the adapter doesn't expose `reserve` directly, so we must construct the underlying container first and then move it in (e.g., `std::priority_queue<int> pq{less{}, my_reserved_vector}`). The other reason is if the element type is unfriendly to `vector` (for example, if it is very large or expensive to move). In that case, `priority_queue` can switch to `deque` as the underlying container. Scenarios where `stack` or `queue` need a different underlying container are even rarer. Unless we explicitly need to save memory (using `list` to avoid pre-allocation), the default `deque` is perfectly fine.

```cpp
// 给 priority_queue 预留容量：先 reserve 底层 vector，再 move 进去
std::vector<int> buf;
buf.reserve(10'000);
std::priority_queue<int> pq{std::less<int>{}, std::move(buf)};
```

## Wrapping Up

The core idea behind container adapters can be summed up in one phrase: **underlying container + restricted interface, where restrictions yield semantic guarantees**. `stack` and `queue` expose one or both ends of a container to act as a stack or queue; `priority_queue` goes a step further, wrapping a contiguous container into a priority queue using heap functions from `<algorithm>`—`top` is O(1), insertion and deletion are O(log n), it defaults to a max-heap, and swapping the comparator turns it into a min-heap. Remember two main usage caveats: first, `top()` only peeks; to actually remove the element, it must be followed immediately by `pop()`. Second, `priority_queue` lacks interfaces for "erase arbitrary element" or "find by value." If you need these operations (for example, to cancel an element midway through), you should use `set` or `multiset` instead of `priority_queue`. In the next article, we will shift our focus from classic containers to the new members added to the container family in C++23/26—`flat_map`, `inplace_vector`, and `mdspan`.

Want to try it out yourself? Check out the online example below (runnable and viewable assembly):

<OnlineCompilerDemo
  title="stack / queue / priority_queue: Default max-heap, greater turns it into min-heap"
  source-path="code/examples/vol3/09_container_adapters.cpp"
  description="Semantics of the three adapters, changing heap direction in priority_queue by swapping comparators, and the heap algorithms behind push/pop"
  allow-run
  allow-x86-asm
/>

## References

- [std::stack — cppreference](https://en.cppreference.com/w/cpp/container/stack)
- [std::queue — cppreference](https://en.cppreference.com/w/cpp/container/queue)
- [std::priority_queue — cppreference](https://en.cppreference.com/w/cpp/container/priority_queue)
- [std::priority_queue::push_range (C++23) — cppreference](https://en.cppreference.com/w/cpp/container/priority_queue/push_range)
- [std::push_heap / std::make_heap (Heap algorithms) — cppreference](https://en.cppreference.com/w/cpp/algorithm/push_heap)
