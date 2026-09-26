---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: 'A thorough tour of the sorting, partitioning, and heap family: why std::sort
  is an Introsort inside (quicksort + heapsort + insertion sort, worst case O(n log
  n)), where partial_sort and nth_element each save work, how the sift-up and sift-down
  of make_heap/push_heap/pop_heap map onto the machinery under priority_queue, and
  how C++20 projections make sorting by a member comparator-free.'
difficulty: intermediate
order: 43
platform: host
prerequisites:
- 'Iterator Basics and Categories: The Glue Between Containers and Algorithms'
- 'Iterator Adapters: Reverse, Insertion, and Stream — Teaching Old Iterators New
  Tricks'
- 'Container Adapters: How stack, queue, and priority_queue Are "Wrapped"'
reading_time_minutes: 28
related:
- 'Container Selection Guide: Choosing the Right Container Based on Operations,
  Memory, and Invalidation Rules'
tags:
- host
- cpp-modern
- intermediate
- 容器
title: 'Algorithm Overview (Part 2): Sorting, Partitioning, and Heaps'
translation:
  source: documents/vol3-standard-library/iterators-algorithms/43-algorithm-overview-part2.md
  source_hash: f2b7ec14432a00e67fb00162e556f6440ff1cad84df2935cbc0d75029aeba5dd
  translated_at: '2026-09-26T01:33:33+00:00'
  engine: anthropic
  token_count: 6100
---
# Algorithm Overview (Part 2): Sorting, Partitioning, and Heaps

Last time we walked through the `<algorithm>` routines that leave elements untouched and those that move / search. This time we take on a heavier family — the ones that **rearrange an entire range**: sorting, partitioning, and heaps. They share one requirement: **random access iterators**. Remember the trap from part 40, where `std::sort` on a `list` doesn't even compile, because quicksort needs the `it + n` random jump to grab a pivot? This whole family inherits that same restriction.

But knowing "it takes random_access" is not enough. The real question is: facing a concrete sorting requirement, which of the four look-alikes — `sort` / `stable_sort` / `partial_sort` / `nth_element` — do you actually pick? What are they doing inside, and where do their complexities differ? And then there is the heap bunch (`make_heap` / `push_heap` / `pop_heap` / `sort_heap`) — back in part 09, when we covered `priority_queue`, we dropped one line: "its push is just `push_back` + `std::push_heap`". This article takes that heap machinery completely apart, to see exactly how sift-up and sift-down move elements around inside an array. We close with C++20 projections: sorting objects by one of their members, without ever writing a custom comparator again.

## The Sorting Family: Four Look-Alikes, Each Covering a Different Slice

Let's lay the four siblings of this family side by side. What they all do is "rearrange a range into some order", but **the scope of the promise differs** — some guarantee only a single element lands in place, some guarantee the first k, some sort the whole range. The less you promise, the faster you run:

| Algorithm | Guarantee | Complexity |
|------|------|--------|
| `sort` | whole range ordered | O(n log n), worst case also O(n log n) |
| `stable_sort` | whole range ordered, equal elements keep their original order | O(n log n), or O(n log² n) (depends on memory) |
| `partial_sort` | first k sorted (and they are the smallest k), rest unordered | about O(n log k) |
| `nth_element` | the nth element lands exactly where it belongs after sorting, everything to its left is smaller, everything to its right is larger, both sides internally unordered | average O(n) |

That table is the entire essence of this section. Whenever a requirement sounds like "I only need the top k" or "I only need the median", **do not run `sort` over the whole range and then take a slice** — that's paying an extra `log n` factor for nothing. Let's take them one by one.

### sort: Introsort, and Why Quicksort Never Degrades to O(n²)

`std::sort` is not a pure quicksort inside. The headache with pure quicksort is that on nearly-sorted input, or when pivot selection goes sideways, it degrades to O(n²) — the classic "quicksort worst case" interview question. The standard library obviously cannot let `sort` suddenly drop an order of magnitude on certain inputs, so libstdc++ and the other mainstream implementations all use the same trick: **Introsort** (introspective sort), which bolts the strengths of three algorithms together.

