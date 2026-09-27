---
title: "Fixed-Size Block Pool: An Allocator with One Bit per Block"
description: "The bitmap gets a full-time job: first we write the MemoryPool concept and hand the pool contract to the compiler (raw_allocate returns expected, try_allocate stays reserved for ISRs); then we write the two-level bitmap fixed-size block pool BitmapPool — L1 keeps one full-flag per L2 word, L2 keeps one bit per block, first-fit finds the free ones; the 0x67 poison check after free is our self-service use-after-free detection on bare metal, with the ever_poisoned_ gate fending off false alarms from fresh .bss blocks; wild pointers, interior pointers, and double-frees all map to NotOwned; acceptance = test_bitmap_pool, seven cases and 40295 assertions all passing (including a fixed-seed 20260904 fuzz of twenty thousand operations, with the stamp/~stamp twin values proving exclusive ownership)"
chapter: 1
order: 3
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 内存管理
  - expected
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 25
prerequisites:
  - "Writing the Bitmap: A Fixed-Capacity Bitmap"
related:
  - "Writing the Bitmap: A Fixed-Capacity Bitmap"
translation:
  source: documents/vol8-domains/embedded/zeros/01-heapless-memory/03-block-pool.md
  source_hash: 82b49ea064d17d411996c12b6831ae0f8cbbb509189ea4d54237316abc0fea62
  translated_at: '2026-09-27T06:00:31+00:00'
  engine: anthropic
  token_count: 8000
---

# Fixed-Size Block Pool: An Allocator with One Bit per Block

With the bitmap in hand, this article turns the grid we sketched in the concepts article into something real: a fixed-size block pool that can hand out blocks, take them back, and check the credentials of whoever comes to return one.

## Write the Contract First, Then the Implementation

In the first article we bragged: write the interface constraints in a compiler-checkable form, and if an implementation is missing so much as one function, the `static_assert` fails on the spot. Now it is that boast's turn on stage. Create `include/ZerOS/kernel/mem/pool.hpp`:

```cpp
#pragma once
#include <concepts>
#include <expected>

namespace ZerOS::memory
{
    enum class MemoryAllocationError {
        Ok, OutOfMemory, Poisoned, NotOwned
    };

    template <typename Pool>
    concept MemoryPool = requires (Pool p, void* block) {
        // A Memory pool should be able to allocate and deallocate blocks
        {p.raw_allocate()} -> std::same_as<std::expected<void*, MemoryAllocationError>>;
        // And also can deallocate target
        {p.raw_deallocate(block)} -> std::same_as<MemoryAllocationError>;

        // for ISR Allocations, we should use try allocate
        {p.try_allocate()} -> std::same_as<void*>;
    };
}
```

A concept really is a contract. We declare that a memory pool must, at the very least, have the following properties:

1. raw_allocate — it can hand out a memory block; we deliberately do not constrain the size
2. raw_deallocate — what is borrowed must be returned!
3. try_allocate — a tentative allocation; ISR-style code does not dare the satisfying, full-throated unwrap of an expected.

Ah, and a quick word on the error codes:

- `OutOfMemory`: there are genuinely no slots left
- `Poisoned`: someone was caught writing to an already-freed block
- `NotOwned`: the pointer you brought back simply does not belong to this pool — wild pointers, interior pointers, and double-frees are all its jurisdiction.

Why does the error channel use `expected` instead of `optional`? The comments in the pool below hand us two reasons: with most implementations, `optional` costs 8 extra bytes; and more importantly, the error classification should not get dropped at the user-interface layer.

## The Star: A Two-Level Bitmap Fixed-Size Block Pool

Create `include/ZerOS/kernel/mem/bitmap_allocate.hpp`. Pasting the whole file at once would be too long, so we walk it from top to bottom and explain each stretch of code right where it stands:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>

#include "ZerOS/base/bitmap.hpp"
#include "ZerOS/kernel/mem/pool.hpp"

