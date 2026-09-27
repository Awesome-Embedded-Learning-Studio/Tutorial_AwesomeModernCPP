---
title: "mini STL in Practice (Part 2): Vector — Growth and Relocation"
description: "Standing up a dynamic array on top of RawBuffer: two members, emplace_back's argument forwarding, doubling growth, and amortized analysis. The capacity trace comes from a real run; relocation adds zero new code — the foundation's Relocate is reused as-is."
chapter: 2
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - 容器
  - vector
  - 内存管理
difficulty: intermediate
platform: host
reading_time_minutes: 8
cpp_standard: [17, 20, 23]
prerequisites:
  - "mini STL in Practice (Part 1): RawBuffer — Capacity, Not Objects"
related:
  - "mini STL in Practice (Part 3): Vector — the Rule of Five, Exception Safety, and Concepts"
translation:
  source: documents/vol8-domains/data-structure/02-vector-growth-and-relocation.md
  source_hash: 82717191918134d1d719b34074fca365bcf78e385d483b14b8abf0fa1391f86e
  translated_at: '2026-09-25T08:50:58+00:00'
  engine: anthropic
  token_count: 1700
---

# mini STL in Practice (Part 2): Vector — Growth and Relocation

Let's stuff 40 values in a row into `tamcpp::ministl::Vector`, printing one line every time the capacity changes (companion example `example/vector_capacity_trace.cpp`):

```text
初始         size=0   capacity=0
第 1  个入库 size=1   capacity=4(扩容)
第 5  个入库 size=5   capacity=8(扩容)
第 9  个入库 size=9   capacity=16(扩容)
第 17 个入库 size=17  capacity=32(扩容)
第 33 个入库 size=33  capacity=64(扩容)
最终         size=40  capacity=64
```

This trace has been turned into an animation: you can play it, pause it, or single-step through it with the step key, and get a clear look at every single reallocation:

<Anim id="opp1-vector-growth" />

Forty insertions, only five reallocations, the capacity doubling all the way. In this article we'll explain exactly where that trace comes from — and hand-roll the whole dynamic array while we're at it. The previous article's `Relocate` is reused verbatim at the growth step; not one line changes.

## Members: One Pointer and a Count

```cpp
  private:
    RawBuffer<Sources> buffer_;
    std::size_t current_cnt_{0};
```

`buffer_` is that raw block of memory; `current_cnt_` counts the live elements. In memory, the first `current_cnt_` slots are live objects; everything behind them is space that has never been constructed. libstdc++'s `std::vector` draws the exact same picture, just with a different notation. It stores three pointers in `_Vector_impl_data` (`bits/stl_vector.h:98-102`): the start, one-past-the-end, and the bottom of the buffer. Want the size? Subtract the first two. Want the capacity? Subtract the last two. We measured the sizes ourselves — both versions match:

```text
sizeof(tamcpp::ministl::Vector<int>) = 24
sizeof(std::vector<int>)             = 24
```

## Birth and Death

There are three constructors. The default constructor does nothing: a `RawBuffer` is born as a null pointer plus zero capacity, which happens to be a perfectly legal empty array. The brace constructor simply walks the initializer list and appends the elements one by one. There is also a capacity constructor, an extension of our own project: `Vector<int> pre(8)` only allocates memory and constructs no objects. `std::vector` has no such constructor, but the library internals and the tests both make use of it.

```cpp
    Vector(std::initializer_list<Sources> init_lists_src) {
        const auto sz = init_lists_src.size();
        reserve(sz);
        for (const Sources& v : init_lists_src) {
            emplace_back(v); // In it goes! In it goes!
        }
    }
```

The destructor is a single line, yet it is the one line you can least afford to get wrong: `helper::DestroySources(buffer_.data(), buffer_.data() + current_cnt_)`.

Folks, when the author writes this by hand, it is genuinely error-prone — every single time, he is counting it off on his fingers. Destroy one slot too many and you are calling destructors on unconstructed memory; one too few and it is a leak. Returning the memory itself happens in the `RawBuffer` destructor — RAII catches that for us — while the container and algorithm layers only manage objects.

## emplace_back: Born in Place

```cpp
    template <typename... Args> Sources& emplace_back(Args&&... args) {
        if (current_cnt_ == buffer_.capacity()) {
            /* Why 4? Honestly an arbitrary pick — we haven't done any serious profiling here */
            grow_to(std::max<size_t>(4, buffer_.capacity() * 2));
        }

        Sources* p = std::construct_at(buffer_.data() + current_cnt_, std::forward<Args>(args)...);
        ++current_cnt_;
        return *p;
    }
```

Whatever constructor arguments you hand it, it forwards them as-is to `Sources`' constructor. The element is born directly at the tail of the array, without passing through any temporary object. The most intuitive example: `emplace_back(5, 'x')` on a `std::string` builds `"xxxxx"` directly. The forwarding works through `Args&&...` plus `std::forward`: an lvalue comes in and is passed along as an lvalue; an rvalue comes in and stays an rvalue — the chance to move never gets swallowed halfway.

For the construction point we use `std::construct_at`. It is C++20's official stand-in for placement new: it does the same thing underneath, plus it carries constexpr credentials. Both are worth knowing — Chromium's code is full of the original, while new code uses the stand-in.

