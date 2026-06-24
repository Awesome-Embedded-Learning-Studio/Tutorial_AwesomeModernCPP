---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: A deep dive into the family of sorting, partitioning, and heap algorithms—why
  `std::sort` uses Introsort internally (Quicksort + Heapsort + Insertion Sort, worst-case
  O(n log n)), where `partial_sort` and `nth_element` save resources, how the sift-up
  and sift-down of `make_heap`/`push_heap`/`pop_heap` map to the underlying `priority_queue`,
  and how C++20 projections eliminate the need to write custom comparators for sorting
  by member.
difficulty: intermediate
order: 43
platform: host
prerequisites:
- 迭代器基础与 category
- 迭代器适配器：反向、插入与流，把现成迭代器改出新行为
- 容器适配器：stack、queue、priority_queue 是怎么「包」出来的
reading_time_minutes: 13
related:
- 容器选择指南：按操作、内存与失效规则挑对容器
tags:
- host
- cpp-modern
- intermediate
- 容器
title: 'Algorithm Overview (Part 2): Sorting, Partitioning, and Heaps'
translation:
  source: documents/vol3-standard-library/iterators-algorithms/43-algorithm-overview-part2.md
  source_hash: 3cdeeeeb40c5ba979fcabd5b398752ed360b0c24a69a88f14c3367ac40300484
  translated_at: '2026-06-24T00:45:30.398083+00:00'
  engine: anthropic
  token_count: 4515
---
# Algorithm Overview (Part 2): Sorting, Partitioning, and Heaps

In the previous article, we covered the algorithms in `<algorithm>` that are "non-modifying" and those that perform "moving / searching". In this article, we look at a heavier family—the ones that **reorder the entire range**: sorting, partitioning, and heaps. They share a common trait: they all require **random access iterators**. Remember the pitfall from Article 40? Applying `std::sort` to a `list` won't even compile, because quicksort needs `it + n` to randomly jump to the pivot. This entire family of algorithms inherits the same restriction.

But knowing that "it needs random access" isn't enough. The real question is: when facing a specific sorting requirement, which of the four look-alikes—`sort`, `stable_sort`, `partial_sort`, or `nth_element`—should we choose? What do they do internally, and how do their complexities differ? Then there's the heap group (`make_heap`, `push_heap`, `pop_heap`, `sort_heap`)—in Article 09, when discussing `priority_queue`, we mentioned that "its push is just `push_back` + `std::push_heap`". In this article, we will completely dismantle this set of heap operations to see exactly how "sifting up" and "sifting down" move elements within an array. Finally, we will wrap up with C++20 projections: sorting by a specific member of an object without writing a custom comparator.

## The Sorting Family: Four Look-alikes with Different Scopes

Let's line up the four brothers of this family. They all deal with "reordering a range according to some criteria," but their **guarantees differ**—some ensure only one element is in place, some ensure the first *k* are in place, and some sort the entire range. The fewer the guarantees, the faster the operation:

| Algorithm | Guarantee | Complexity |
|------|------|--------|
| `sort` | Entire range sorted | O(n log n), worst case is also O(n log n) |
| `stable_sort` | Entire range sorted, equal elements retain original order | O(n log n) or O(n log² n) (depends on memory) |
| `partial_sort` | First *k* sorted (and are the smallest *k*), rest is unordered | Approx. O(n log k) |
| `nth_element` | The *n*th element is exactly where it would be after sorting, elements to the left are smaller, elements to the right are larger, internals of left/right sides are unordered | Average O(n) |

This table is the essence of this section. When we see requirements like "I only need the top *k*" or "I only need the median," **don't use `sort` to sort the whole range and then extract**—that wastes `log n` times the effort. Let's break them down one by one.

### sort: Introsort, and Why Quicksort Doesn't Degrade to O(n²)

`std::sort` is not pure quicksort internally. The biggest headache with pure quicksort is that it degrades to O(n²) on inputs that are already nearly sorted, or when the pivot selection is poor—this is the "quicksort worst-case scenario" often tested in interviews. The standard library obviously cannot allow `sort` to suddenly drop an order of magnitude on certain inputs, so libstdc++ and other mainstream implementations use the same strategy: **Introsort** (introspective sort), combining the advantages of three algorithms.

