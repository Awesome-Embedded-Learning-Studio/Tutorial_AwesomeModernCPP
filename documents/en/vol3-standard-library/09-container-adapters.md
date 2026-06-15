---
title: 'Container Adapters: How stack, queue, and priority_queue Are Wrapped'
description: 'A deep dive into the three container adapters: they are not new containers,
  but rather wrappers around underlying containers that provide restricted interfaces
  to express LIFO/FIFO/heap semantics. We explore the essence of `priority_queue`
  as an underlying container combined with `std::push_heap`/`pop_heap`, covering the
  default max-heap, converting to a min-heap by swapping comparators, and the addition
  of `push_range` in C++23.'
chapter: 7
order: 9
tags:
- host
- cpp-modern
- intermediate
- 容器
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 20
- 23
reading_time_minutes: 13
prerequisites:
- vector 深入：三指针、扩容与迭代器失效
- deque、list 与 forward_list：vector 之外的三个选择
related:
- 容器选择指南：按操作、内存与失效规则挑对容器
translation:
  source: documents/vol3-standard-library/09-container-adapters.md
  source_hash: 08bc8dd7591c4aec4f05629412e7bb5172af01aa85a2d35d3fd561fabaff6137
  translated_at: '2026-06-15T09:17:12.996956+00:00'
  engine: anthropic
  token_count: 1648
---
# Container Adapters: How `stack`, `queue`, and `priority_queue` Wrap Underlying Containers

## Adapters are not Containers: They are Restricted Shells Around Underlying Containers

`stack`, `queue`, and `priority_queue` are officially called **container adapters** in the standard, not independent containers. The distinction is this: a true container (like `vector` or `list`) owns its data and determines its storage strategy; an adapter does not invent its own storage. Instead, it **holds an underlying container** and wraps it in a restricted interface, forcing you to access data in a specific way (stack, queue, or priority queue).

This "restriction" is the key, and the reason adapters exist. `stack` only exposes `push`, `pop`, and `top`, all occurring at the same end. Physically, it is impossible to steal an element from the middle—this turns "Last-In-First-Out" from a convention into a structural guarantee, blocking misuse at the compiler level. Similarly, `queue` guarantees First-In-First-Out, and `priority_queue` guarantees you always get the highest priority element. The cost is the loss of random access, but in exchange, you get predictable access patterns and an interface that prevents abuse. So, the decision to use an adapter boils down to this: **Do I only need this specific access mode, and do I want the type system to block other operations?**

## `stack` and `queue`: Building LIFO/FIFO with Operations at the Ends

The interface of an adapter is essentially a renaming of specific operations from the underlying container. `stack` is Last-In-First-Out: `push` adds an element to the back, `top` peeks at the back, and `pop` removes the back. Since all three actions occur at the container's `back`, it requires the underlying container to support `back`, `push_back`, and `pop_back`. `queue` is First-In-First-Out: elements enter via `push_back` at the `back` and leave via `front`/`pop` at the `front`. Thus, it additionally requires the underlying container to support `front` and `pop_front`.

| Adapter | Semantics | Required Underlying Container Support | Default Underlying |
|--------|-----------|----------------------------------------|-------------------|
| `stack` | LIFO | `back`, `push_back`, `pop_back` | `deque` |
| `queue` | FIFO | `front`, `back`, `push_back`, `pop_front` | `deque` |
| `priority_queue` | Priority | `front`, `push_back`, `pop_back` + **Random Access Iterator** | `vector` |

Why is `deque` the default for `stack` and `queue`? Because insertion and deletion at both ends are $O(1)$, satisfying the needs of `stack` (which only uses `back`) and `queue` (which uses `front` and `back`). Furthermore, `deque` avoids the cost of bulk reallocation that `vector` incurs during expansion. Here is a counter-intuitive point worth noting: **`queue` cannot use `vector` as its underlying container**, because `vector` lacks `pop_front`. To pop from the front of a `vector`, you would need `erase(begin())`, which is $O(n)$ and isn't even provided as a member function by the standard library. To swap the underlying container for `queue`, your only legal choices are `deque` or `list`. `stack` is much more flexible; `deque`, `vector`, or `list` all work because they satisfy the three requirements.