When it's full, we grow. Our starting capacity is 4 — a number that has never seen a proper benchmark, a gut call; what actually carries weight is "double every time", and below we'll tally up that cost properly.

## grow_to and Amortized Analysis

```cpp
    void grow_to(std::size_t new_cap) {
        RawBuffer<Sources> new_buf_(new_cap);
        helper::Relocate(buffer_.data(), buffer_.data() + current_cnt_, new_buf_.data());
        buffer_ = std::move(new_buf_);
    }
```

Growth is just these few steps: allocate a new block of memory, move the live elements over, and hand the old buffer to the move assignment to release. The relocation is still the previous article's `Relocate`, not one line changed. Trivial types get moved wholesale with `memcpy`; everything else gets move-constructed over one by one. And who cleans up the elements left at the old location? Nobody has to — they were already destroyed one by one during the move, so no extra pass is needed afterwards. There is also no "copy to the new home first, then kill the old objects" double overhead anywhere in the flow — it is moves from beginning to end.

Most of the time, `push_back` merely constructs one object into one empty slot — constant time. But the moment the capacity fills up, that one call becomes a full relocation, at linear cost. So on what grounds do we still call `push_back` amortized constant? Add up the total cost and it becomes clear. Capacity doubles starting from 4, so the k-th reallocation moves 2^k elements; pushed all the way up to n elements, the total relocation cost is 4 + 8 + … + n. That is a standard geometric series, summing to less than 2n. Spread back over each push, it comes to fewer than two element moves per push — the capacity trace at the top, 40 insertions with only 5 reallocations, is exactly what this set of numbers looks like.

The amortized argument has one precondition: "the new capacity is a fixed multiple of the old" — file that away for now, because the ring-buffer article will come back to it. Doubling works; so does 1.5x — that is what MSVC uses. But adding just one slot each time does not: then n pushes cost 1+2+…+n relocations in total, which is quadratic. Chromium's `circular_deque` opts for growing by only 25%, with the comment citing steady queue load and memory saved by gradual growth (`circular_deque.h:1112-1114`) — the scenario there differs from an array, and that discussion waits for the ring-buffer article.

That is exactly the job `reserve(n)` is cut out for: when we know in advance how much we will hold, we allocate once, up front, with no relocations in between. It only grows, never shrinks — there is a dedicated test for this: after `r.reserve(10)`, a further `reserve(5)` still leaves the capacity at 10.

## Access, Iteration, and the Wrap-up

Access is where the benefits of contiguous storage show up first. `begin()` and `end()` return raw pointers, and a raw pointer over contiguous memory is a qualified iterator by birth: increment, compare, dereference — it does them all, and `std::sort` and range-based for can pick it up and run. When the linked-list article has us writing a whole iterator class for the same interface, come back and look at these few lines, and you'll see what they saved us.

For subscript access, we run a two-track design. `operator[]` performs no checks and trusts the caller; `visit_at` carries a `Check`, but the bound it checks is `current_cnt_` — however large the capacity is, it doesn't count. A death test keeps watch over exactly this bound: deliberately reserve(8), store only 3 elements, then go access slot 5 — an implementation that wrongly checks the bound against capacity cannot get past that gate.

The order inside `pop_back` matters: we decrement the count first, then destroy the last slot. Written the other way, you destroy the second-to-last slot while the actual last one keeps sitting there for nothing. `clear` destroys all live elements and keeps the capacity, consistent with `std::vector`: clearing destroys objects only — the memory stays.

## Acceptance Testing

Whether the relocation moved everything correctly, we check by subscript. Start from {1,2,3,4}, append 100 more — four reallocations along the way — then assert that `v[50]` must equal 46. Off by one slot, and this assertion exposes it on the spot.

Whether construction and destruction pair up, we bring in the counting type from Part 1 — the one that does "birth +1, death -1" — to keep watch. Add an element, remove an element, and the live-object count has to follow; clear everything, and it has to hit zero; keep using the container after clearing, and everything must still work. One destruction too many or too few, and the assertions won't pass. Finally we load in a batch of `std::string`s and confirm that non-trivial types come through the relocation with their contents intact.

```text
$ ./build/tests/test_vector
VECTOR ALL GREEN
```

At this point, our Vector can hold, can relocate, and can iterate — but it cannot yet copy or move itself: the copy constructor and copy assignment still hang under `DISABLE_COPY`. The next article completes the Rule of Five, and along the way runs the hands-on measurement of how a `noexcept` buys zero-copy moves.

## Build and Reproduce

```bash
cd code/volumn_codes/vol8-labs/ministl/stage1_rawbuf_vector
cmake -B build . && cmake --build build
(cd build && ctest --output-on-failure)
./build/example/vector_capacity_trace
```

## References

- Companion code: `code/volumn_codes/vol8-labs/ministl/stage1_rawbuf_vector/`
- libstdc++ `bits/stl_vector.h:98-102` (the three-pointer layout)
- [Deep Dive into std::vector: Three Pointers, Reallocation, and Iterator Invalidation](../../vol3-standard-library/containers/03-vector-deep-dive.md) (vol3 covers the concept level; this article is the implementation level)
- [cppreference: `std::vector` complexity](https://en.cppreference.com/w/cpp/container/vector)