The logic of Introsort is as follows: it starts with **quicksort**, which is the fastest on average; but it tracks the recursion depth at every level. Once the depth exceeds the threshold of `2·log₂ n`—indicating that quicksort might be degrading towards an unbalanced state—it **switches to heapsort**. Heapsort is guaranteed O(n log n) in the worst case, providing a safety net. When recursion bottoms out and the sub-range is small (usually around a dozen elements), it switches to **insertion sort**, because for small data volumes, insertion sort has a low constant factor and is cache-friendly, making it faster than continuing recursion.

These three stages combined provide average performance close to the fastest quicksort, worst-case performance locked at O(n log n) by heapsort, and constant factor savings for small data via insertion sort. Therefore, the complexity guarantee for `std::sort` is **O(n log n)**—specifically, "applies approximately `N·log(N)` comparisons," **with no room for degradation**. The "worst case is also O(n log n)" we wrote in the previous table comes from this—Introsort's "introspection" means it detects when quicksort is about to degrade and actively switches algorithms. Incidentally, the C++11 standard solidified this worst-case guarantee (early `sort` complexity was "average O(n log n)" with no worst-case floor), so in modern implementations, you no longer need to worry about quicksort degradation.

::: warning sort Does Not Guarantee Order of Equal Elements
Note that `sort` does not guarantee the relative order of equal elements—the order of two elements with the same value is **unspecified**. If your logic relies on "preserving original order when values are equal" (e.g., sorting first by hire date, then by salary, where people with the same salary must not have their hire dates shuffled), you must use `stable_sort`.
:::

Let's run a test to intuitively see the difference between `sort` and `stable_sort` regarding "equal elements." To make this difference visible, we need to construct an input with **many identical keys**—the more elements with the same key, the more space a non-stable sort has to shuffle them:

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

struct Point {
    int key;   // 排序依据
    int tag;   // 用来追踪"原始顺序"
};

void print_tagged(const std::vector<Point>& v, const char* lbl)
{
    std::cout << lbl << ": ";
    for (const auto& p : v) std::cout << "{" << p.key << "," << p.tag << "} ";
    std::cout << '\n';
}

