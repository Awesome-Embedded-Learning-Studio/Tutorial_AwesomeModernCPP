---
title: "mini STL in Practice (Part 1): RawBuffer — Capacity, Not Objects"
description: "The foundation of a hand-rolled container library: a crash-style check function, a helper header for destruction and relocation, and a block of raw memory that manages capacity but never object lifetimes. Modeled on Chromium's vector_buffer.h; the tests include not just regular assertions, but also a death case that must die on the spot."
chapter: 1
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - 容器
  - 内存管理
difficulty: intermediate
platform: host
reading_time_minutes: 12
cpp_standard: [17, 20, 23]
prerequisites:
  - "mini STL Introduction: Why Hand-Rolling Containers Is Worth It"
related:
  - "mini STL in Practice (Part 2): Vector — Growth and Relocation"
translation:
  source: documents/vol8-domains/data-structure/01-raw-buffer.md
  source_hash: 81047497f9242a425b4223900b72e5574f795d56050abc1a9847979d95042a38
  translated_at: '2026-09-25T08:48:33+00:00'
  engine: anthropic
  token_count: 6500
---

# mini STL in Practice (Part 1): RawBuffer — Capacity, Not Objects

The companion code for this article lives in `code/volumn_codes/vol8-labs/ministl/stage1_rawbuf_vector/` — four headers: two helpers, one utility, and the leading role. All of the code and test output can be reproduced verbatim on your machine; the build commands are at the end of the article.

`std::vector` sells "allocate memory" and "construct objects" as one bundle. Convenient — but what is sold bundled cannot be bought separately. The ring buffer and the hash table we will build later both need the freedom to "stake out a plot of memory first, and decide for ourselves when objects are born and when they die".

The good news: Chromium's engineers already had a go at the same need back in 2017, and the product was `base/containers/vector_buffer.h` — 179 lines, whose opening self-introduction reads: "Unlike std::vector, VectorBuffer never constructs or destructs its arguments". What we hand-roll in this article is a teaching version of it. (You ask why not a 1:1 copy — well, uh, we couldn't pull it off...)

## debug::Check: Crash-Style Checking

Before we start building, let's get the checking tooling ready. When this library sees an out-of-bounds access or a broken contract, the remedy is to crash on the spot:

```cpp
static inline void Check(bool IsExpectedTrue, const char* msg,
                         std::source_location loc = std::source_location::current()) {
    if (IsExpectedTrue) {
        return; // Ha, it holds? Nothing happens~
    }

    std::fprintf(stderr, "[TAMCPP Check Crash]%s-%d(%s): %s\n", loc.file_name(), loc.line(),
                 loc.function_name(), msg);
    std::abort();
}
```

This is learned from Chromium's `CHECK`: a browser kernel would rather crash than keep running on bad data. The philosophy: during development, never delay exposing a problem. This is also the answer the author once wrote in a discussion back in the Crash Lab series — don't put problems off while developing.

Here we implement it as a function: `std::source_location` (C++20) delivers the caller's file, line, and function name free of charge. In the macro era you had to stitch those together yourself with `__FILE__`/`__LINE__`; you no longer need to. It will genuinely earn its keep later: this article's acceptance run includes a death case that "must end in a Check Crash".

Alongside it sits a two-line `DISABLE_COPY` macro that deletes the copy constructor and copy assignment together, saving every class from hand-writing two `= delete` lines. The macro deliberately omits the trailing semicolon, forcing use sites into a complete statement that ends with one.

## DestroySources and Relocate: The Death and the Move of Objects

`memory_helper.hpp` holds only two functions, yet it is the code most worth reading line by line in the entire library. The first one "makes a range of objects die":

```cpp
template <typename Sources> inline void DestroySources(Sources* begin, Sources* end) {
    if constexpr (!std::is_trivially_destructible_v<Sources>) {
        for (Sources* index = begin; index < end; index++) {
            index->~Sources();
        }
    }
}
```

The syntax `index->~Sources()` is us explicitly invoking the destructor — the mirror operation of placement new. Placement new (written `new (p) Sources(...)`) "births" the object onto the given memory: construct only, no allocation; explicit destruction "takes the object away" from the memory, leaving the memory itself untouched.

One in, one out, used in pairs — this is what "memory and objects are two different things" looks like at the code level, and every container we build later relies on this pair of operations. The leading `if constexpr` prunes the branch at compile time: for trivially destructible types like `int`, the function body is empty — even the loop does not exist. Calling destructors one by one on trivial types was never necessary in the first place; semantically, this branch is meant to disappear.

The second one is the "move", in three tiers:

```cpp
template <typename Sources> inline void Relocate(Sources* from, Sources* from_end, Sources* to) {
    // We let the cleanly empty case go, because there's no need
    if (from == from_end) {
        return;
    }
    if constexpr (std::is_trivially_copyable_v<Sources>) {
        std::memcpy(to, from, sizeof(Sources) * (from_end - from));
    } else {
        for (Sources* p = from; p != from_end; ++p, ++to) {
            if constexpr (std::is_move_constructible_v<Sources>) {
                new (to) Sources(std::move(*p));
            } else {
                new (to) Sources(*p);
            }
            p->~Sources();
        }
    }
}
```

Trivially copyable types get moved wholesale with `memcpy`. Move-constructible types go one at a time: construct one at the new position, destroy one at the old position immediately after — strict alternation, no "finish moving everything first, then kill them all in one sweep". Types that cannot even move-construct have to fall back on an honest copy. The tests include a `CopyOnly` type with its move constructor deleted, dedicated to verifying that the third tier really exists.

