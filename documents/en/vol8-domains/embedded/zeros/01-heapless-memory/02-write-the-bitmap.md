---
title: "Writing the Bitmap: A Fixed-Capacity Bitmap"
description: "This article walks the path the concept piece thought through: create include/ZerOS/base/bitmap.hpp, a fixed-capacity template with bit-level set/clear/test plus word-level word() access (the layer std::bitset refuses to give); dual-track ctz (__builtin_ctz lowers to RBIT+CLZ on Cortex-M3/M4, with a pure-software fallback); tail_mask as the anchor of every tail-word check; four static_asserts of immediately-invoked lambdas at the end of the file, so every compile is a regression run. The test infrastructure rises in step: a ZEROS_BUILD_TESTS mutually-exclusive switch in the root CMakeLists, per-language flag injection via .clangd, Catch2 v3.7.1 arriving through FetchContent; acceptance = test_bitmap with six cases and 158 assertions all passing (real output)."
chapter: 1
order: 2
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 内存管理
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 20
prerequisites:
  - "A World Without a Heap · Part 1: How Memory Gets Handed Out, and What a Bitmap Is"
related:
  - "A World Without a Heap: How Memory Gets Handed Out, and What a Bitmap Is"
translation:
  source: documents/vol8-domains/embedded/zeros/01-heapless-memory/02-write-the-bitmap.md
  source_hash: 590313db31ef6dd8bb4912ba9bea50e56eb225a8834e7de19c00f1030b0bb9ae
  translated_at: '2026-09-25T07:55:43+00:00'
  engine: anthropic
  token_count: 2300
---

# Writing the Bitmap: A Fixed-Capacity Bitmap

Last time we turned the bitmap over in our hands: one bit records one block, and finding a free block is just finding the first 0. This time we write it as real code, and while we're at it we stand up the test bench — from this article on, everything we write gets tests waiting on it, and a part like the bitmap, which is going to accompany us all the way to the end, all the more needs insurance from day one.

We create a new file, `include/ZerOS/base/bitmap.hpp`:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