int main()
{
    // 3 组 key（1/2/3），每组 8 个 tag 0..7 —— 相等 key 足够多才能看出非稳定
    std::vector<Point> data;
    for (int i = 0; i < 8; ++i) data.push_back({1, i});
    for (int i = 0; i < 8; ++i) data.push_back({2, i});
    for (int i = 0; i < 8; ++i) data.push_back({3, i});

    auto a = data;
    std::sort(a.begin(), a.end(),
              [](const Point& x, const Point& y) { return x.key < y.key; });
    print_tagged(a, "sort        (key 相同的 tag 顺序被算法打乱)");

    auto b = data;
    std::stable_sort(b.begin(), b.end(),
                     [](const Point& x, const Point& y) { return x.key < y.key; });
    print_tagged(b, "stable_sort (key 相同的 tag 仍是 0..7 原顺序)");
    return 0;
}
```

Here are the results obtained by running `g++ -std=c++20 -O2` (local GCC 16.1.1):

```text
sort        (key 相同的 tag 顺序被算法打乱): {1,1} {1,2} {1,3} {1,4} {1,5} {1,6} {1,7} {1,0} {2,4} {2,7} {2,6} {2,5} {2,3} {2,2} {2,1} {2,0} {3,0} {3,1} {3,2} {3,3} {3,4} {3,5} {3,6} {3,7}
stable_sort (key 相同的 tag 仍是 0..7 原顺序): {1,0} {1,1} {1,2} {1,3} {1,4} {1,5} {1,6} {1,7} {2,0} {2,1} {2,2} {2,3} {2,4} {2,5} {2,6} {2,7} {3,0} {3,1} {3,2} {3,3} {3,4} {3,5} {3,6} {3,7}
```

The `key` values are grouped by 1, 2, and 3 in both algorithms. The difference lies entirely in "the order of tags within the same key group": `sort` rearranges the tags for `key=1` into `1,2,3,4,5,6,7,0` and `key=2` into `4,7,6,5,3,2,1,0`—completely discarding the original input order of `0..7`. Meanwhile, `stable_sort` strictly preserves `0,1,2,3,4,5,6,7`. This is the meaning of "unstable": it doesn't mean it will definitely scramble the order, but rather that the standard **does not guarantee** preservation. The final arrangement depends on the internal swap paths of the algorithm, and it may vary with different inputs or implementations. Code that relies on the order of equivalent elements must use `stable_sort`. The trade-off is that stable sorting typically requires a buffer of equal size (degrading to O(n log² n) if allocation fails), so **if you don't need that stability, just use `sort`**—it is faster and saves memory.

::: warning sort's order of equivalent elements is implementation-defined
Because `sort` does not guarantee the order of equivalent elements, the "scrambled" output shown above is specific to libstdc++ 16.1.1 with this particular input—with libc++, MSVC, or a different input, the tag arrangement could be completely different. We use this example here simply to "visualize the reordering": the only truly portable conclusion is that the order of equivalent elements in `sort` is unreliable. If you need to preserve order, use `stable_sort`.
:::

### partial_sort: I only want the top k, sorted

Many requirements call for "finding the top k elements, and keeping those k sorted"—for example, a leaderboard displaying only the top 10 players ranked by score. `partial_sort(begin, middle, end)` does exactly this: after execution, the range `[begin, middle)` contains the smallest `k = middle - begin` elements from the entire interval, and they are **internally sorted**. The range `[middle, end)` contains the remaining elements with **no guaranteed order**.

The algorithm maintains a min-heap in the first k positions: it scans the remaining elements linearly. If an element is smaller than the current heap top (the largest of the current top k), it replaces the heap top and sifts down. After scanning the entire range, the top k elements are found. Finally, it performs a sort on this min-heap. The complexity is approximately **O(n log k)**—the smaller `k` is, the more efficient it is. When `k` approaches `n`, it degrades to roughly the same cost as a full sort.

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

void print(const std::vector<int>& v, const char* lbl)
{
    std::cout << lbl << ": ";
    for (int x : v) std::cout << x << ' ';
    std::cout << '\n';
}

int main()
{
    std::vector<int> v{5, 2, 9, 1, 7, 3, 8, 4, 6, 0};
    std::partial_sort(v.begin(), v.begin() + 4, v.end());
    print(v, "partial_sort (前 4 名有序，后面无序)");
    return 0;
}
```

```text
partial_sort (前 4 名有序，后面无序): 0 1 2 3 9 7 8 5 6 4
```

The first four elements `0 1 2 3` happen to be the smallest four out of ten, and they are already sorted; the remaining six `9 7 8 5 6 4` are unordered—but don't worry, they are indeed all greater than `3`, and that is sufficient. When the requirement is "the top k items," fully sorting the second half is a pure waste.

### `nth_element`: I only want the element at rank n, no need to sort the sides

For scenarios even more aggressive than `partial_sort`: we only care about the identity of the "n-th largest / n-th smallest" element, and we couldn't care less about the order on either side. The most typical use case is finding the **median**—`nth_element` is tailor-made for this.

Internally, it uses **quickselect**, which shares origins with quicksort, but after each partition, it only recurses into the side containing the target position and discards the other side. Therefore, the average complexity is **O(n)**—one whole logarithmic factor less than `sort`'s O(n log n). The trade-off is that the result only guarantees "the element at position n is this value, the left side is smaller (or equal), and the right side is larger (or equal)," while the internal order of both sides remains unordered.

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

void print(const std::vector<int>& v, const char* lbl)
{
    std::cout << lbl << ": ";
    for (int x : v) std::cout << x << ' ';
    std::cout << '\n';
}

