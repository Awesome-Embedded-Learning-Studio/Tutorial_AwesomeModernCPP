---
title: "Dynamic Arrays: A Block of Memory That Moves House"
description: "Part 1 of the data structures primer. The fixed length and stack-space limits of static arrays (a 2 MiB stack array crashes the moment you really run it, exit code 0xC00000FD) lead us to contiguous storage on the heap; with the three members data/size/cap we sketch the embryo of a dynamic array. A real run on MSVC x64 yields the capacity sequence 1,2,3,4,6,9,13,19,28,42 and the buffer address changing on every reallocation; a timed test with 200,000 elements measures about 1.9 seconds for +1 growth versus about 0.6 milliseconds for doubling; ASan reproduces the old pointer's use-after-free after reallocation (WRITE of size 4). All output comes from real runs on this machine, not retelling."
chapter: 7
order: 1
tags:
  - host
  - cpp-modern
  - beginner
  - vector
  - 容器
  - 基础
  - 入门
difficulty: beginner
platform: host
reading_time_minutes: 15
prerequisites:
  - "Volume 1, Chapter 12: Memory Layout (stack and heap)"
  - "Volume 1: Pointer Basics (address-of, dereference)"
related:
  - "Deep Dive into std::vector: Three Pointers, Reallocation, and Iterator Invalidation"
  - "mini STL in Practice (Part 2): Vector — Growth and Relocation"
cpp_standard: [11]
translation:
  source: documents/vol3-standard-library/containers/primer/01-dynamic-array.md
  source_hash: dfb0cc92a97e5fcc9a3c3d4c1d3081120bda0e7ba05068408aea17f51999251b
  translated_at: '2026-09-25T09:08:20+00:00'
  engine: anthropic
  token_count: 3500
---

# Dynamic Arrays: A Block of Memory That Moves House

Friends, we are about to begin studying the standard library. I believe that even before you clicked open our data-structures library coverage, the phrase `int a[100]` was no stranger to you. (Huh? A stranger? Then go read the C tutorial!) We have also had a light brush with `std::vector<int>`. They both look like arrays, right? **So where is the difference? With the former, the cells are nailed down the moment we write that line of code — once compiled into the executable, not a single extra byte can appear, and I trust nobody will argue with that. Yet, strangely, the latter can swallow a million numbers in a row without blowing up. Where does the extra memory come from?**

Borrowed from the heap.

Yes, borrowed from the heap. And never the same block twice — once it is full, we swap to a bigger one and move every element over wholesale. That one sentence is the entire secret of `std::vector`. In this piece we lay it open: where the memory lives, who does the counting, how the move is carried out, how expensive a single move is, and which things quietly go invalid after the move. Get these nailed down, and whether you later read the implementation-level deep dive or go touch linked lists and hash tables, the ground under your feet is all bedrock.

## Why Do Static Arrays Fall Short in Business Scenarios?

I have given it some thought, and there are really three points: the length is set in stone, the stack is too small, and you cannot add to it.

If you do backend development, when data comes in to be registered, you could never get away with saying: exactly 10 users submitted forms this second — the count is surely variable, right! Sometimes none at all, sometimes hundreds.

For friends coming from embedded, let me put it this way: while writing the code, we often do not know how much data will arrive at runtime — how many frames per second a sensor sends, how many log lines pile up, how many numbers the user types in — it is all runtime business, and fixing the length at compile time is betting with your eyes closed.

At the root, the problem is still the notation `int a[100]`: the moment we write this line down, the length becomes part of the type and must be given at compile time. Want to change it later? No chance, friends!

Second is space. An array declared this way lives on the stack by default, and the stack is small: 1 MiB by default on Windows, commonly 8 MiB on Linux. Let's actually run one (MSVC x64):

```cpp
#include <cstdio>

int main() {
    int big[2 * 1024 * 1024];  // 2 MiB, over the default 1 MiB stack
    big[0] = 41;
    std::printf("big[0] = %d\n", big[0]);
    return 0;
}
```