namespace ZerOS::memory {

#define ALL_ALIGNED alignas(alignof(std::max_align_t))

/**
 * @brief   This is a bitmap pool, allocating the stuff of buffer_
 *          Take a breathe, we use bitmap, which, we use a bit to shoow if we use a block
 *          Mentioned: One Block, One Stuff
 *
 * @tparam block_size
 * @tparam block_cnt
 * @tparam owns_poison_policy
 */
template <std::size_t block_size, std::size_t block_cnt, bool owns_poison_policy>
struct BitmapPool {
    static constexpr std::size_t BUFFER_SIZE = block_size * block_cnt;
    static constexpr std::size_t BITMAP_L2_SIZE = (block_cnt + 31) / 32;
    static constexpr std::size_t BLOCK_SIZE = block_size;
    // buffer_ is max-aligned (ALL_ALIGNED), so every block start is too;
    // Make<> static_asserts objects against this (see typeable.hpp).
    static constexpr std::size_t BLOCK_ALIGN = alignof(std::max_align_t);

    static_assert(BLOCK_SIZE % alignof(std::max_align_t) == 0,
                  "block size must keep every block max-aligned");
    // Why not optional
    // A. if we use optional, for most impls, it costs 8 bytes
    // B. for User Interfaces, we should never carry it

    constexpr BitmapPool() = default;
```

The `static_assert` in the constants section hands the geometric constraint to the compiler: the block size must be a multiple of `max_align_t`, otherwise misalignment starts from the second block on. `BLOCK_ALIGN` stores that alignment requirement as a constant; the `typeable.hpp` the comment points to is where the next article's `Make<>` validates objects against it. The `ALL_ALIGNED` macro is defined here too; the `buffer_` that uses it waits at the tail of the file, and we will meet it when we reach the member section.

```cpp
    // ------------------------------------------------------------------
    // Contract surface: these three together satisfy concept MemoryPool
    // ------------------------------------------------------------------
    std::expected<void*, MemoryAllocationError> raw_allocate() {
        const auto index = available_block_index();
        if (!IsAvailableIndex(index)) {
            return std::unexpected(MemoryAllocationError::OutOfMemory);
        }

        if constexpr (owns_poison_policy) {
            // Only blocks that HAVE been poisoned (freed once) can fail the check;
            // fresh .bss blocks are all-zero, which is not poison tampering.
            if (ever_poisoned_.test(index) && detected_poison(index)) {
                return std::unexpected(MemoryAllocationError::Poisoned);
            }
        }

        set_as_in_used(index);
        return fetch_target_block(index);
    }
    MemoryAllocationError raw_deallocate(void* block) {
        const auto index = index_of_given_ptr(block);
        if (!IsAvailableIndex(index)) {
            return MemoryAllocationError::NotOwned; // wild / interior / foreign pointer
        }

        if (!release_block(index)) {
            return MemoryAllocationError::NotOwned; // double free
        }

        poison_block(index); // no-op when poison policy is off
        return MemoryAllocationError::Ok;
    }

    // For ISR allocations: no expected, failure simply reported as nullptr
    void* try_allocate() {
        auto res = raw_allocate();
        return res ? *res : nullptr;
    }
```

The public section holds exactly these three functions — precisely the trio the concept above names. The backbone of `raw_allocate`: find a free block, check the poison, mark it taken, hand out the pointer; `raw_deallocate` walks the other direction: check the pointer's identity, take the block back, poison it. You do not need to fully follow the poison check inside the `if constexpr (owns_poison_policy)` stretch just yet — the `poison_block` section below is its home turf; the criteria behind the two `NotOwned` exits likewise live in the private functions below, and we will get to them one by one. `try_allocate` is for ISRs: an interrupt handler does not dare unwrap an `expected`, so failure folds into `nullptr` — a one-line forwarding job, done.

```cpp
  private:
    // we fetch the first available block, if not, return the
    // npos
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    static constexpr bool IsAvailableIndex(std::size_t index) { return index != npos; }