int main()
{
    std::vector<int> v{5, 2, 9, 1, 7, 3, 8, 4, 6, 0};
    std::nth_element(v.begin(), v.begin() + 4, v.end());
    print(v, "nth_element (第 4 位 = 排序后该在的值，两边无序)");
    std::cout << "  v[4] = " << v[4] << "（10 个数升序排，第 4 位就是 4）\n";
    return 0;
}
```

```text
nth_element (第 4 位 = 排序后该在的值，两边无序): 2 0 1 3 4 5 6 7 8 9
  v[4] = 4（10 个数升序排，第 4 位就是 4）
```

`v[4]` is exactly `4`—after sorting in ascending order, that is where it belongs. On the left, `2 0 1 3` are all `<= 4`, and on the right, `5 6 7 8 9` are all `>= 4`. There is no order within the two sides. Finding the median, finding the k-th percentile, or finding the top k elements without requiring internal order among them—these are the home turf of `nth_element`.

::: warning The internal order of the left and right segments of `nth_element` is implementation-defined
Just like the element order for `sort`, the internal arrangement of the left and right segments of `nth_element` is **unspecified**—the standard only guarantees that "the nth element is in place, the left side is not greater, and the right side is not smaller". The specific arrangement of the segments above (`2 0 1 3` / `5 6 7 8 9`) is the result of libstdc++ 16.1.1 on this input, and it might be completely different on libc++ or MSVC. The only truly portable conclusion is: **trust only that `v[n]` is in place and the size relationship between the two segments**; do not rely on the internal arrangement of the left and right sides.
:::

### How to choose among the four siblings

Looking back at this family table, the selection logic boils down to one sentence: **exactly how many elements do you need to be in their final sorted position**?

- Only one element needs to be in place (median, k-th element) → `nth_element`, average O(n).
- Need the top k elements, and these k must be internally ordered (leaderboard) → `partial_sort`, approx. O(n log k).
- The entire range needs to be ordered, and we don't care about the relative order of equal elements → `sort`, O(n log n).
- The entire range needs to be ordered, and equal elements must preserve their original order → `stable_sort`.

The weaker the requirements, the faster the algorithm we can use. Many people habitually `sort` and then take the top 10 in scenarios where they "only need the top 10 entries". When the data volume is large, this becomes noticeably slow—this is the most common misuse of this algorithm family.

## Partitioning: Moving elements that meet a condition to one end

The goal of partitioning is lighter: it doesn't require ordering, only moving **all elements that meet a certain condition to one end of the range**, and placing those that don't at the other end. The most common example is "move even numbers to the front and odd numbers to the back".

`std::partition(begin, end, pred)` does this in-place, returning an iterator pointing to the "partition point"—everything before this point satisfies `pred`, and everything after does not. The complexity is O(n), but it is **unstable**: the relative order among elements that satisfy the condition, and among those that don't, may be disrupted. To preserve order, use `stable_partition` (the cost is O(n) if extra memory is available, otherwise O(n log n)).

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

void print(const std::vector<int>& v, const char* lbl)
{
    std::cout << lbl << ": ";
    for (int x : v) std::cout << x << ' ';
    std::cout << '\n';
}

int main()
{
    std::vector<int> v{1, 2, 3, 4, 5, 6, 7, 8, 9};
    auto it = std::partition(v.begin(), v.end(),
                             [](int x) { return x % 2 == 0; });
    print(v, "partition (偶数在前)");
    std::cout << "  分界点在第 " << (it - v.begin()) << " 位\n";

    std::vector<int> w{1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::stable_partition(w.begin(), w.end(),
                          [](int x) { return x % 2 == 0; });
    print(w, "stable_partition (偶数仍是 2,4,6,8 的原顺序)");
    return 0;
}
```

```text
partition (偶数在前): 8 2 6 4 5 3 7 1 9
  分界点在第 4 位
stable_partition (偶数仍是 2,4,6,8 的原顺序): 2 4 6 8 1 3 5 7 9
```

`partition` moves the even numbers `8 2 6 4` to the front, but their order is completely different from the input `2 4 6 8`. This happens because the algorithm swaps elements from both ends towards the middle, so they end up in this order by chance, without any stability guarantees. `stable_partition`, on the other hand, strictly preserves the original relative order of `2 4 6 8` and `1 3 5 7 9`, at the cost of potentially requiring an allocated buffer.