Not a single line of output — the process dropped dead at the starting line.

```text
EXIT_CODE:-1073741571
```

Right, I ran this on Windows (when I slack off at work, only Windows is available to me): that number is Windows' `0xC00000FD`, the stack-overflow status code. On Linux it is the same medicine in a different bottle — blowing past the stack limit crashes just the same, only the cause of death becomes a segmentation fault. The stack is the fast lane reserved for local variables and function calls: small in capacity, carved out in one lump by the operating system when the thread is created; to change it you have to open a dedicated hole in the linker options or the thread attributes. Using it to hold data whose size you only learn at runtime is using the wrong room.

> I have genuinely seen someone argue back at me — "I write OI like this" — until I saw his template code, and I said outright: bro, living in the bss / data section and living on the stack are not the same thing! If you are an OJ contestant, remember when declaring big arrays: either mark them `static` or toss them into global scope — I recommend globals, because then you skip typing the `static` keyword.

Third, even if you guessed the length right, there is no headroom to fit one more element midway. An array is a row of cells sitting contiguously in memory, with somebody else's territory pressed right up behind it; growing would mean annexing the neighbors' land along with it — not realistic.

## The Data Lives on the Heap, and We Hold a Pointer in Hand

These three towering thresholds all point to the same solution: move the data onto the heap. The heap is a large stretch of memory requested on demand — with `new`, any size is negotiable (as long as physical memory allows), and when to return it and how much is also our call. On the stack we keep only a small handle:

```cpp
int* data = new int[8];   // allocate 8 ints on the heap
data[0] = 41;             // used exactly like an array
delete[] data;            // must be released when done
```

![A pointer on the stack pointing to contiguous cells on the heap](./01-stack-heap-pointers.drawio)

In usage we cannot tell `data[0]` from `a[0]` — subscript access goes through the same address arithmetic. The difference is entirely behind the curtain: `a`'s cells are parceled out at compile time and reclaimed automatically when the function returns; `data`'s cells are borrowed at runtime and must be returned by hand — forgetting to return them is a memory leak.

This bare-bones version immediately exposes two new problems, though. The first: we borrowed 8 cells — what about the 9th piece of data? Borrow a bigger block, move the old elements over, return the old memory — we will take that action apart in the next section. The second is more hidden: if the function `return`s midway or throws an exception, `delete[]` gets skipped and the leak happens anyway. Letting a bare pointer manage a resource leaves too many failure paths. We will first solve "how to fit it all"; encapsulation and automatic reclamation get handed to RAII at the end.

## Two Numbers to Keep Straight: How Many Are In, and How Many Can Fit

Wrap the bare pointer together with its counts, and the embryo of a dynamic array takes shape:

```cpp
struct IntVec {
    int*   data;  // start of that contiguous block on the heap
    size_t size;  // how many are in (the first size cells hold live elements)
    size_t cap;   // how many fit at most (the cell count of the whole block)
};
```

![size versus capacity: 8 cells of capacity, the first 5 holding elements](./01-size-capacity.drawio)

`size` and `cap` are two different things — the first intuition to build for understanding dynamic arrays: `size` says how many live elements there are right now; `cap` says how many cells this block of memory holds in total. The empty stretch in the middle is not handed back; it stays ready for the next `push_back` to use directly. Handing it back at once and borrowing again next time costs more in allocator round-trips than keeping it does.

With these two numbers, appending an element at the tail splits into two cases:

```cpp
void push_back(IntVec& v, int x) {
    if (v.size < v.cap) {
        v.data[v.size] = x;   // room left: write one cell, bump the count
        v.size += 1;
        return;
    }
    grow(v);                  // capacity full: grow — the next section's business
    v.data[v.size] = x;
    v.size += 1;
}
```

When spare capacity is plentiful, all `push_back` does is write one cell and add one to `size` — that is the whole reason it is cheap. The `std::vector` object itself is nowhere near fat either. Let's run it for real:

```text
sizeof(std::vector<int>) = 24 bytes
```

24 bytes — exactly the width of three pointers on a 64-bit machine. All the meat is on the heap; the object itself is skinny down to nothing but counts. How exactly those three pointers are arranged (start, one-past-the-end, end of storage) and how `size()` and `capacity()` are derived from them — the [`Vector Deep Dive`](../03-vector-deep-dive.md) piece in this volume carries the full derivation; here, remembering the conclusion is enough.

## Full Means Moving Day: Swap In a Bigger Block of Memory

The moment `size` catches up with `cap` is moving day. What we do is just three steps: borrow a bigger new block of memory, move the old elements over, return the old memory.

```cpp
void grow(IntVec& v) {
    size_t ncap = v.cap == 0 ? 1 : v.cap * 2;  // new capacity doubles
    int*   nd   = new int[ncap];               // request a bigger new buffer
    for (size_t i = 0; i < v.size; ++i) {
        nd[i] = v.data[i];                     // copy each old element over
    }
    delete[] v.data;                           // free the old buffer
    v.data = nd;
    v.cap  = ncap;
}
```

![Three steps of reallocation: request a new buffer, relocate the elements, free the old buffer](./01-reallocation-steps.drawio)

The code is plain, but two details deserve our eyes. The new home's size is `cap * 2`, not `cap + 1` — that factor is the soul of the whole design, and the next section lets the timing data do the talking. The relocation loop is written as per-element assignment; for a trivial type like `int`, the standard library actually performs a whole-block copy (the `memmove` family); swap in complex objects as elements, and relocation further drags in how constructions pair with destructions — that is another tier of fastidiousness altogether, and vol8's [`Vector — Growth and Relocation`](../../../vol8-domains/data-structure/02-vector-growth-and-relocation.md) dissects exactly that.

Let's have `std::vector` itself demonstrate a move. Push 40 numbers in a row, print one line each time the capacity changes (a real run on MSVC x64):

```text
born    : size=0 capacity=0 data=0000000000000000
push #1  : size=1   capacity=1   data=0000016D1A7AF410
push #2  : size=2   capacity=2   data=0000016D1A7AF430
push #3  : size=3   capacity=3   data=0000016D1A7B6C90
push #4  : size=4   capacity=4   data=0000016D1A7B6B10
push #5  : size=5   capacity=6   data=0000016D1A7B6DB0
push #7  : size=7   capacity=9   data=0000016D1A7B7380
push #10 : size=10  capacity=13  data=0000016D1A7A5B60
push #14 : size=14  capacity=19  data=0000016D1A7AC890
push #20 : size=20  capacity=28  data=0000016D1A7AF410
push #29 : size=29  capacity=42  data=0000016D1A7AF490
```

Read the two columns together. The capacity column: 1, 2, 3, 4, 6, 9, 13, 19, 28, 42, roughly ×1.5 each time — that is the MSVC STL's strategy; libstdc++ and libc++ multiply by 2, giving the sequence 0, 1, 2, 4, 8, 16, 32 (for the comparison see [`Vector Deep Dive`](../03-vector-deep-dive.md)). The standard does not prescribe the factor — each house picks its own — but all are strictly greater than 1. Why? The next section delivers the verdict.

Now look at the address column: every time the capacity changes, `data` changes too. Every reallocation is a wholesale move; the old buffer is invalidated as a whole. And here is a detail worth chewing on: the address `push #20` got, `...AF410`, is exactly the same as `push #1`'s.

Hey! That is genuinely not a coincidence — under the 1.5× strategy, an old block handed back earlier happens to be exactly big enough for a later reallocation to reuse: the allocator rented the just-returned house right back to us. Why 1.5 has this property and 2 does not — `Vector Deep Dive` has a beautiful derivation of it; here, remembering the phenomenon is enough.

You can also reproduce this scene with your own hands — we have put the demo on the online compiler:

<OnlineCompilerDemo
  title="Growth in Action: Capacity Sequence and Buffer Addresses"
  source-path="code/examples/vol3/primer_01_vector_growth.cpp"
  description="Run it online once: GCC's libstdc++ uses the doubling strategy (1, 2, 4, 8, …), different from MSVC's 1.5× sequence in this article — the difference between the two strategies is itself the point. Then watch the data column: the address changes on every reallocation."
  allow-run
/>

## Double, or Add One: The Price of Moving Differs by Three Orders of Magnitude

Back to that factor inside `grow`. Intuitively, `cap + 1` seems to save more memory: borrow exactly as many as needed, not one cell more. Let's push 200,000 numbers through each of the two strategies and time it for real (MSVC `/O2`, three runs each):

```text
appending 200000 ints, 3 runs each (times in microseconds):
run 1:  +1 growth = 2016464 us   x2 growth = 676 us
run 2:  +1 growth = 1868235 us   x2 growth = 662 us
run 3:  +1 growth = 1869491 us   x2 growth = 590 us
```

![Intuition for amortization: the vast majority of pushes are cheap, with an occasional expensive one at a capacity boundary](./01-amortized-cost.drawio)

The +1 strategy takes about 1.9 seconds; the doubling strategy, under 1 millisecond — three orders of magnitude apart. The derivation is not complicated. First, the +1 strategy: before the Nth element arrives, all N-1 old elements must be moved, for a total of 0+1+2+…+(N-1) ≈ N²/2 moves; with N at 200,000, that is two hundred billion copies. The doubling strategy moves house only at the capacity points 1, 2, 4, 8, …, and its per-move volumes sum to 1+2+4+…+N/2 < N — fewer than 400,000 moves in total for 200,000 elements. Quadratic versus linear: let the data grow, and it becomes seconds versus microseconds.

That is why the complexity the standard promises for `push_back` is written as **amortized constant**. The word "amortized" is the key: the single `push_back` that triggers a move is, in hard fact, O(n). The intuitive telling is that we spread the cost of the occasional expensive move over the string of cheap writes before it, so the average per operation is still constant. The figure above draws exactly this shape: the vast majority of `push_back`s are short bars of height 1, an occasional tall bar stands at a capacity boundary, and the tall bars space farther and farther apart, so the average height gets diluted back down to a constant.

If we know in advance how much we will store, `reserve(n)` can borrow enough in one go and spare that whole stretch from moving — the hiccup on the hot path simply ceases to exist. The details of capacity-facing interfaces like `shrink_to_fit` and `resize` all live in the `Vector Deep Dive` piece; this primer won't dig into them here.

## Insert One in the Middle: The Whole Back Half Shifts Seats

At this point the dynamic array in our hands looks cheap at every turn: subscript access lands in one step (base address plus offset times cell width, pure arithmetic), and appending at the tail is amortized constant. The cost hides in the middle. Try inserting a number at the front (real run):

```text
before insert at front: 0 1 2 3 4 5 6 7
after  insert at front: 99 0 1 2 3 4 5 6 7
```

All 8 old elements shifted back one cell each. That is the constraint contiguous storage lays down: the cells must run in an unbroken row, so cell 0 has to be vacated for the new element and the 8 behind can only shuffle right one by one; deletion works the same way — dig one out of the middle, and everything behind shifts left to fill the hole. The further forward the insertion point, the more shifting; on average, one insertion or deletion in the middle costs O(n) element moves. Cheap at the tail, expensive at the head — these are the two faces of the same coin called "contiguous": it grants us O(1) random access and lovely cache locality, and it also fixes upon us the moving duty for operations in the middle.

So is there a structure where inserting in the middle makes nobody move? There is. The price is that finding the i-th element no longer lands in one step — we have to start from the head and ask our way along. That is the linked list, the next installment of this series.

## After the Move, Every Old Address Is Dead

One last accident, and the pit newbies step into hardest. Look at the code:

```cpp
std::vector<int> v;
v.reserve(2);
v.push_back(41);
v.push_back(1);

int* p = &v[0];   // cache a pointer
v.push_back(99);  // capacity full, triggers reallocation
*p = 100;         // write to the old address
```

Look at `p`: it points at the first cell of the old buffer. When the move happened, the old buffer had already been `delete`d, and from then on `p` points at a piece of memory that has been returned. The line `*p = 100` is undefined behavior: it may crash, it may look fine, or it may quietly write 100 into a cell the allocator has already re-let to somebody else, corrupting a completely unrelated object. Of those three endings the standard guarantees not a single one — and that is precisely what makes UB hateful: its favorite trick is to play innocent during testing and do damage after going live.

Let's catch this scene red-handed with AddressSanitizer (MSVC, `/fsanitize=address`):

```text
==18800==ERROR: AddressSanitizer: heap-use-after-free on address 0x123562da0010
WRITE of size 4 at 0x123562da0010 thread T0
```

`WRITE of size 4` — exactly the write width of one `int`, and the address is precisely that old-buffer cell. Without ASan, this program would most likely "run to completion" on a real machine, with the error lurking unseen beneath the surface. References and iterators fare the same. Whether we hold a pointer like `&v[0]`, the reference returned by `v.front()`, or the iterator currently walking in a loop — once the move-triggering operation passes, everything in our hands is invalidated.

Which operation invalidates what, and which does not (`reserve` within capacity invalidates nothing; `swap` does not invalidate a single one) — the complete invalidation rule table is in [`Vector Deep Dive`](../03-vector-deep-dive.md). Here let's first erect the causality "a move means every old address is dead"; that table then stops reading like items to be memorized by rote.

This accident also has an online copy prepared — run it once and it will stick:

<OnlineCompilerDemo
  title="Dangling Pointer After Reallocation: Address Comparison Before and After the Move"
  source-path="code/examples/vol3/primer_01_dangling_pointer.cpp"
  description="First run it as-is: in the before/after lines, p and v.data() go from identical to different — the old pointer is dangling from that point on, yet the program most likely runs to completion. Then add -fsanitize=address in the run options and run again: ASan will catch this out-of-bounds write on the spot."
  allow-run
/>

## Add RAII, Templates, and Exception Safety to the Embryo, and You Get std::vector

Looking back at what we built along the way: one contiguous block of memory on the heap, the two numbers `size` and `cap`, doubling and moving when full. That is the skeleton of `std::vector`; on top of it go three pieces of tooling: RAII lets the destructor return the memory automatically, walling shut the leak route; templates generalize the cells from `int` to any type; exception safety handles "what if it breaks halfway through the move," and whether an element's move constructor qualifies as `noexcept` determines whether relocation gets slowed down. Each of the three has its home in this project: for the full implementation-level view, read [`Deep Dive into std::vector: Three Pointers, Reallocation, and Iterator Invalidation`](../03-vector-deep-dive.md) in this volume; to hand-knead the embryo into a real container yourself, head to vol8's [mini STL in practice](../../../vol8-domains/data-structure/index.md) and start from a raw buffer, one step at a time.

The primer's next stop is the linked list: a structure that never moves house, where the price of finding an element is asking for directions along the way. Contiguous versus scattered, random access versus sequential access — this one trade-off frames most of the questions you face when choosing a data structure. See you in the next piece.

## References

- [cppreference: std::vector](https://en.cppreference.com/w/cpp/container/vector) — the authoritative source for complexity promises and iterator invalidation rules
- [`Vector Deep Dive`](../03-vector-deep-dive.md) in this volume — the three-pointer derivation, the math behind the three libraries' growth strategies, the complete invalidation table
- vol8 [`mini STL in Practice (Part 2): Vector — Growth and Relocation`](../../../vol8-domains/data-structure/02-vector-growth-and-relocation.md) — the hand-rolled implementation, with the complete care around relocating objects