    // bitmap using here, for 0, it is available, for 1, it is full
    std::size_t available_block_index() // fetch the available block recorded in the pool
    {
        // Boost the speed by using the l1 bitmap, fastly, we find the first zero in the l1 bitmap
        const auto word_index = bitmap_l1_.find_first_zero();
        if (word_index == decltype(bitmap_l1_)::npos) {
            return npos;
        }

        // OK, this is the case, find in this word
        return bitmap_l2_.first_zero_in_word(word_index);
    }
```

The path to a free block is two steps: `bitmap_l1_.find_first_zero()` locates the first L2 word that is not full; `first_zero_in_word` then enters that word and lands on the exact bit. The two bitmaps each govern one level: `bitmap_l2_` keeps one bit per block, recording occupancy; `bitmap_l1_` keeps one bit per word, recording "is this word full yet". The skip-a-word-then-land two-stage scheme we wrote in the last article reports for duty unchanged. When nothing is found we return `npos`, a sentinel that runs through this whole file; `raw_allocate` tests it with `IsAvailableIndex`, and that is where `OutOfMemory` comes from.

With few blocks you cannot see the payoff; only when the block count grows does the benefit come out: skipping whole words at a time presses the search down to constant order. This structure should already look familiar to you — a commercial RTOS's priority-ready bitmap finds the "highest-priority ready task" exactly this way; the "allocators, schedulers" written in the bitmap article's header comment was not written for decoration either, and the structure will take the stage once more when the journey reaches the scheduler.

```cpp
    void set_as_in_used(std::size_t index) {
        bitmap_l2_.set(index);

        if (bitmap_l2_.word_full(index >> 5)) {
            bitmap_l1_.set(index >> 5); // ok, this is also full
        }

        used_++;
    }