Introsort works like this: start with **quicksort**, which is fastest on average; but record the depth at each recursion level, and once the depth passes the `2·log₂ n` threshold — a sign that quicksort is degenerating toward unbalanced partitions — **switch to heapsort**. Heapsort's worst case is O(n log n), rock solid. When recursion bottoms out and subranges get small (a dozen-ish elements), switch to **insertion sort**: at small sizes its constants are small and it is cache-friendly, beating continued recursion.

Stitch the three phases together and the average performance approaches the fastest quicksort, the worst case is locked down at O(n log n) by the heapsort fallback, and small ranges are finished off by insertion sort to save constants. That is why the standard's complexity guarantee for `std::sort` is **O(n log n)** — specifically, "applies approximately `N·log(N)` comparisons" — with **no room for degradation**. The "worst case also O(n log n)" we wrote in the table above comes from exactly this: Introsort's "introspective" part means it notices quicksort about to degrade and proactively swaps algorithms. Incidentally, it took until C++11 for the standard to pin down this worst-case guarantee (early `sort` complexity was "average O(n log n)" with no worst-case floor), so on modern implementations you can stop worrying about quicksort degradation.

::: warning sort does not guarantee the order of equal elements
Note that `sort` makes no promise about the relative order of equal elements — which of two same-valued elements comes first after sorting is **unspecified**. If your logic depends on "ties keep their original front-to-back relation" (say you already sorted by hire date and now sort by salary, and people with equal salaries must not have their hire order shuffled), you need `stable_sort`.
:::

Let's run it and get a feel for how `sort` and `stable_sort` differ on equal elements. To make the difference visible, we need input with **lots of duplicate keys** — the more elements share a key, the more room an unstable sort has to shuffle them:

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

struct Point {
    int key;   // the field we sort by
    int tag;   // used to track the "original order"
};

void print_tagged(const std::vector<Point>& v, const char* lbl)
{
    std::cout << lbl << ": ";
    for (const auto& p : v) std::cout << "{" << p.key << "," << p.tag << "} ";
    std::cout << '\n';
}