namespace ZerOS::base {

namespace zeros_impl {
static constexpr std::size_t _ctz(std::uint32_t x) {
    std::size_t n = 0;
    while ((x & 1u) == 0) {
        x >>= 1;
        ++n;
    }
    return n;
}
} // namespace zeros_impl

/// Index of the lowest set bit; x must be non-zero.
/// GCC/Clang lower this to RBIT+CLZ on Cortex-M3/M4.
constexpr std::size_t ctz(std::uint32_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return static_cast<std::size_t>(__builtin_ctz(x));
#else
    return zeros_impl::_ctz(x);
#endif
} // ctz

/**
 * @brief  A bare-word bitmap for kernel bookkeeping (allocators, schedulers).
 *
 *         Contracts (break them and the helpers will lie to you):
 *         - Padding bits (>= bit_count in the tail word) must stay 0.
 *           word() hands out raw access on purpose: keeping the tail
 *           padding clean while writing whole words is on the caller.
 *         - set()/clear() are read-modify-write, NOT atomic. Guard them
 *           with a critical section / exclusive access when ISRs share the map.
 *         - Out-of-range bit/word indexes are UB.
 */
template <std::size_t bit_count> struct Bitmap {
    static_assert(bit_count > 0, "ZerOS::base::Bitmap needs at least one bit");

    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    static constexpr std::size_t WORDS = (bit_count + 31) >> 5;

    // all-zero -> lands in .bss, zero flash cost, constinit friendly
    constexpr Bitmap() = default;
    constexpr Bitmap(Bitmap&&) noexcept = default;
    constexpr Bitmap& operator=(Bitmap&&) noexcept = default;

    // —— bit level ——
    constexpr void set(std::size_t i) { words_[i >> 5] |= one_hot(i); }
    constexpr void clear(std::size_t i) { words_[i >> 5] &= ~one_hot(i); }
    [[nodiscard]] constexpr bool test(std::size_t i) const {
        return (words_[i >> 5] & one_hot(i)) != 0;
    }

    // —— word level (what std::bitset refuses to give us) ——
    /// Raw access to the w-th 32-bit plane: bulk set/clear, L1 summaries,
    /// word-wide atomics. Keep the tail padding zero!
    constexpr std::uint32_t& word(std::size_t w) { return words_[w]; }
    [[nodiscard]] constexpr std::uint32_t word(std::size_t w) const { return words_[w]; }

    /// All real bits occupied? Tail word compares against tail_mask(), NOT 0xFFFFFFFF!
    [[nodiscard]] constexpr bool word_full(std::size_t w) const {
        return words_[w] == valid_mask(w);
    }

    /// All 32 slots free? Safe for the tail word too, padding stays 0.
    [[nodiscard]] constexpr bool word_empty(std::size_t w) const { return words_[w] == 0; }

    // —— CLZ lookup ——
    /// First clear bit inside the w-th word, npos if that word is full.
    /// The second CLZ step of a two-level lookup: level-1 finds the word,
    /// this lands the bit. Tail-word safe: padding never fakes a hit.
    [[nodiscard]] constexpr std::size_t first_zero_in_word(std::size_t w) const {
        const std::uint32_t free_bits = ~words_[w] & valid_mask(w);
        return free_bits != 0 ? (w << 5) + ctz(free_bits) : npos;
    }

    /// First clear bit (a free slot), npos if none.
    /// Skips whole words with one compare; RBIT+CLZ inside on Cortex-M3+.
    [[nodiscard]] constexpr std::size_t find_first_zero() const {
        for (std::size_t w = 0; w < WORDS; ++w) {
            const std::size_t bit = first_zero_in_word(w);
            if (bit != npos) {
                return bit;
            }
        }
        return npos;
    }

    /// First set bit, npos if none. Tail word is naturally safe: padding 0s never fake-hit.
    [[nodiscard]] constexpr std::size_t find_first_set() const {
        for (std::size_t w = 0; w < WORDS; ++w) {
            if (words_[w] != 0) {
                return (w << 5) + ctz(words_[w]);
            }
        }
        return npos;
    }

    // static_assert on it, memcpy it, summarize it.
    // so public it
    std::uint32_t words_[WORDS]{};

  private:
    // A Copy cast is not thought as popular, i think!
    Bitmap(const Bitmap&) = delete;
    Bitmap& operator=(const Bitmap&) = delete;

    static constexpr std::uint32_t one_hot(std::size_t i) { return 1u << (i & 31); }

    /// Valid-bit mask of the w-th word: full for a plain word, only
    /// the real bits for the tail word. Anchor of every tail-safe check.
    static constexpr std::uint32_t valid_mask(std::size_t w) {
        return (w + 1 == WORDS) ? tail_mask() : ~0u;
    }

    static constexpr std::uint32_t tail_mask() {
        // (bit_count & 31) == 0 would shift by 32, which is UB -> ~0u instead
        return (bit_count & 31) ? (1u << (bit_count & 31)) - 1u : ~0u;
    }
};

// —— Compile-time self checks: free unit tests, zero runtime cost ——
static_assert([] {
    Bitmap<8> b;
    b.set(0); b.set(1);
    return b.find_first_zero() == 2;
}());

static_assert([] {
    Bitmap<5> b;                          // tail word carries 3 padding bits
    for (std::size_t i = 0; i < 5; ++i) b.set(i);
    return b.find_first_zero() == Bitmap<5>::npos;  // padding must never fake a hit
}());

static_assert([] {
    Bitmap<8> b;
    b.set(7);
    return b.find_first_set() == 7 && b.test(7) && !b.test(0);
}());

static_assert(Bitmap<8>{}.find_first_set() == Bitmap<8>::npos);

} // namespace ZerOS::base
```

A few design points here are worth stopping for as you write this.

The header comment lays the contract out as legal clauses: padding bits must stay 0, `set`/`clear` are read-modify-write and not atomic, out-of-range indexes are UB — and then it closes with one extra line, "break them and the helpers will lie to you": break the contract, and these helper functions will dare to lie to you. That's not scaremongering — `word()` really does hand you the raw 32-bit plane, so when you bulk-write whole words, whether the tail padding stays clean is the caller's responsibility. Why insist on word-level access at all? The comment answers that too: it's the layer `std::bitset` refuses to give us, and the pool's L1 summaries and whole-word bulk zeroing both live off of it.

Look at the dual-track `ctz`: GCC/Clang take `__builtin_ctz`, which compiles into RBIT+CLZ on Cortex-M3/M4 — both single-cycle instructions; other compilers get the pure-software fallback, counting one bit at a time. The find functions (`find_first_zero`/`find_first_set`) skip whole words at a time, and a single ctz lands the bit inside the word. That two-stage "skip the word, then land the bit" shape is precisely the intuition we turned over in the previous article — and the embryo of the two-level bitmap to come.

Tail handling is where a bitmap most easily comes to grief, and `tail_mask` is the anchor of every check: a tail word's "full" must be compared against `valid_mask`, not `0xFFFFFFFF` — the comment calls that out on purpose. And because shifting by 32 when `(bit_count & 31) == 0` would be UB, that branch returns `~0u` instead — one comment line covering one edge case; places like this deserve an extra look from you.

Now look at the copy constructor: deleted. The comment says it verbatim: "A Copy cast is not thought as popular, i think!". The bitmap is the kernel's bookkeeping itself — whoever copies one carries a duplicate of the state away — so simply disallowing copies keeps life simple.

The four `static_assert`s at the end of the file are the most interesting part. Look: immediately-invoked lambdas — every time this header gets compiled, a regression run comes along with it. Host tests compile it, firmware builds compile it; nobody escapes. The "padding must never fake a hit" one exists specifically to lock down tail-word safety.

## Setting Up the Test Bench

With the code written, we need a way to keep it in line. The bitmap is going to walk the whole road with us, so we set up the test infrastructure now, and every new part from here on hooks onto this same bench.

First, the root `CMakeLists.txt`, with a mutually exclusive switch added:

```cmake
cmake_minimum_required(VERSION 3.20)
project(ZerOS C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra)