    bool release_block(std::size_t index) // The index acquired by available_block_index
    {
        if (index >= block_cnt) {
            return false;
        }

        if (!bitmap_l2_.test(index)) {
            // you cant release a block that is not in use
            return false;
        }

        bitmap_l2_.clear(index);
        // l1 is the "word full" flag: freeing ANY block makes its word not-full.
        // Unconditional clear is idempotent and keeps the invariant honest.
        bitmap_l1_.clear(index >> 5);

        used_--;
        return true;
    }
```

To see how the occupancy bits are maintained, watch this pair of functions. After setting the L2 bit, `set_as_in_used` takes one extra look: is this word full now (`word_full`)? If it is, light the corresponding L1 bit too; `release_block` goes the other way — after clearing L2 it clears L1 unconditionally. The comment says it plainly: once ANY block is freed, the word is back to "not full"; the unconditional clear is idempotent, and only that keeps the invariant honest. The double-free line of defense also lives in this function: if `bitmap_l2_.test(index)` does not pass, the block was never occupied in the first place, `false` travels back up, and the caller translates it into `NotOwned`.

```cpp
    constexpr void* fetch_target_block(std::size_t index) { return buffer_ + index * BLOCK_SIZE; }
    std::size_t index_of_given_ptr(void* ptr) {
        auto* p = static_cast<std::byte*>(ptr);
        if (p < buffer_ || p >= buffer_ + BUFFER_SIZE) {
            // Not in this scpoe
            return npos;
        }

        const auto off = static_cast<std::size_t>(p - buffer_);
        if (off % BLOCK_SIZE != 0) {
            // not aligned, All the target allocated should be aligned
            return npos;
        }

        return off / BLOCK_SIZE;
    }
```

The eligibility vetting of a returned pointer is exactly the two checks in `index_of_given_ptr`: whether the pointer is inside the pool's territory, and if so, whether the offset is block-aligned. Wild pointers, interior pointers aimed at the middle of a block, pointers from someone else's pool — none of them pass these two gates; together with the double-free intercepted by `release_block` above, they all come back `NotOwned`. Note the `p < buffer_` line: ordering two pointers into unrelated objects is, strictly speaking, unspecified behavior; but this is host-side code, everybody writes it this way in practice, and if you insist on being rigorous it can be rewritten as an integer comparison — over in the startup code we hold the line on "cast to integer, then compare". With the two sites side by side, weigh it yourself.

```cpp
    // poisoned the target block
    static constexpr std::byte POISON_VALUE{0x67};
    void poison_block(std::size_t index) {
        if constexpr (owns_poison_policy) {
            auto* p = fetch_target_block(index);
            memset(p, std::to_integer<int>(POISON_VALUE), BLOCK_SIZE);
            ever_poisoned_.set(index); // from now on, this block is expected to stay poisoned
        }
    }
    bool detected_poison(std::size_t index) {
        if constexpr (!owns_poison_policy) {
            return false;
        }
        auto* p = static_cast<std::byte*>(fetch_target_block(index));
        for (std::size_t i = 0; i < BLOCK_SIZE; ++i) {
            if (p[i] != POISON_VALUE) {
                return true; // One write the session!
            }
        }
        return false;
    }
```

That `if constexpr` stretch inside `raw_allocate` above is implemented by this pair of functions, and what they do is not complicated. When `owns_poison_policy` is on, the moment a block comes back, `poison_block` fills the whole block with `0x67`, and in passing sets the block's bit in `ever_poisoned_`. What that bit is for, we will see in a moment.

Before that block is handed out again, `detected_poison` runs a prior check: is the whole block still 0x67? If even one position does not match, someone wrote to freed memory; `Poisoned` bounces it back, and this block is no longer issued to you.

Why can a bitmap pool do this? A free list cannot: the linked list has to write its next pointer into the free block itself, so the block's contents are inherently dirty — even if you wanted to verify, there would be nothing to verify against. The bitmap records occupancy outside the blocks; a freed block comes back clean, whatever was filled in is what is there, and one inspection tells you someone touched it. There is no ASan on the board either, so this poison scheme is our self-service use-after-free detection.

That `ever_poisoned_` gate exists so the innocent do not get framed. Think it through: a fresh `.bss` block that has never been poisoned is all zeros to begin with, and all zeros of course is not a solid block of 0x67 — without a gate, would it not be a false alarm on every single block? So only blocks that have been poisoned go through this check.

```cpp
    // Buffer Locations here, as it request all baasic
    ALL_ALIGNED std::byte buffer_[BUFFER_SIZE];
    base::Bitmap<block_cnt> bitmap_l2_;      // one bit per block: 1 = occupied
    base::Bitmap<BITMAP_L2_SIZE> bitmap_l1_; // one bit per l2 word: 1 = that word is full
    base::Bitmap<block_cnt> ever_poisoned_;  // 1 = block went through a poison-on-free cycle
    std::size_t used_ = 0; // block we have been used
};

#undef ALL_ALIGNED // OK, dont leek this out

// we should ensure that, BitmapPool is A Memory Pool
static_assert(MemoryPool<BitmapPool<64, 8, true>>);

} // namespace ZerOS::memory
```

The member section closes the file, and every name is one we have met: `buffer_` wears the `ALL_ALIGNED` from the top, so the whole buffer is aligned to `max_align_t`; combined with the block-size `static_assert` from the beginning, the starting address of every block keeps its alignment. The three bitmap members each mind one thing: L2 records occupancy, L1 records word-fullness, `ever_poisoned_` records poison history, and `used_` counts the blocks in use. The `#define` gets `#undef`-ed as soon as it has done its job — macro hygiene, nothing leaks out. The line at the tail, `static_assert(MemoryPool<BitmapPool<64, 8, true>>)`, deserves a dedicated look: the pool proves itself to the concept, and if one interface goes missing, this line stops the build.

## Throw Twenty Thousand Operations at It

Add one line to the list in `test/CMakeLists.txt`:

```cmake
zeros_add_test(test_bitmap)
zeros_add_test(test_bitmap_pool)
```

Then `test/test_bitmap_pool.cpp`, seven test cases, the full file:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "ZerOS/kernel/mem/bitmap_allocate.hpp"

using ZerOS::memory::BitmapPool;
using ZerOS::memory::MemoryAllocationError;