int main()
{
    // 3 key groups (1/2/3), each with 8 tags 0..7 — enough equal keys to expose instability
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

Run with `g++ -std=c++20 -O2` (local GCC 16.1.1):

```text
sort        (key 相同的 tag 顺序被算法打乱): {1,1} {1,2} {1,3} {1,4} {1,5} {1,6} {1,7} {1,0} {2,4} {2,7} {2,6} {2,5} {2,3} {2,2} {2,1} {2,0} {3,0} {3,1} {3,2} {3,3} {3,4} {3,5} {3,6} {3,7}
stable_sort (key 相同的 tag 仍是 0..7 原顺序): {1,0} {1,1} {1,2} {1,3} {1,4} {1,5} {1,6} {1,7} {2,0} {2,1} {2,2} {2,3} {2,4} {2,5} {2,6} {2,7} {3,0} {3,1} {3,2} {3,3} {3,4} {3,5} {3,6} {3,7}
```

In both cases the `key` values all landed in 1, 2, 3 order — that part is identical. The difference is entirely in "the order of tags within one key group": `sort` rearranged the tags of `key=1` into `1,2,3,4,5,6,7,0` and those of `key=2` into `4,7,6,5,3,2,1,0` — no trace of the input's `0..7` is preserved; `stable_sort` strictly keeps `0,1,2,3,4,5,6,7`. That is what "unstable" means: not that shuffling is guaranteed, but that the standard **does not promise** preservation — exactly what you get depends on the algorithm's internal swap path, and a different input or a different implementation can land differently. Code that depends on the order of equal elements must use `stable_sort`; the cost is that stable sorting usually needs a buffer as large as the range (degrading to O(n log² n) when that cannot be allocated), so **if you have no stability requirement, use `sort`** — faster and leaner on memory.

::: warning sort's ordering of equal elements is implementation-defined
Precisely because `sort` does not guarantee the order of equal elements, the "what it shuffled into" output above is libstdc++ 16.1.1's result on this particular input — switch to libc++, MSVC, or a different input, and the tag permutation can be completely different. We show it only to "see the shuffling with your own eyes"; the only portable conclusion is one sentence: the order of equal elements under `sort` is nothing you can rely on — if you need preservation, reach for `stable_sort`.
:::

### partial_sort: I Only Want the Top k, and I Want Them Sorted

Plenty of requirements sound like "find the top k, and those k must also be in order" — a leaderboard that only shows the top 10, ranked. That is exactly what `partial_sort(begin, middle, end)` does: afterwards, `[begin, middle)` holds the smallest `k = middle - begin` elements of the entire range, **sorted among themselves**; `[middle, end)` keeps the remaining elements with **no ordering guarantee**.

Its approach maintains a min-heap over the first k positions: linearly scan the elements behind, and whenever one is smaller than the current heap top (the largest of the current top k), replace the top and sift down to fix the heap. Once the scan completes, the top k are settled, and it finishes with one sort over that min-heap. Complexity is roughly **O(n log k)** — the smaller the `k`, the bigger the saving; as `k` approaches `n` it degrades to roughly a full sort.

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

The first four positions `0 1 2 3` are exactly the four smallest of the ten numbers, sorted; the trailing six `9 7 8 5 6 4` are a mess — but don't worry, they are indeed all greater than `3`, and that's all we need. When the requirement is "the top k", sorting the back half as well is pure waste.

### nth_element: I Only Need the nth One, Leave Both Sides Unsorted

One notch more radical than `partial_sort`: I only care which single element is "the nth largest / nth smallest"; whether the two sides around it are a mess is irrelevant. The most typical case is the **median** — `nth_element` is tailor-made for this kind of requirement.

Inside it runs **quickselect**, a sibling of quicksort: after each partition it recurses only into the side containing the target position and throws the other side away outright. Hence the average complexity of **O(n)** — a whole logarithmic factor below `sort`'s O(n log n). The price is that the result only guarantees "position n holds the right value, everything to its left is smaller (or equal), everything to its right is larger (or equal)", with both sides internally unordered.

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

`v[4]` is exactly `4` — that's where it belongs once sorted ascending. On the left, `2 0 1 3` are all `<= 4`; on the right, `5 6 7 8 9` are all `>= 4`; neither side is internally ordered. Medians, k-th percentiles, "top k without needing the k internally sorted" — these are all `nth_element`'s home turf.

::: warning the internal order of nth_element's two sides is implementation-defined
Just like the order of equal elements under `sort`, "how each side is arranged internally" after `nth_element` is **unspecified** — the standard only guarantees "position n is in place, nothing on the left is greater, nothing on the right is smaller". The particular arrangements of the two sides above (`2 0 1 3` / `5 6 7 8 9`) are libstdc++ 16.1.1's result on this input; on libc++ / MSVC they can be completely different. The only portable conclusion, again in one sentence: **trust only that the value at `v[n]` is in place and the left/right magnitude relation holds** — never rely on the internal arrangement of either side.
:::

### How to Pick Among the Four Siblings

Look back at the family table and the selection logic is one sentence: **how many elements do you actually need in their final places**?

- Only one element in place (median, k-th ranked) → `nth_element`, average O(n).
- The top k, sorted among themselves (a leaderboard) → `partial_sort`, about O(n log k).
- The whole range ordered, relative order of equal elements irrelevant → `sort`, O(n log n).
- The whole range ordered, and equal elements must keep their original order → `stable_sort`.

The weaker the requirement, the faster the algorithm you get to use. Many people habitually `sort` and take the first 10 in a "I only need the top 10" scenario — once the data grows, the slowdown becomes obvious. This is the most common misuse of this family.

## Partitioning: Move Everything Matching a Condition to One End

Partitioning sets a lighter goal: no ordering required, just move **every element satisfying some condition to one end of the range**, with the non-matching ones at the other. The canonical example is "evens to the front, odds to the back".

`std::partition(begin, end, pred)` does this in place, returning an iterator to the "partition point" — everything before it satisfies `pred`, everything after it doesn't. Complexity O(n), but it is **unstable**: the relative order among satisfying elements, and among non-satisfying ones, may be scrambled. Need preservation? Use `stable_partition` (at the cost of O(n) when extra memory is available for it, O(n log n) when not).

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

`partition` moved the evens `8 2 6 4` all to the front, but their order is nothing like the input's `2 4 6 8` — they happened to land that way as the algorithm swapped from both ends toward the middle; there is no order-preservation guarantee. `stable_partition`, in contrast, strictly preserved the original orders of both `2 4 6 8` and `1 3 5 7 9`, paying for it with a possible extra buffer allocation.

### partition_point: Binary Search on an Already-Partitioned Range

`std::partition_point(begin, end, pred)` looks like `partition`'s twin, but it **does not** partition for you — its precondition is that **the range is already partitioned** (satisfying `pred` up front, not satisfying after); on such a range it simply binary-searches for that partition point, at O(log n).

Where it shines is in combination with `partition` or sorted ranges: partitioning and sorting are O(n)-or-worse operations, so after doing one, if you later want to ask "where is the partition point?" again and again, you can't rescan every time — just `partition_point` with a binary search, O(log n).

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

int main()
{
    // Note: this range must already be partitioned (all evens in front, all odds behind)
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

::: warning partition_point does not partition for you
`partition_point` assumes the range **is already partitioned**; it only binary-searches for the partition point. Call it on a range that isn't partitioned at all (a shuffled one, say) and the result is undefined — it will not reorganize anything for you. Confirm the precondition before use, which in practice means calling it right after a `partition` or some other operation that guarantees ordering / partitioning.
:::

## Heap Algorithms: The Machinery Underneath priority_queue, Taken Apart

Back in part 09, while covering `priority_queue`, we planted a line: "its `push` is equivalent to `c.push_back(x)` + `std::push_heap`, and its `pop` to `std::pop_heap` + `c.pop_back()`". This section takes those `<algorithm>` heap functions completely apart, to see exactly how they move elements inside an array. Once this section clicks, `priority_queue`'s behavior holds no more secrets either.

First, a refresher on what a heap is. A **binary heap** is a complete binary tree stored in an array: node `i`'s left child is `2i+1`, right child is `2i+2`, parent is `(i-1)/2`. This "array index ↔ tree node" mapping is the entire secret of why a heap can live in an array and why heap operations run in O(log n) — finding parent or child is index arithmetic, no pointers involved. A max-heap (the default) requires **every node to be `>=` its children**, so the top of the heap, `v[0]`, is always the maximum.

The standard library provides four heap operations, covering heap construction, insertion, extraction, and full sorting:

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

    // 1) make_heap: rearrange an arbitrary range into a max-heap in place
    std::make_heap(h.begin(), h.end());
    print(h, "make_heap（堆顶 v[0] 是最大值 9）");

    // 2) push_heap: precondition — the new element has already been push_back'ed at the end
    h.push_back(7);
    std::push_heap(h.begin(), h.end());
    print(h, "push_heap(7)（7 从末尾上浮到该在的位置）");

    // 3) pop_heap: move the heap top to the end; the rest sifts back down into a heap
    std::pop_heap(h.begin(), h.end());
    std::cout << "  pop_heap 后，末尾存着刚取出的堆顶 = " << h.back() << '\n';
    h.pop_back();   // actually remove that maximum from the container
    print(h, "pop_back 之后");

    // 4) sort_heap: sort the whole heap into ascending order; no longer a heap afterwards
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

### push_heap's Sift-Up and pop_heap's Sift-Down

The most counter-intuitive thing about these functions: **none of them changes the container's size for you**. `push_heap` inserts no element, and `pop_heap` deletes none — they only move elements around within an already-shaped range. That is exactly why `priority_queue` splits its calls into two steps, pairing `push_back` / `pop_back` with `push_heap` / `pop_heap`.

`push_heap`'s precondition is: `[begin, end-1)` is already a heap, and the new element has just been `push_back`'ed into position `end-1`. Its job is to **sift that new element up (sift-up)** — compare the new element with its parent `(i-1)/2`, swap if it is larger, and keep climbing, at most `log n` levels up the tree. In the example above, after `make_heap` the array is `9 6 4 1 5 3 2 1`; 7 was `push_back`'ed to the end (index 8), its parent is index `(8-1)/2 = 3`, holding the value `1` — 7 beats 1, swap; now 7 lands at index 3, and its new parent is index `(3-1)/2 = 1`, holding the value `6`; 6 is not smaller than 7, stop. And so 7 "bubbled" from the tail up to the index-3 level. The key point is that the parent is determined by index arithmetic, not by array adjacency — index 8's parent is index 3, which has nothing to do with index 7 (value 1).

`pop_heap` is the reverse: a **sift-down**. It first swaps the heap top (the maximum) with the element at the end of the range, freeing the maximum into position `end-1`; then it sinks the element that got swapped onto the top — at each level comparing against the larger of its two children and swapping down while it is the smaller one, until it is at least as large as both children, again `log n` levels. So after `pop_heap`, the maximum sits at `back()` (still inside the container), and the remaining `[begin, end-1)` is still a heap — `priority_queue` then calls `pop_back()` to actually delete that maximum, completing the whole `pop`.

`sort_heap` is nothing more than repeated `pop_heap`: move the current heap top to the end of the range, shrink the range, and loop until empty. The result is an ascending sequence — each step lays down "the largest of what remains", so from tail to head it comes out ascending. The cost: after sorting, **the heap structure is destroyed**; to reuse it you must `make_heap` again.

The complexities of these operations are exactly that `priority_queue` table from part 09:

| Heap operation | What it does | Complexity |
|--------|--------|--------|
| `make_heap` | turn an arbitrary range into a heap in place | O(n) |
| `push_heap` | sift the new tail element up into place | O(log n) |
| `pop_heap` | sift the heap top down to the end | O(log n) |
| `sort_heap` | repeated pops, yielding ascending order | O(n log n) |

So the next time someone asks why `priority_queue`'s `top` is O(1) while push and pop are O(log n), you can derive it straight from these `<algorithm>` functions — `top` just reads `c.front()`, constant time; `push` is one `push_back` (constant) plus one `push_heap` (O(log n)); `pop` is one `pop_heap` (O(log n)) plus one `pop_back` (constant). No black magic anywhere — it is all this heap-algorithm family.

::: warning push_heap / pop_heap do not change the container's size
This is the trap beginners hit most often. `push_heap` does not `push_back` for you — you must get the element into the container's tail before calling it; `pop_heap` does not `pop_back` for you either — it only swaps the heap top to the end; whether to delete is your business. Skip the `push_back`, and the new element never entered; skip the `pop_back`, and that "extracted" maximum keeps squatting at the container's tail. This is part of why the standard library provides `priority_queue` — it bundles the two steps for you so a hand-written version can't drop one.
:::

## C++20 Projections: Sort by an Object Member, No Comparator Needed

That covers sorting, partitioning, and heaps — but one high-frequency pain point from real work is still unsolved. Say you have a bunch of `Employee`s and want to sort by `salary`; the traditional way is to write your own comparator:

```cpp
std::sort(staff.begin(), staff.end(),
          [](const Employee& a, const Employee& b) { return a.salary < b.salary; });
```

It works, but it's wordy — all you wanted was "compare the salary field", yet you write an entire lambda that receives two objects, pulls the field out of each, and compares. The C++20 **ranges** family of algorithms (`std::ranges::sort`, `std::ranges::stable_sort`, `std::ranges::nth_element`, and so on) introduces **projections**, compressing the whole thing into one argument.

The idea of a projection: you tell the algorithm "before comparing, apply this function to each element (extract the field to compare)", and the algorithm keeps applying the default comparison rule (`<`) on its own. The sort above then becomes:

```cpp
std::ranges::sort(staff, {}, &Employee::salary);
```

The second argument `{}` means "use the default comparator"; the third argument `&Employee::salary` is the projection — a pointer to member. Internally, before each pair of elements is compared, the algorithm extracts `a.salary` and `b.salary` first. No lambda, no naming the field twice — it reads exactly like what it does: "sort by salary". Want descending? Swap `{}` for `std::greater{}`.

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

    // projection + default comparator: ascending by salary
    auto a = staff;
    std::ranges::sort(a, {}, &Employee::salary);
    std::cout << "ranges::sort 按 &Employee::salary（升序）:\n";
    for (const auto& e : a) std::cout << "  " << e << '\n';

    // projection + greater: descending by salary
    auto b = staff;
    std::ranges::sort(b, std::greater{}, &Employee::salary);
    std::cout << "ranges::sort 按 &Employee::salary（greater，降序）:\n";
    for (const auto& e : b) std::cout << "  " << e << '\n';

    // stable_sort + projection: equal salaries keep input order (Alice before Carol)
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

Focus on the third group: Alice and Carol both earn 9000, and `ranges::stable_sort` strictly preserved the input order with Alice before Carol; the first group's `ranges::sort` gives no such guarantee — the relative order of the two 9000s is not dependable. Projections are a feature the ranges family supports across the board — `sort`, `stable_sort`, `partial_sort`, `nth_element`, `partition` **all have ranges versions and all accept a projection argument**, with identical semantics.

::: warning a pointer-to-member projection requires the field to be accessible
Passing a pointer to member like `&Employee::salary` is the most convenient form of projection, but it requires the field to be `public`. If `salary` is `private`, you either expose it or pass a getter function as the projection (`&Employee::get_salary`); a lambda works too (`[](const Employee& e) { return e.salary; }`). A projection does not care about the callable's shape — anything invocable on a single element that returns the field to compare qualifies.
:::

## What C++23 Added to This Family

At this point you might ask: did C++23 add anything new for sorting / partitioning / heaps? The answer — **this family of algorithms itself gained nothing new in C++23**. What C++23 added to `<algorithm>` is the **searching** side: `ranges::contains`, `ranges::find_last`, `ranges::starts_with` / `ends_with` (belonging to the "non-modifying / searching" group from the previous article); the core APIs of the sorting, partitioning, and heap families were settled once C++20's ranges-ification landed.

I compiled these ranges algorithms under `-std=c++23` with local GCC 16.1.1 — all of them passed cleanly:

```text
g++ -std=c++23 ranges_proj.cpp  →  编译通过，行为与 c++20 一致
```

So everything in this article (C++20 projections, Introsort, the heap algorithms) applies unchanged under C++23 / C++26 — no API churn to migrate. For the new C++23 arrivals in `<algorithm>`, look at the searching family's `ranges::contains` / `ranges::find_last`; that is not this article's business.

## Summary

That completes our walk through the sorting, partitioning, and heap family; let's collect the key conclusions:

- **Pick among the four sorting siblings by "how many elements must land in place"**: `nth_element` (just one, average O(n)) < `partial_sort` (top k and sorted, about O(n log k)) < `sort` (whole range ordered, O(n log n)) < `stable_sort` (whole range ordered with equal elements kept in order). The weaker the requirement, the faster the algorithm you can use.
- **`std::sort` is an Introsort inside**: quicksort as the workhorse, a switch to heapsort when recursion depth exceeds the budget (guaranteeing worst-case O(n log n)), and insertion sort to finish small ranges. Hence it has none of quicksort's O(n²) degradation.
- **Partitioning is the lighter rearrangement**: `partition` moves everything matching to one end (O(n), unstable); `stable_partition` preserves order (O(n) with memory available / O(n log n) without); `partition_point` only binary-searches an **already-partitioned** range for the partition point, O(log n).
- **The heap algorithms are all of `priority_queue`'s underbelly**: `make_heap` (build the heap, O(n)) / `push_heap` (sift-up, O(log n)) / `pop_heap` (sift-down, O(log n)) / `sort_heap` (repeated pops into ascending order, O(n log n)). Remember that `push_heap` / `pop_heap` **do not change the container's size** — `priority_queue` exists to bundle those two steps for you.
- **C++20 projections**: `std::ranges::sort(v, {}, &T::member)` makes member-based sorting lambda-free, and the whole ranges family supports them.
- **C++23 added no new algorithms to this family**: the core sorting / partitioning / heap APIs were settled in C++20, and under local GCC 16.1.1's `-std=c++23` these families compile and behave identically to C++20.

## References

- [cppreference: Sorting operations](https://en.cppreference.com/w/cpp/algorithm#Sorting_operations) — overview and complexity of `sort` / `stable_sort` / `partial_sort` / `nth_element`
- [cppreference: std::sort](https://en.cppreference.com/w/cpp/algorithm/sort) — complexity guarantees and the Introsort implementation convention
- [cppreference: std::nth_element](https://en.cppreference.com/w/cpp/algorithm/nth_element) — quickselect and where the average O(n) comes from
- [cppreference: Partitioning operations](https://en.cppreference.com/w/cpp/algorithm#Partitioning_operations) — `partition` / `stable_partition` / `partition_point`
- [cppreference: Heap operations](https://en.cppreference.com/w/cpp/algorithm#Heap_operations) — `make_heap` / `push_heap` / `pop_heap` / `sort_heap`
- [cppreference: std::ranges::sort (C++20)](https://en.cppreference.com/w/cpp/algorithm/ranges/sort) — the projection parameter explained
