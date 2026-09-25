---
title: "Fixed-Size Block Pool: An Allocator with One Bit per Block"
description: "The bitmap gets a real job: first we write the MemoryPool concept so the compiler enforces the pool contract (raw_allocate returns expected, try_allocate stays reserved for ISRs); then we build the two-level bitmap fixed-size block pool BitmapPool — L1 keeps one full-flag per L2 word, L2 keeps one bit per block, first-fit finds the free ones; the 0x67 poison check after free is our self-service use-after-free detection on bare metal, with ever_poisoned_ gating out false alarms from fresh .bss; wild pointers / interior pointers / double-free all map to NotOwned; acceptance = test_bitmap_pool, seven cases, 40295 assertions all passing (including a fixed-seed 20260904 fuzz of twenty thousand operations, where the stamp/~stamp twin values prove exclusive ownership)"
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
  - "A World Without a Heap · Part 2: Writing the Bitmap"
related:
  - "Writing the Bitmap: A Fixed-Capacity Bitmap"
translation:
  source: documents/vol8-domains/embedded/zeros/01-heapless-memory/03-block-pool.md
  source_hash: 0bca054968dbe345dbdf19cfa3b53897e2e69ae89b4e2f71f972446aa6e536ad
  translated_at: '2026-09-25T08:00:52+00:00'
  engine: anthropic
  token_count: 7500
---

# Fixed-Size Block Pool: An Allocator with One Bit per Block

With the bitmap in hand, this article turns the grid we sketched in the concepts article into something real: a fixed-size block pool that can hand out blocks, take them back, and verify the identity of whoever comes knocking to return one.

## Write the Contract First, Then the Implementation

In the first article we bragged a little: write the interface constraints in a compiler-checkable form, and if an implementation is missing even one function, the `static_assert` fails on the spot. Now it is time for that promise to take the stage. Create `include/ZerOS/kernel/mem/pool.hpp`:

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

You can take in the whole contract at a glance: it can hand out a block (`raw_allocate` returns `expected<void*, error code>`), take one back (`raw_deallocate` returns an error code), plus a fast path reserved for interrupts (`try_allocate` returns a raw pointer directly, and failure is simply `nullptr`—inside an ISR there is no time to unwrap an `expected`).

The error codes form a small taxonomy: `OutOfMemory` means the pool is genuinely out of slots, `Poisoned` means someone was caught writing to a freed block, and `NotOwned` means the pointer you brought back simply does not belong to this pool—wild pointers, interior pointers, and double-frees all land here. Why `expected` for the error channel instead of `optional`? The comments in the pool below give two reasons: with most implementations, `optional` costs 8 extra bytes; and more importantly, the error classification should not be thrown away at the user-interface layer.

## The Star: A Two-Level Bitmap Fixed-Size Block Pool

Create `include/ZerOS/kernel/mem/bitmap_allocate.hpp`:

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

Let's walk through the structure.

**Two-level bitmap**. `bitmap_l2_` keeps one bit per block recording occupancy; `bitmap_l1_` keeps one bit per L2 word recording "is this word full?". The path to a free block is two steps: `find_first_zero` in L1 locates the first word that is not full, then `first_zero_in_word` lands on the exact bit inside that word—the "skip a word, then land the bit" two-stage move from the previous article's bitmap, reporting for duty unchanged. With few blocks you cannot see the payoff; once the block count grows, skipping whole words at a time crushes the search down to constant order. And this should already look familiar: the priority-ready bitmap of commercial RTOSes finds the "highest-priority ready task" exactly this way, so that "allocators, schedulers" line in the header comment was not written for decoration—this structure will make a second appearance when the journey reaches the scheduler.

**Poison detection**. When `owns_poison_policy` is on, a block gets filled edge to edge with `0x67` the moment it is returned, and `ever_poisoned_` marks its bit; before this block is handed out again, we first check whether it is still a solid block of 0x67—if not, someone wrote to freed memory, and `Poisoned` comes back immediately. This is the capability that the concepts article's "separate the ledger from the inventory" buys us: a free list hides its pointers inside the bellies of the blocks, so the contents are naturally dirty and there is nothing coherent left to verify; a bitmap's freed blocks come back clean—whatever was poured in is what you find, and one lookup tells you who laid a finger on it. There is no ASan on the board, so this is our do-it-ourselves use-after-free detection. The `ever_poisoned_` gate exists so the innocent are not framed: a fresh `.bss` block that has never been poisoned is all zeros to begin with and should never be read as "the poison was tampered with".

**Vetting the pointer that comes back**. `index_of_given_ptr` first checks whether the pointer lies inside the pool's territory and whether it is block-aligned; then `release_block` checks whether that block is actually occupied: wild pointers, interior pointers aimed at the middle of a block, pointers from someone else's pool, double-frees—all of them get `NotOwned`. Note the `p < buffer_` line here: ordering two pointers into unrelated objects is, strictly speaking, unspecified behavior; but this is host-side code, everybody writes it this way in practice, and if you insist on being strict it can be rewritten as an integer comparison—over in the startup code we hold the line on "cast to integer, then compare". With the two sites side by side, weigh it yourself.

**Compile-time constraints on the geometry**. We require the block size to be a multiple of `max_align_t`; otherwise misalignment starts from the second block on. `buffer_` wears `ALL_ALIGNED`, and the `#define` is `#undef`ed the moment it has done its job—macro hygiene. The line at the file's tail, `static_assert(MemoryPool<BitmapPool<64, 8, true>>)`, is the pool proving itself to the concept: leave one interface unimplemented, and this line stops the build.

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

Two of these cases are worth slowing down for as you write them.

When you write the 100-block case, the comment deserves a line-by-line read: it records three real historical bugs—the l2 bitmap mistakenly using the word count as its bit count, l1 mistakenly using `L1_SIZE` as its bit width, and release clearing l1 only when the word became **empty**. That last bug's consequence: once an l1 bit was set it froze forever, and the whole word could never be found again. The case's move is to occupy all 100 blocks, dig exactly one hole, and then demand that this hole must be found again: if the hole cannot be found, the only explanation is that the road from l1 down to l2 is broken.

The fuzz case is this article's ballast, and it deserves five extra minutes of your time. The seed is pinned to `20260904`: a failure must reproduce bit for bit, on any machine. The 55/45 allocation bias makes the pool genuinely saturate and then drain, instead of receiving a couple of painless pats; every live block gets two values written into it, `stamp` and `~stamp`, verified right before the free: if the stamp is broken, then two owners—or one wild write—touched this block. Across twenty thousand operations, if any invariant collapses, the whole thing derails on the spot.

## Acceptance

```shell
cmake --build build-host
./build-host/test/test_bitmap_pool
```

The real output on my machine:

```text
All tests passed (40295 assertions in 7 test cases)
```

Forty thousand assertions, the bulk of them inside the fuzz, and the number stays the same however many times you run it—the seed is fixed. All green means pass.

The pool can hand out `void*` now, but kernel objects want types. In the next article we write the final layer of facade: `Make<T>(pool, ...` lets objects be born inside the pool, `Destroy` gives them a decent burial, plus two compile-time lines of defense and a negative test dedicated to proving that "the defenses exist".