TEST_CASE("distinct blocks, first-fit reuse, double free", "[pool]") {
    BitmapPool<64, 8, true> pool;

    auto a = pool.raw_allocate();
    auto b = pool.raw_allocate();
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(*a != *b); // different callers must never share a block

    REQUIRE(pool.raw_deallocate(*a) == MemoryAllocationError::Ok);
    auto c = pool.raw_allocate();
    REQUIRE(c.has_value());
    CHECK(*c == *a); // lowest freed index comes back first

    pool.raw_deallocate(*c);
    CHECK(pool.raw_deallocate(*c) == MemoryAllocationError::NotOwned);
}

TEST_CASE("wild and interior pointers are NotOwned", "[pool]") {
    BitmapPool<64, 8, true> pool;

    int dummy = 0; // a stack object living outside the pool
    CHECK(pool.raw_deallocate(&dummy) == MemoryAllocationError::NotOwned);

    auto taken = pool.raw_allocate();
    REQUIRE(taken.has_value());
    auto* interior = static_cast<std::byte*>(*taken) + 8;
    CHECK(pool.raw_deallocate(interior) == MemoryAllocationError::NotOwned);
}

TEST_CASE("100 blocks spanning 4 l2 words keep l1/l2 honest", "[pool][l1l2]") {
    // Regression for the classic trio: l2 typed with word-count bits,
    // l1 typed with L1_SIZE bits, release only clearing l1 when its
    // word became EMPTY (the old bug froze l1 bits set forever).
    BitmapPool<16, 100, false> pool;
    std::vector<void*> ps;

    for (std::size_t i = 0; i < 100; ++i) {
        auto r = pool.raw_allocate();
        REQUIRE(r.has_value());
        ps.push_back(*r);
    }
    CHECK_FALSE(pool.raw_allocate().has_value()); // exhausted

    pool.raw_deallocate(ps[33]); // the ONLY hole in the whole pool
    auto q = pool.raw_allocate();
    REQUIRE(q.has_value());
    CHECK(*q == ps[33]); // the hole must be found again through l1 -> l2
}

TEST_CASE("poison catches write-after-free", "[pool][poison]") {
    BitmapPool<64, 4, true> pool;

    auto v = pool.raw_allocate();
    REQUIRE(v.has_value());
    auto* p = static_cast<unsigned char*>(*v);
    pool.raw_deallocate(p); // poison-on-free fills the block

    p[3] ^= 0xFF;           // sneak write into a free block
    auto r = pool.raw_allocate();
    CHECK_FALSE(r.has_value());
    CHECK(r.error() == MemoryAllocationError::Poisoned);
}

TEST_CASE("a clean free reallocates without false poison", "[pool][poison]") {
    BitmapPool<64, 4, true> pool;

    auto v = pool.raw_allocate();
    REQUIRE(v.has_value());
    pool.raw_deallocate(*v); // nobody touched it since the free

    auto r = pool.raw_allocate();
    REQUIRE(r.has_value()); // ever_poisoned_ gates the check, must not misfire
    CHECK(*r == *v);
}

TEST_CASE("try_allocate reports exhaustion as nullptr", "[pool]") {
    BitmapPool<64, 2, false> pool;

    CHECK(pool.try_allocate() != nullptr);
    CHECK(pool.try_allocate() != nullptr);
    CHECK(pool.try_allocate() == nullptr); // ISR-safe surface, no expected<>
}

