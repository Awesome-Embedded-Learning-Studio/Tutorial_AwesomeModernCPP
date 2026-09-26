---
chapter: 7
cpp_standard:
- 11
- 20
- 23
description: 'A thorough walkthrough of the three container adapters: they are not new containers, but restricted interfaces wrapped around an underlying container to produce LIFO/FIFO/heap semantics; the essence of priority_queue is an underlying container plus std::push_heap/pop_heap — a max-heap by default, a min-heap after swapping the comparator — plus push_range from C++23.'
difficulty: intermediate
order: 9
platform: host
prerequisites:
- 'Deep Dive into vector: Three Pointers, Reallocation, and Iterator Invalidation'
- 'deque, list, and forward_list: Three Alternatives to vector'
reading_time_minutes: 8
related:
- 'Container Selection Guide: Picking the Right One by Operations, Memory, and Invalidation Rules'
tags:
- host
- cpp-modern
- intermediate
- 容器
title: 'Container Adapters: How stack, queue, and priority_queue Are "Wrapped"'
translation:
  source: documents/vol3-standard-library/containers/09-container-adapters.md
  source_hash: 408ba324d603586059e2a72f5f9c08bc4ae2ed73b4cbabc735ff569d1855f30e
  translated_at: '2026-09-26T02:34:27+00:00'
  engine: anthropic
  token_count: 7000
---
# Container Adapters: How stack, queue, and priority_queue Are "Wrapped"

## Adapters Are Not Containers: A Restricted Shell Around the Underlying Container

These three — `stack`, `queue`, and `priority_queue` — are what the standard calls **container adapters**, not independent containers. The difference: a true container (say `vector` or `deque`) holds its own data and decides for itself how to store it; an adapter invents no storage of its own. Instead, it **holds an underlying container** and wraps a restricted interface around the outside, letting you touch the data in exactly one particular way (stack, queue, or priority queue).

That "restricted" part is the key — and the reason adapters deserve to exist. `std::stack` exposes only `top`/`push`/`pop`, all happening at the same end; physically there is no way to sneak an element out of the middle — which turns "last in, first out" from a convention into a structural guarantee, with misuse stopped right at the compiler level. Likewise, `queue` guarantees first-in-first-out, and `priority_queue` guarantees you always get the currently most-prioritized element. The price is that you lose arbitrary access; what you buy back is "the elements you get are predictable, and the interface cannot be abused". So whether to use an adapter is really asking yourself: **do I want exactly this one access pattern, and do I want the type system to block every other operation for me?**

## stack and queue: A Few End Operations Stitched into LIFO/FIFO

An adapter's interface is nothing more than a renaming of a handful of operations on the underlying container. `std::stack` is last-in-first-out: `push` presses an element onto the end, `top` looks at the end, `pop` pops the end — all three actions happen at the container's `back` end, so what it requires of the underlying container is `back()` / `push_back()` / `pop_back()`. `std::queue` is first-in-first-out: `push` enters at `back`, `front()`/`pop` exit at `front`, so it additionally requires the underlying container to have `front()` and `pop_front()`.

| Adapter | Semantics | Required of the underlying container | Default underlying |
|--------|------|----------------|---------|
| `stack` | LIFO | `back`, `push_back`, `pop_back` | `deque` |
| `queue` | FIFO | `front`, `back`, `push_back`, `pop_front` | `deque` |
| `priority_queue` | Priority | `front`, `push_back`, `pop_back` + **random access iterators** | `vector` |

Why is the default underlying container `deque`? Because insertion and erasure at both ends are O(1), which satisfies `stack` (back only) and `queue` (front + back) exactly, and `deque` never pays `vector`'s cost of hauling the whole block over during a reallocation. And here is a counter-intuitive point worth committing to memory: **`std::queue` cannot use `vector` as its underlying container**, because vector has no `pop_front` — popping from the head of a vector could only be done with `erase(begin())`, which is O(n), and the standard library does not provide that member at all; forcing it in fails to compile. To swap a queue's underlying container, the only legal choices are `deque` and `list`. `stack` lives a much easier life: `vector`/`deque`/`list` all work, because all of them meet its three requirements.

## priority_queue: An Underlying Container Plus Heap Algorithms — This Is the Key Part

Of the three adapters, `priority_queue` is the one most worth taking apart, because its implementation best embodies the pattern "adapter = underlying container + standard library algorithms". It is not some mysterious data structure at all. In essence it is "a contiguous container + a few heap functions from `<algorithm>`" — concretely, `push` is equivalent to `c.push_back(x)` followed by `std::push_heap(c.begin(), c.end(), cmp)`; `pop` is equivalent to `std::pop_heap(c.begin(), c.end(), cmp)` followed by `c.pop_back()`; and `top` just returns `c.front()`. The "heap order" maintained by the heap algorithms guarantees that `c.front()` is always the currently most-prioritized element.

Every complexity bound falls straight out of this implementation. `top()` reads the first element directly: O(1). `push()` appends at the end in constant time, and `push_heap` floats the new element upward — at most `log n` levels of tree height — so it is O(log n). In `pop()`, `pop_heap` first swaps the first element with the last one, then sinks the new first element down, again at most `log n` levels, plus one `pop_back`: O(log n) overall. This also explains why the underlying container of a `priority_queue` **must offer random access iterators** — the heap's sink-and-float game jumps around the array by index (parent `i`, children `2i+1`/`2i+2`), and a linked list cannot deliver that kind of O(1) positioning. So the underlying container can only be `vector` or `deque`, with `vector` as the default (contiguous memory, cache-friendly, faster heap operations).