# Two disjoint configurations:
#   default          -> cross firmware (arm-none-eabi toolchain file)
#   ZEROS_BUILD_TESTS=ON -> host-only unit tests, no firmware targets
option(ZEROS_BUILD_TESTS "Build host unit tests" OFF)
if(ZEROS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
else()
    add_subdirectory(src/board/stm32f103_bluepill)
endif()
```

At the project-setup stop we locked the architecture flags inside the toolchain file and kept only cross-target-common things at the root, and the dividend lands here: the host configuration doesn't even need to point at a toolchain file — `cmake -B build-host -DZEROS_BUILD_TESTS=ON` is an ordinary desktop project where we can enable exceptions, run tests, and hook up sanitizers, none of it stepping on anyone's toes; the default configuration is still the firmware, unchanged by a single character.

Beyond configuration, we owe the editor one small file: `.clangd`. The reason: orphan headers — ones not yet included by any TU — get handled by clangd's fallback, the standard stops at gnu++17, and concept syntax turns into a sea of red; injecting per-language flags consistent with CMake fixes it:

```yaml
# Inject flags in per-language blocks: .hpp/.cpp get C++23 (conf HAL headers are
# C-context; fed C++ flags they reject).
# Why: orphan headers (not included by any TU) fall into clangd's fallback, where the
# standard stops at gnu++17 and concepts and other C++20 syntax get false positives;
# once the flag matching CMake's CXX_STANDARD 23 is injected, TUs already in
# compile_commands.json see an identical-value override — no behavior change.
---
If:
  PathMatch: [.*\.hpp, .*\.cpp]
CompileFlags:
  Add: [-std=c++23]
---
If:
  PathMatch: .*\.h
CompileFlags:
  Add: [-std=c2x]
---
Diagnostics:
  UnusedIncludes: None
  MissingIncludes: None
```

Then comes `test/CMakeLists.txt`: we choose Catch2 as the test framework and pull it in with FetchContent, stuffing no third-party code into the repository:

```cmake
# Host-only unit tests. Cross builds (arm-none-eabi) never reach here:
# the root file guards this subdirectory behind ZEROS_BUILD_TESTS (OFF by default).
#
#   cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build-host
#   ctest --test-dir build-host --output-on-failure

include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# The kernel headers as an INTERFACE target so tests stay decoupled
# from the firmware build.
add_library(zeros_test_headers INTERFACE)
target_include_directories(zeros_test_headers INTERFACE ${CMAKE_SOURCE_DIR}/include)

function(zeros_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE Catch2::Catch2WithMain zeros_test_headers)
    # Same hardening the smoke test ran with; ASan+UBSan are free bug finders on host.
    target_compile_options(${name} PRIVATE -Werror -fsanitize=address,undefined)
    target_link_options(${name} PRIVATE -fsanitize=address,undefined)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

zeros_add_test(test_bitmap)
# The next few articles keep adding tests to this list; the reference answer shows it filled in
```

We feed the kernel headers to the tests through an `INTERFACE` library, fully decoupled from the firmware build; every test target uniformly gets `-Werror` plus ASan/UBSan — on host, hanging these on costs nothing, and skipping them would be a waste. The last line currently hooks up only `test_bitmap`; each new part we write in the articles ahead adds one more line to it. That's the incremental approach — when you `diff` against the reference answer, differences in this list are expected.

## Putting the Bitmap on the Rack

`test/test_bitmap.cpp`, six test cases, in full:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <cstddef>

#include "ZerOS/base/bitmap.hpp"

using ZerOS::base::Bitmap;

TEST_CASE("bit level: set/clear/test round trip", "[bitmap]") {
    Bitmap<70> b; // 3 words: 32 + 32 + 6, exercises the tail word too
    for (std::size_t i = 0; i < 70; ++i) {
        b.set(i);
        CHECK(b.test(i));
    }
    for (std::size_t i = 0; i < 70; ++i) {
        b.clear(i);
        CHECK_FALSE(b.test(i));
    }
}

TEST_CASE("word level: empty/full and raw bulk access", "[bitmap]") {
    Bitmap<8> b;
    CHECK(b.word_empty(0));
    CHECK_FALSE(b.word_full(0));

    b.word(0) = 0xFFu; // raw write covering exactly the 8 real bits
    CHECK(b.word_full(0));
    CHECK(b.find_first_zero() == Bitmap<8>::npos);
    CHECK(b.find_first_set() == 0);
}

TEST_CASE("tail word: padding bits never fake a hit", "[bitmap][tail]") {
    Bitmap<5> b; // tail word carries 27 padding bits
    for (std::size_t i = 0; i < 5; ++i) {
        b.set(i);
    }

    CHECK(b.find_first_zero() == Bitmap<5>::npos);
    CHECK(b.first_zero_in_word(0) == Bitmap<5>::npos);
    CHECK(b.find_first_set() == 0);
}

TEST_CASE("find_first_zero crosses into the tail word", "[bitmap]") {
    Bitmap<70> b;
    for (std::size_t i = 0; i < 64; ++i) {
        b.set(i); // fill words 0 and 1 completely
    }

    CHECK(b.find_first_zero() == 64);
    CHECK(b.word_full(0));
    CHECK(b.word_full(1));
    CHECK_FALSE(b.word_full(2));

    b.set(64);
    CHECK(b.find_first_zero() == 65);
}

TEST_CASE("find_first_set skips empty words", "[bitmap]") {
    Bitmap<70> b;
    CHECK(b.find_first_set() == Bitmap<70>::npos);

    b.set(65); // deep inside the tail word
    CHECK(b.find_first_set() == 65);

    b.clear(65);
    b.set(33); // head of the second word
    CHECK(b.find_first_set() == 33);
}

TEST_CASE("first_zero_in_word pinpoints inside one word", "[bitmap]") {
    Bitmap<32> b;
    b.set(0);
    b.set(1);
    b.set(5);

    CHECK(b.first_zero_in_word(0) == 2);

    b.word(0) = ~0u; // full single-word bitmap
    CHECK(b.first_zero_in_word(0) == Bitmap<32>::npos);
}
```

The sizes the cases pick are all deliberate. Look at `70`: `32 + 32 + 6`, three words with only 6 real bits in the tail word — it forces out both the bit-level round trip and cross-word searching. `5` is harsher still: 27 padding bits in the tail word, dedicated to verifying the "padding must never fake a hit" contract. The `word(0) = ~0u` line is the only place the raw plane gets touched; once it is full, `first_zero_in_word` must report `npos`, and the word-level and bit-level views agree.

## Acceptance

Three steps:

```shell
cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
./build-host/test/test_bitmap
```

The real output from my machine:

```text
All tests passed (158 assertions in 6 test cases)
```

Six cases, 158 assertions — all passing is a pass. What you get should match character for character: the cases are fixed, no randomness, no environment differences.

Next time, the bitmap takes up its official post: we write the pool's contract and stand up the fixed-size-block pool over a two-level bitmap — a fuzz of twenty thousand operations is already waiting for it down the road.