That early return at the top blocks a hidden pit the author once stepped into. When an empty array grows for the first time, `from` and `from_end` are two null pointers and the `memcpy` length is 0 — it looks like "length zero means nothing was done". But the C standard requires both pointers passed to `memcpy` to be valid; passing null pointers is undefined behavior in its own right, independent of the length.

All three faces of UB can show up here: it may crash, it may not crash, and it may "succeed" at doing nothing — none of these outcomes is guaranteed, so don't expect it to argue reasonably. Pits like this — every test green while the defect hides in the dark — can only be caught by AddressSanitizer / UndefinedBehaviorSanitizer. That is why this library's CMake hangs both sanitizers on every target by default; when you want to run bare for performance, pass `-DTAMCPP_MINISTL_SANITIZE=OFF`.

## The RawBuffer Itself

Now the leading role takes the stage, and we give it exactly two members: a pointer and a capacity.

```cpp
template <typename Sources> struct RawBuffer {
    RawBuffer() = default;
    explicit RawBuffer(std::size_t capacity) : capacity_(capacity) {
        // Check the multiplication for overflow up front: wraparound would fool operator new into a smaller plot than asked, and everything after is out of bounds
        debug::Check(capacity <= std::numeric_limits<std::size_t>::max() / sizeof(Sources),
                     "capacity * sizeof(Sources) overflows");
        raw_buffer_begin_ =
            static_cast<Sources*>(::operator new(capacity * sizeof(Sources)));
    }
    // ...
```

The memory comes from `::operator new`. Why not the other two candidates? Let's take them one by one. `new Sources[n]` looks convenient, but it goes ahead and constructs all n objects for you — a direct conflict with the "capacity, not objects" positioning, so it is out. `malloc` is raw enough, but it only guarantees fundamental alignment; the day you put a custom-aligned type inside, it stops being enough. `::operator new` is just right: what lands in your hands is a block of memory with the right size, the right alignment, and undefined contents. Destruction symmetrically uses `::operator delete`, which only gives the memory back. Killing the objects off cleanly is the upper-layer container's duty, not this buffer's business. Chromium's side chose `malloc`, pairing the multiplication with an overflow check (`vector_buffer.h:50-54`). Our multiplication check is just as non-negotiable: the `Check` above has actually fired once — constructing with `SIZE_MAX / 4 + 1` `int`s earned this on the spot:

```text
[TAMCPP Check Crash]include/tamcpp_ministl/raw_buffer.hpp-32(...): capacity * sizeof(Sources) overflows
```

The move pair is in charge of handing ownership over cleanly: take the pointer and the capacity, null out the other side. The move assignment begins with a self-assignment check. Why check? When something like `MySelf = std::move(MySelf)` happens, your user will only come asking you why it blew up — they will never admit to having written that line. If the check trips, we return and act as if nothing happened.

Access runs on two tracks. `visit_at` carries a `Check`: out of bounds dies on the spot. `operator[]` does not check; it trusts the caller. This division of labor also follows Chromium: `vector_buffer.h`'s `operator[]` carries `CHECK_LT` (:79). The std side is exactly the opposite: `operator[]` never checks. We keep both, and the upper-layer containers use whichever they please.

## Acceptance

The tests we wrote are black-box: they look only at the header signatures and the behaviors promised in this article, never reading the implementation. The heart of `tests/test_raw_buffer.cpp` is a counting type. Every object constructed bumps a global counter up by one; every object destroyed brings it down by one. With it, verification is simple: move 5 objects over, and the count must still be 5. Drop one destruction along the relocation and the count reads 1 too high; destroy the same object twice and it reads 1 too low. Either way, the assertion fails on the spot. Each of the three relocation tiers gets its own group of cases like these. On top of that, which tier takes which path is pinned down at compile time with `static_assert` inside the tests, so a different compiler cannot quietly switch tiers. Running it:

```text
$ cmake --build build && ./build/tests/test_raw_buffer
RAW BUFFER ALL GREEN
```

The interesting one is another case. `tests/test_raw_buffer_bounds.cpp` has exactly one line of proper code: `buf.visit_at(4)` — capacity is exactly 4, accessing the 5th slot. Its correct ending is a crash; printing "got this far" would instead count as a failure. That poses a puzzle for the test framework. ctest rules any child process killed by a signal a failure, and it does not look at output regexes either. So a referee script, `expect_check_death.sh`, is wedged in the middle, and it verifies two things: the crash report is present, and the program has really died. A fake check that only prints the report and refuses to die itself cannot get past it — that layer of insurance is exactly what we want.

```text
$ ctest --output-on-failure
100% tests passed out of 4
```

All four cases done running: two black-box interface tests, two out-of-bounds death cases, each wrapped up as expected. The foundation is laid — in the next article we raise a Vector on top of it.

## Build and Reproduction

```bash
cd code/volumn_codes/vol8-labs/ministl/stage1_rawbuf_vector
cmake -B build . && cmake --build build
(cd build && ctest --output-on-failure)   # 4/4
./build/example/raw_buffer_smoke          # relocated[5] = 25 / SMOKE GREEN
```

## References

- Companion code: `code/volumn_codes/vol8-labs/ministl/stage1_rawbuf_vector/`
- Chromium `base/containers/vector_buffer.h` (this article's mirror, 179 lines)
- [cppreference: placement new](https://en.cppreference.com/w/cpp/language/new)
- [cppreference: `std::source_location`](https://en.cppreference.com/w/cpp/utility/source_location)
- [Custom Allocators & PMR: Managing Memory Yourself](../../vol3-standard-library/containers/13-custom-allocators.md) (vol3, the conceptual layer of memory management)