## `priority_queue`: Underlying Container Plus Heap Algorithms, This is the Key

Of the three adapters, `priority_queue` is the most worth dissecting, as its implementation best embodies the pattern "adapter = underlying container + standard library algorithms." It is not some mysterious data structure; essentially, it is "a contiguous container + a few heap functions from `<algorithm>`." Specifically, `push` is equivalent to `push_back` followed by `push_heap`; `pop` is equivalent to `pop_heap` followed by `pop_back`; and `top` just returns `front`. The heap algorithms maintain the "heap property," ensuring that `front` is always the current highest priority element.

We can derive the complexity directly from this implementation. `top` reads the first element directly, so it is $O(1)$. `push` appends to the end in constant time, and `push_heap` floats the new element up at most the tree height of $\log n$ layers, resulting in $O(\log n)$. In `pop`, `pop_heap` first swaps the first and last elements, then sinks the new first element down, again traversing at most $\log n$ layers, plus one `pop_back`, resulting in overall $O(\log n)$. This also explains why the underlying container for `priority_queue` **must** support random access iterators. Heap sinking and floating require jumping by index within an array (parent node $(i-1)/2$, children $2i+1$/$2i+2$). Linked lists cannot achieve this $O(1)$ positioning, so the underlying choices are limited to `vector` or `deque`, with `vector` as the default (contiguous memory is cache-friendly, making heap operations faster).

The default comparator is `less`, resulting in a **max-heap**—`top` returns the current maximum. To get a min-heap, simply swap the comparator for `greater`. This feature of "changing heap direction by swapping the comparator" is the most common usage pattern for `priority_queue`.

## Try It Out: Default Max-Heap, Swap Comparator for Min-Heap

Just saying "default max-heap" isn't concrete enough; let's run it to see exactly what `priority_queue` gives us.

```cpp
#include <iostream>
#include <queue>
#include <vector>

int main() {
    // Default: max-heap (std::less)
    std::priority_queue<int> max_heap;
    // Min-heap (std::greater)
    std::priority_queue<int, std::vector<int>, std::greater<int>> min_heap;

    for (int val : {3, 1, 4, 1, 5, 9, 2, 6}) {
        max_heap.push(val);
        min_heap.push(val);
    }

    std::cout << "Max-Heap pop order: ";
    while (!max_heap.empty()) {
        std::cout << max_heap.top() << " ";
        max_heap.pop();
    }
    std::cout << "\n";

    std::cout << "Min-Heap pop order: ";
    while (!min_heap.empty()) {
        std::cout << min_heap.top() << " ";
        min_heap.pop();
    }
    std::cout << "\n";

    return 0;
}
```

```text
Max-Heap pop order: 9 6 5 4 3 2 1 1
Min-Heap pop order: 1 1 2 3 4 5 6 9
```

With the same dataset, the default setup pushes the largest value, 9, to the top. After switching to `greater`, the smallest value, 1, rises to the top. Notice that the pop order is **sorted**—this is essentially the process of heap sort. `priority_queue` spits out the current extreme value on every `pop`. Continuously popping until empty yields a sorted sequence. Because the underlying structure is a heap, `priority_queue` is often used as "online heap sort": you can push elements and retrieve the current extreme value at any time. `top` is $O(1)$, and insertion/deletion are $O(\log n)$, making it a core data structure for many algorithms (Dijkstra, merging K sorted lists, Top-K).

## C++23 Upgrade: `push_range` for Bulk Insertion