The default comparator is `std::less`, and the result is a **max-heap** — `top()` returns the current maximum. Want a min-heap instead? Just swap the comparator for `std::greater`. This "swap the comparator to flip the heap" property is the single most common trick played with priority_queue.

## Let's Run It: Max-Heap by Default, Min-Heap After Swapping the Comparator

Just saying "max-heap by default" is not concrete enough — let's run it and see who `top` actually is.

```cpp
#include <cstdio>
#include <functional>
#include <queue>
#include <vector>

int main()
{
    // Default: vector + less = max heap, top() returns the maximum
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

    // Swap in greater = min heap, top() returns the minimum
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

On the same dataset, the default pushes the largest value, 9, to the top of the heap; after switching to `greater`, the smallest value, 1, floats up instead. Notice that the pop order is **sorted** — this is literally the process of heapsort: each pop of a priority_queue spits out the current extreme, and popping until empty yields a sorted sequence. And precisely because the underlying structure is a heap, priority_queue often moonlights as "online heapsort": push elements as they arrive while always being able to grab the current extreme — `top()` at O(1), insert/delete at O(log n) — which makes it the workhorse structure behind many algorithms (Dijkstra, merging k sorted sequences, Top-K).

## A Small C++23 Upgrade: push_range, Pushing a Whole Range at Once

C++23 added `push_range` to all three adapters, so an entire range can be pushed in one shot. For `stack`/`queue` it is just syntactic sugar over a loop of `push` calls, but for `priority_queue` it brings a real, tangible complexity advantage — worth a section of its own.

The reason: maintaining heap order is not free. Take a range of N elements and loop `push` N times, and every `push_heap` costs O(log n), for a total of O(n log n). `push_range` instead appends the entire range to the underlying container in one go (`append_range`, O(n)), then runs a single `make_heap` over the whole thing (also O(n)) — only O(n) in total. When the element count gets large, this gap is very visible.

```cpp
#include <queue>
#include <vector>

int main()
{
    std::vector<int> data{5, 1, 9, 3, 7, 2, 8, 4, 6, 0};
    std::priority_queue<int> pq;

#if __cplusplus >= 202302L
    pq.push_range(data);   // C++23: bulk append_range + make_heap, O(n)
#else
    for (int x : data) {   // C++20 fallback: push in a loop, O(n log n)
        pq.push(x);
    }
#endif
    return 0;
}
```

This needs C++23 library support (a reasonably recent libstdc++/libc++); compile with `-std=c++23`. On older toolchains, fall back to the push loop — the behavior is identical, just slower when the volume gets large.

## The Knack of Picking the Underlying Container

Almost always, the defaults are the right call — `deque` for `stack`/`queue`, `vector` for `priority_queue`; the committee already picked the optimal defaults for you. When you do swap, it is usually for one of two purposes. One: you want a `priority_queue` to avoid the default `vector`'s reallocation copies, so you reserve the underlying vector up front — but the adapter does not expose `reserve` directly, so you have to construct the underlying container yourself and then move it in (`std::priority_queue<int> pq{less{}, my_reserved_vector}`). The other: your element type does not get along with `vector` (very large objects, expensive moves), in which case `priority_queue` can switch its underlying container to `deque`. Swapping the underlying container of `stack`/`queue` comes up even less often — unless you specifically want to save memory (use `list` to avoid pre-allocation), the `deque` default is perfectly fine.

```cpp
// Reserving capacity for a priority_queue: reserve the underlying vector first, then move it in
std::vector<int> buf;
buf.reserve(10'000);
std::priority_queue<int> pq{std::less<int>{}, std::move(buf)};
```

## A Few Parting Words

The core of container adapters fits into one sentence: **underlying container + restricted interface — you trade restriction for a semantic guarantee**. `stack`/`queue` expose one end or both ends of a container as a stack or a queue; `priority_queue` goes one step further, wrapping a contiguous container into a priority queue with the heap functions from `<algorithm>` — `top` at O(1), insert/delete at O(log n), max-heap by default, min-heap after swapping the comparator. Two practical stumbling blocks to burn into memory: first, `top()` only looks — to actually extract the element you must immediately follow it with `pop()`; second, `priority_queue` offers no "erase an arbitrary element" or "find by value" interface — if you need those (say, to cancel an element midway through), what you want is `set` or `multiset`, not priority_queue. In the next article we turn our eyes away from the classic containers and meet the new members C++23/26 added to the container family — `flat_map`, `inplace_vector`, and `mdspan`.

Want to get your hands on it right away and see it run? Open the online example below (it runs, and you can view the assembly):

<OnlineCompilerDemo
  title="stack / queue / priority_queue: max-heap by default, greater flips it to a min-heap"
  source-path="code/examples/vol3/09_container_adapters.cpp"
  description="The semantics of the three adapters, flipping priority_queue's heap direction with a comparator, and the heap algorithms behind push/pop"
  allow-run
  allow-x86-asm
/>

## References

- [std::stack — cppreference](https://en.cppreference.com/w/cpp/container/stack)
- [std::queue — cppreference](https://en.cppreference.com/w/cpp/container/queue)
- [std::priority_queue — cppreference](https://en.cppreference.com/w/cpp/container/priority_queue)
- [std::priority_queue::push_range (C++23) — cppreference](https://en.cppreference.com/w/cpp/container/priority_queue/push_range)
- [std::push_heap / std::make_heap (heap algorithms) — cppreference](https://en.cppreference.com/w/cpp/algorithm/push_heap)