### `partition_point`: Binary search on a partitioned range

`std::partition_point(begin, end, pred)` looks like a counterpart to `partition`, but it **does not** partition the range for you. Its precondition is that **the range is already partitioned** (elements satisfying `pred` at the front, those that don't at the back). It simply performs a binary search on such a range to find the boundary point, with a complexity of O(log n).

This is most useful when combined with `partition` or a sorted range: partitioning or sorting are O(n) operations. Once done, if you need to repeatedly ask "where is the boundary?", you shouldn't scan the whole range every time. Just use `partition_point` for a binary search in O(log n).

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

int main()
{
    // 注意：这个区间必须已经分区好（偶数全在前、奇数全在后）
    std::vector<int> v{2, 4, 6, 8, 1, 3, 5, 7, 9};
    auto pp = std::partition_point(v.begin(), v.end(),
                                   [](int x) { return x % 2 == 0; });
    std::cout << "分界点在第 " << (pp - v.begin()) << " 位，值是 " << *pp << '\n';
    return 0;
}
```

```text
分界点在第 4 位，值是 1
```

::: warning partition_point won't partition for you
`partition_point` assumes the range **is already partitioned**; it simply performs a binary search to find the partition point. If the range isn't partitioned (for example, if it's unordered), calling it results in undefined behavior—it won't re-sort the elements for you. Ensure the range satisfies the preconditions before use, typically immediately following a `partition` or an operation that guarantees ordering/partitioning.
:::

## Heap Algorithms: Under the Hood of priority_queue

In Chapter 09, when we discussed `priority_queue`, we mentioned: "Its `push` is equivalent to `c.push_back(x)` + `std::push_heap`, and `pop` is equivalent to `std::pop_heap` + `c.pop_back()`". In this section, we will completely dissect these heap functions from `<algorithm>` to see exactly how they move elements within an array. Once you understand this section, the behavior of `priority_queue` will be crystal clear.

First, let's review what a heap actually is. A **binary heap** is a complete binary tree stored in an array: for a node at index `i`, its left child is at `2i+1`, its right child is at `2i+2`, and its parent is at `(i-1)/2`. This "array index ↔ tree node" mapping is the entire secret to why heaps can be implemented with arrays and why heap operations are O(log n)—finding parents and children involves only index arithmetic, no pointers. A max-heap (the default) requires **every node to be `>=` its children**, so the top of the heap `v[0]` is always the maximum value.

The standard library provides four heap operations, corresponding to heap construction, insertion, extraction, and sorting:

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

void print(const std::vector<int>& v, const char* lbl)
{
    std::cout << lbl << ": ";
    for (int x : v) std::cout << x << ' ';
    std::cout << '\n';
}

int main()
{
    std::vector<int> h{3, 1, 4, 1, 5, 9, 2, 6};

    // 1) make_heap：把任意区间原地重排成最大堆
    std::make_heap(h.begin(), h.end());
    print(h, "make_heap（堆顶 v[0] 是最大值 9）");

    // 2) push_heap：前提是已经把新元素 push_back 到末尾
    h.push_back(7);
    std::push_heap(h.begin(), h.end());
    print(h, "push_heap(7)（7 从末尾上浮到该在的位置）");

    // 3) pop_heap：把堆顶挪到末尾，剩下的重新下沉成堆
    std::pop_heap(h.begin(), h.end());
    std::cout << "  pop_heap 后，末尾存着刚取出的堆顶 = " << h.back() << '\n';
    h.pop_back();   // 真正把那个最大值从容器里删掉
    print(h, "pop_back 之后");

    // 4) sort_heap：把堆整体排成升序，排完不再是堆
    std::vector<int> s{5, 1, 9, 3, 7, 2, 8, 4, 6, 0};
    std::make_heap(s.begin(), s.end());
    std::sort_heap(s.begin(), s.end());
    print(s, "sort_heap（升序，堆结构被破坏）");
    return 0;
}
```

```text
make_heap（堆顶 v[0] 是最大值 9）: 9 6 4 1 5 3 2 1
push_heap(7)（7 从末尾上浮到该在的位置）: 9 7 4 6 5 3 2 1 1
  pop_heap 后，末尾存着刚取出的堆顶 = 9
pop_back 之后: 7 6 4 1 5 3 2 1
sort_heap（升序，堆结构被破坏）: 0 1 2 3 4 5 6 7 8 9
```

### Sift-up in `push_heap` and Sift-down in `pop_heap`

The most counter-intuitive aspect of these functions is: **they do not change the container size for you**. `push_heap` does not insert an element, and `pop_heap` does not erase an element—they only shuffle elements within an "already established" range. This is exactly why `priority_queue` calls `push_back` / `pop_back` and `push_heap` / `pop_heap` in two separate steps.

The precondition for `push_heap` is that `[begin, end-1)` is already a heap, and a new element has just been `push_back`ed to the `end-1` position. Its job is to **sift-up** this new element—comparing the new element with its parent node at index `(i-1)/2`. If it is larger than the parent, they swap, and the process continues up the tree, traversing at most `log n` levels (the tree height). In the example above, after `make_heap`, the array is `9 6 4 1 5 3 2 1`. The value 7 is `push_back`ed to the end (index 8). Its parent node is at index `(8-1)/2 = 3`, which holds the value `1`. Since 7 is greater than 1, they swap. Now 7 is at index 3, and its new parent is at index `(3-1)/2 = 1`, which holds the value `6`. Since 6 is not less than 7, we stop. Thus, 7 "bubbles" from the end to index 3. The key point is that the parent node is calculated by index, not by physical adjacency in the array—the parent of index 8 is index 3, having nothing to do with index 7 (value 1).

`pop_heap` is the reverse operation, **sift-down**. It first swaps the heap top (the maximum value) with the element at the end of the range, effectively moving the maximum value to the `end-1` position. Then, it takes the new element now at the top and sifts it all the way down—comparing it with the larger of its two children at each step. If it is smaller than that child, they swap, and the process continues until it is larger than both children. This also takes at most `log n` levels. Therefore, after `pop_heap`, the maximum value is at `back()` (still inside the container), and the remaining `[begin, end-1)` is still a heap. `priority_queue` then calls `pop_back()` to actually remove that maximum value, completing the entire `pop` operation.

`sort_heap` essentially repeats `pop_heap`: each iteration moves the current heap top to the end of the range and then shrinks the range, looping until empty. This results in an ascending sequence—because every step places the "current maximum remaining value" at the end, filling from back to front. The cost is that the **heap structure is destroyed** after sorting; if you want to use it as a heap again, you must `make_heap` anew.

The complexity of these operations corresponds exactly to the table for `priority_queue` in Chapter 09:

| Heap Operation | What it does | Complexity |
|----------------|--------------|------------|
| `make_heap` | Turns an arbitrary range into a heap in-place | O(n) |
| `push_heap` | Sifts the new element at the end into place | O(log n) |
| `pop_heap` | Sifts the heap top element down to the end | O(log n) |
| `sort_heap` | Repeatedly pops to get ascending order | O(n log n) |

So, the next time someone asks why `priority_queue` has `top` at O(1) while insertion/deletion are O(log n), you can derive it directly from these `<algorithm>` functions—`top` is just reading `c.front()`, which is constant time; `push` is one `push_back` (constant) plus one `push_heap` (O(log n)); `pop` is one `pop_heap` (O(log n)) plus one `pop_back` (constant). There is no black magic, just this family of heap algorithms.

::: warning push_heap / pop_heap Do Not Change Container Size
This is a common pitfall for beginners. `push_heap` does not perform `push_back` for you—you must append the element to the end of the container before calling it. `pop_heap` does not perform `pop_back` for you—it merely swaps the heap top to the end; whether to delete it is your responsibility. If you forget `push_back`, the new element never enters the structure. If you forget `pop_back`, that "extracted" maximum value remains lingering at the end of the container. This is one of the motivations for the standard library providing `priority_queue`—it packages these two steps for you to prevent manual errors.
:::

## C++20 Projections: Sorting by a Member Object Without Writing a Comparator

At this point, we have covered sorting, partitioning, and heaps. However, there is a frequent pain point in real-world development that remains unsolved. Suppose we have a list of `Employee` objects and want to sort by `salary`. The traditional approach requires writing a custom comparator:

```cpp
std::sort(staff.begin(), staff.end(),
          [](const Employee& a, const Employee& b) { return a.salary < b.salary; });
```

It works, but it's verbose—we are only comparing the `salary` field, yet we have to write an entire lambda to accept two objects, extract the field, and then compare. C++20's **ranges** algorithms (such as `std::ranges::sort`, `std::ranges::stable_sort`, and `std::ranges::nth_element`) introduce **projections**, which condense this into a single parameter.

The idea behind a projection is: you tell the algorithm, "before comparing, apply this function to each element (to extract the field to be compared)," and the algorithm handles the rest using the default comparison rule (`<`). Thus, the sorting above becomes:

```cpp
std::ranges::sort(staff, {}, &Employee::salary);
```

The second parameter `{}` means "use the default comparator," while the third parameter `&Employee::salary` is the projection—a pointer to a member. Internally, the algorithm extracts the fields `a.salary` and `b.salary` before comparing each pair of elements. We don't need a lambda, nor do we need to mention the field name twice; the code reads simply as "sort by salary." Want descending order? Just replace `{}` with `std::greater{}`.

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

struct Employee {
    std::string name;
    int salary;
    int age;
};

std::ostream& operator<<(std::ostream& os, const Employee& e)
{
    return os << "{" << e.name << ", $" << e.salary << ", " << e.age << "}";
}

int main()
{
    std::vector<Employee> staff{
        {"Alice", 9000, 30},
        {"Bob", 12000, 25},
        {"Carol", 9000, 40},
        {"Dave", 7000, 35},
    };

    // 投影 + 默认比较器：按 salary 升序
    auto a = staff;
    std::ranges::sort(a, {}, &Employee::salary);
    std::cout << "ranges::sort 按 &Employee::salary（升序）:\n";
    for (const auto& e : a) std::cout << "  " << e << '\n';

    // 投影 + greater：按 salary 降序
    auto b = staff;
    std::ranges::sort(b, std::greater{}, &Employee::salary);
    std::cout << "ranges::sort 按 &Employee::salary（greater，降序）:\n";
    for (const auto& e : b) std::cout << "  " << e << '\n';

    // stable_sort + 投影：salary 相同时保留输入顺序（Alice 在 Carol 前）
    auto c = staff;
    std::ranges::stable_sort(c, {}, &Employee::salary);
    std::cout << "ranges::stable_sort 按 salary（并列时保输入顺序）:\n";
    for (const auto& e : c) std::cout << "  " << e << '\n';
    return 0;
}
```

```text
ranges::sort 按 &Employee::salary（升序）:
  {Dave, $7000, 35}
  {Alice, $9000, 30}
  {Carol, $9000, 40}
  {Bob, $12000, 25}
ranges::sort 按 &Employee::salary（greater，降序）:
  {Bob, $12000, 25}
  {Alice, $9000, 30}
  {Carol, $9000, 40}
  {Dave, $7000, 35}
ranges::stable_sort 按 salary（并列时保输入顺序）:
  {Dave, $7000, 35}
  {Alice, $9000, 30}
  {Carol, $9000, 40}
  {Bob, $12000, 25}
```

Let's focus on the third group: Alice and Carol both have a salary of 9000. `ranges::stable_sort` strictly preserves the input order where Alice comes before Carol. In contrast, the `ranges::sort` in the first group makes no such guarantees, so the relative order of the two 9000 entries is unreliable. Projection is a feature widely supported across the ranges library—`sort`, `stable_sort`, `partial_sort`, `nth_element`, and `partition` all have ranges versions and accept projection parameters, with consistent logic.

::: warning Pointers to members as projections require accessible fields
Passing a pointer to a member like `&Employee::salary` is the most convenient way to specify a projection, but it requires the field to be `public`. If `salary` were `private`, you would either need to expose it or write a getter function to pass as the projection (e.g., `&Employee::get_salary`). Passing a lambda works as well (e.g., `[](const Employee& e) { return e.salary; }`). Projections are agnostic to the specific type of callable; as long as it can be invoked on a single element and returns the field to be compared, it is valid.
:::

## What C++23 Added to This Family

At this point, you might ask: Did C++23 add anything new for sorting, partitioning, or heaps? The answer is—**no, this family of algorithms received no new additions in C++23**. C++23 supplemented `<algorithm>` with `ranges::contains`, `ranges::find_last`, and `ranges::starts_with` / `ends_with`. These are **searching** algorithms (belonging to the "Non-modifying / Search" category discussed in the previous article). The core APIs for the sorting, partitioning, and heap families were finalized when they were "ranges-ified" in C++20.

I compiled these ranges algorithms on my local machine with GCC 16.1.1 using `-std=c++23`, and they all passed successfully:

```text
g++ -std=c++23 ranges_proj.cpp  →  编译通过，行为与 c++20 一致
```

Therefore, the content covered in this article (C++20 projections, Introsort, and heap algorithms) applies verbatim under C++23 / C++26, with no API changes requiring migration. If you are looking for C++23 additions in `<algorithm>`, check out `ranges::contains` / `ranges::find_last` in the search family, not here.

## Summary

We have now completed our tour of the sorting, partitioning, and heap families. Here are the key takeaways:

- **Choose the "Sorting Quartet" based on "how many elements are guaranteed to be in place"**: `nth_element` (just one, average O(n)) < `partial_sort` (top k sorted, approx. O(n log k)) < `sort` (fully sorted, O(n log n)) < `stable_sort` (fully sorted and preserves order of equivalents). The weaker the requirement, the faster the algorithm we can use.
- **`std::sort` uses Introsort internally**: It starts with quicksort, switches to heapsort if recursion depth exceeds the limit (guaranteeing worst-case O(n log n)), and finishes with insertion sort for small intervals. Thus, it avoids the O(n²) worst-case degradation of plain quicksort.
- **Partitioning is a lighter form of reordering**: `partition` moves elements satisfying a condition to one end (O(n), unstable); `stable_partition` preserves order (O(n) with memory / O(n log n) without); `partition_point` performs a binary search for the boundary point only on **already partitioned** ranges, O(log n).
- **Heap algorithms are the full foundation of `priority_queue`**: `make_heap` (build heap, O(n)) / `push_heap` (sift up, O(log n)) / `pop_heap` (sift down, O(log n)) / `sort_heap` (repeatedly pop to get ascending order, O(n log n)). Remember that `push_heap` / `pop_heap` **do not change the container size**; `priority_queue` simply wraps these steps for you.
- **C++20 Projections**: `std::ranges::sort(v, {}, &T::member)` allows sorting by member without writing lambdas, and is widely supported across ranges algorithms.
- **C++23 adds no new algorithms to this family**: The core APIs for sorting / partitioning / heaps were established in C++20; compiling these algorithms with GCC 16.1.1's `-std=c++23` behaves identically to C++20.

## References

- [cppreference: Sorting operations](https://en.cppreference.com/w/cpp/algorithm#Sorting_operations) — Overview and complexity of `sort` / `stable_sort` / `partial_sort` / `nth_element`
- [cppreference: std::sort](https://en.cppreference.com/w/cpp/algorithm/sort) — Complexity guarantees and Introsort implementation conventions
- [cppreference: std::nth_element](https://en.cppreference.com/w/cpp/algorithm/nth_element) — Quickselect and the source of average O(n)
- [cppreference: Partitioning operations](https://en.cppreference.com/w/cpp/algorithm#Partitioning_operations) — `partition` / `stable_partition` / `partition_point`
- [cppreference: Heap operations](https://en.cppreference.com/w/cpp/algorithm#Heap_operations) — `make_heap` / `push_heap` / `pop_heap` / `sort_heap`
- [cppreference: std::ranges::sort (C++20)](https://en.cppreference.com/w/cpp/algorithm/ranges/sort) — Projection parameter documentation