C++23 adds `push_range` to all three adapters, allowing you to push an entire range at once. For `stack` and `queue`, this is just syntactic sugar for a loop of `push` calls. However, for `priority_queue`, it offers a tangible complexity advantage that is worth discussing.

The reason lies in the cost of maintaining the heap property. If you take a range of N elements and loop `push` N times, each `push` (which calls `push_heap`) is $O(\log n)$, resulting in a total of $O(n \log n)$. The `push_range` approach, however, appends the entire range to the underlying container at once (`append`, $O(n)$) and then performs a single `make_heap` (also $O(n)$), resulting in a total of only $O(n)$. When the number of elements is large, the difference is significant.

```cpp
#include <iostream>
#include <queue>
#include <vector>

int main() {
    std::priority_queue<int> pq;

    // C++23: push_range
    // Complexity: O(N) vs O(N log N) for individual pushes
    std::vector<int> source = {3, 1, 4, 1, 5, 9, 2, 6};
    pq.push_range(source);

    std::cout << "Top after push_range: " << pq.top() << "\n";
    return 0;
}
```

Requires C++23 standard library support (a newer libstdc++ or libc++). Compile with `-std=c++23`. In older environments, falling back to a loop of `push` works fine; the behavior is identical, just slower for large datasets.

## The Rationale for Choosing Underlying Containers

The vast majority of the time, the defaults are optimal—`stack` and `queue` use `deque`, and `priority_queue` uses `vector`. These are the choices selected by the committee for good reason. If you need to swap them, it is usually for one of two reasons. One is `priority_queue` trying to avoid the default `vector` expansion copies—you can reserve space for the underlying `vector`. However, the adapter doesn't expose `reserve` directly, so you must construct the underlying container first and then `move` it in. The other reason is if the element type is not friendly to `vector` (e.g., very large or expensive to move); in that case, `priority_queue` can use `deque` as the underlying container. Scenarios for swapping the underlying container for `stack`/`queue` are even rarer, unless you explicitly want to save memory (using `list` to avoid pre-allocation), in which case the default `deque` is usually fine.

## Wrapping Up

The core of container adapters can be summed up in one phrase: **underlying container + restricted interface, trading restriction for semantic guarantees.** `stack` and `queue` expose one or both ends of a container as a stack or queue. `priority_queue` goes a step further, using the heap functions from `<algorithm>` to wrap a contiguous container into a priority queue—`top` is $O(1)$, insertion/deletion are $O(\log n)$, it defaults to a max-heap, and swapping the comparator turns it into a min-heap. Two usage caveats to remember: First, `top` is just a peek; to actually remove the element, you must follow it with `pop`. Second, `priority_queue` lacks interfaces for "erase arbitrary element" or "find by value." If you need these (e.g., to revoke an element midway), you should be using `std::set` or `std::multiset`, not `priority_queue`. In the next article, we will shift our focus from classic containers to the new members added to the container family in C++23/26—`std::flat_map`, `std::flat_set`, and `std::mdspan`.

Want to run this yourself? Check out the online example below (runnable, with x86 assembly output):

<OnlineCompilerDemo
  title="stack / queue / priority_queue: Default Max-Heap, greater for Min-Heap"
  source-path="code/examples/vol3/09_container_adapters.cpp"
  description="Semantics of the three adapters, changing heap direction with comparator in priority_queue, and heap algorithms behind push/pop"
  allow-run
  allow-x86-asm
/>

## References

- [std::stack — cppreference](https://en.cppreference.com/w/cpp/container/stack)
- [std::queue — cppreference](https://en.cppreference.com/w/cpp/container/queue)
- [std::priority_queue — cppreference](https://en.cppreference.com/w/cpp/container/priority_queue)
- [std::priority_queue::push_range (C++23) — cppreference](https://en.cppreference.com/w/cpp/container/priority_queue/push_range)
- [std::push_heap / std::make_heap (Heap Algorithms) — cppreference](https://en.cppreference.com/w/cpp/algorithm/push_heap)