TEST_CASE("randomized torture: interleaved alloc/free keeps invariants", "[pool][fuzz]") {
    // Fixed seed: a failure must reproduce bit-for-bit on every machine.
    std::mt19937 rng{20260904u};

    constexpr std::size_t kBlocks = 64;
    BitmapPool<16, kBlocks, true> pool; // poison ON: exercises ever_poisoned_ gating too

    struct Live {
        void* p;
        std::uint64_t stamp;
    };
    std::vector<Live> live;
    live.reserve(kBlocks);

    std::size_t allocs = 0, frees = 0;
    constexpr std::size_t kOps = 20000;

    for (std::size_t op = 0; op < kOps; ++op) {
        // 55/45 bias towards alloc so the pool really saturates and drains;
        // forced alloc when empty keeps `live` indices well-defined.
        const bool want_alloc = live.empty() || (rng() % 100u) < 55u;

        if (want_alloc) {
            auto r = pool.raw_allocate();
            if (live.size() == kBlocks) {
                // holding every block => the pool must report exhaustion
                REQUIRE_FALSE(r.has_value());
                REQUIRE(r.error() == MemoryAllocationError::OutOfMemory);
                continue;
            }
            REQUIRE(r.has_value());

            // stamp the block: if the stamp is ever broken, two owners
            // (or a wild write) touched this block between alloc and free.
            const auto stamp = (static_cast<std::uint64_t>(op) << 32) ^ allocs;
            auto* w = static_cast<std::uint64_t*>(*r); // 16B blocks, max-aligned
            w[0] = stamp;
            w[1] = ~stamp;

            live.push_back({*r, stamp});
            ++allocs;
        } else {
            const auto idx = rng() % live.size();
            auto* w = static_cast<std::uint64_t*>(live[idx].p);
            REQUIRE(w[0] == live[idx].stamp); // still exclusively ours?
            REQUIRE(w[1] == ~live[idx].stamp);

            REQUIRE(pool.raw_deallocate(live[idx].p) == MemoryAllocationError::Ok);
            live.erase(live.begin() + static_cast<std::ptrdiff_t>(idx));
            ++frees;
        }
    }

    // drain: every survivor must still carry an intact stamp and free cleanly
    for (const auto& b : live) {
        auto* w = static_cast<std::uint64_t*>(b.p);
        REQUIRE(w[0] == b.stamp);
        REQUIRE(w[1] == ~b.stamp);
        REQUIRE(pool.raw_deallocate(b.p) == MemoryAllocationError::Ok);
    }
    live.clear();

    // a fully-drained pool must hand out all blocks again, then report full
    std::vector<void*> refill;
    for (std::size_t i = 0; i < kBlocks; ++i) {
        auto r = pool.raw_allocate();
        REQUIRE(r.has_value());
        refill.push_back(*r);
    }
    REQUIRE_FALSE(pool.raw_allocate().has_value());

    CHECK(allocs > 100); // guard against an accidentally idle test
    CHECK(frees > 100);
}
```

Two of the cases are worth slowing down for as you write them.

When you write the 100-block case, the comment deserves a line-by-line read: it records three real historical bugs — the l2 bitmap mistakenly using the word count as its bit count, l1 mistakenly using `L1_SIZE` as its bit width, and release clearing l1 only when the word became **empty**. The consequence of that last bug: once an l1 bit was set it froze forever, and the whole word could never be found again. The case's move is to occupy all 100 blocks, dig exactly one hole, and then demand that this hole must be found: if the hole cannot be found, the only explanation is that the road from l1 to l2 is broken.

The fuzz case is this article's ballast, and it deserves five extra minutes of your time. The seed is pinned to `20260904`: a failure must reproduce bit for bit, on any machine; the 55/45 allocation bias makes the pool genuinely fill up and then drain, instead of receiving a couple of painless pats; every live block gets two values written into it, `stamp` and `~stamp`, verified right before the free: if the stamp is broken, two owners or one wild write touched this block. Across twenty thousand operations, if any invariant collapses, the whole thing derails on the spot.

## Acceptance

```shell
cmake --build build-host
./build-host/test/test_bitmap_pool
```

The real output on my machine:

```text
All tests passed (40295 assertions in 7 test cases)
```

Forty-thousand-odd assertions, the bulk of them inside the fuzz, and the number stays the same however many times you run it — the seed is dead. All passing means we pass.

The pool can hand out `void*` now, but kernel objects want types. In the next article we write the final layer of facade: `Make<T>(pool, ...` lets an object be born inside the pool, `Destroy` gives it a decent burial, plus two compile-time lines of defense and a negative test dedicated to proving that the defenses exist.
