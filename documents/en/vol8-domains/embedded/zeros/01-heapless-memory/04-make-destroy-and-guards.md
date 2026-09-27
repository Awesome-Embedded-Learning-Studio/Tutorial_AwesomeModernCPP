---
title: "Make/Destroy: Typed Birth and Death, and the Compile-Time Line of Defense"
description: "The closing move of the memory line: Make/Destroy is a typed facade over placement new, with ObjectType first in the template parameter list so the pool type can be deduced; the sizeof and alignof guards go into static_asserts (a 64-byte object in a 16-byte-aligned block is fine, a 64-byte-aligned object is not — over-alignment meets LDRD and the hardware HardFaults); the Resurrector case turns Destroy's destroy-first-return-second order into a tested contract; a negative compile test builds a TU that must fail via try_compile, proving the guard exists — tried for real: rip out the alignof guard and configure FATAL_ERRORs on the spot; final acceptance = ctest, three targets, 24 cases, 40555 assertions, all passing (real output)"
chapter: 1
order: 4
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 内存管理
  - expected
  - cpp-modern
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 25
prerequisites:
  - "Fixed-Size Block Pool: An Allocator with One Bit per Block"
related:
  - "Fixed-Size Block Pool: An Allocator with One Bit per Block"
translation:
  source: documents/vol8-domains/embedded/zeros/01-heapless-memory/04-make-destroy-and-guards.md
  source_hash: 3bde6fe598e12193a3569402bf5b95e9153994e48af1d84c39fa6e01455e2a8d
  translated_at: '2026-09-27T06:05:45+00:00'
  engine: anthropic
  token_count: 6000
---

# Make/Destroy: Typed Birth and Death, and the Compile-Time Line of Defense

The pool hands out `void*`; the kernel objects we want are types. Between the two sits the one layer still missing: a facade. This article writes the memory line to its end: once the facade takes its seat, the memory stack is complete, and the grand acceptance run begins.

Create `include/ZerOS/kernel/mem/typeable.hpp`:

```cpp
#pragma once

#include "pool.hpp"
#include <expected>
#include <memory>

namespace ZerOS::memory
{
    // ObjectType first: it never appears in the parameter list, so callers
    // write Make<T>(pool, args...) and let the pool type be deduced.
    template<typename ObjectType, MemoryPool PoolStuff, typename... CreationArgs>
    std::expected<ObjectType*, MemoryAllocationError> Make(PoolStuff& pool, CreationArgs&&... args) {
        // If the pool exposes its block geometry, hold the object to it:
        // it must FIT (sizeof) and SIT straight (alignof). A 64-byte object
        // in a 16-byte-aligned block is fine; a 64-byte-ALIGNED object is
        // not -- placement new would land it on an unaligned address and
        // that is UB no sanitizer reliably forgives.
        if constexpr (requires { PoolStuff::BLOCK_SIZE; PoolStuff::BLOCK_ALIGN; }) {
            static_assert(sizeof(ObjectType) <= PoolStuff::BLOCK_SIZE, "block overflow");
            static_assert(alignof(ObjectType) <= PoolStuff::BLOCK_ALIGN, "block under-aligned");
        }
```

There is a point to the parameter order, and the comment's first sentence is exactly it: `ObjectType` appears only in the return type, so it can never be deduced and must be specified explicitly — which is why it sits first in the template parameter list, leaving the pool type after it to be deduced from the arguments. At the call site this reads `Make<Gadget>(pool, 41, "answer")`: the pool type tags along automatically, no need for you to spell it out.

Those two guard lines are the core line of defense you should keep your eyes on throughout this article. The block must hold the object — that is the `sizeof` line; the object must also sit straight — that is the `alignof` line. The example in the comment is precise, so let us take it apart.

A 64-byte **object** placed in a 16-byte-aligned block: no problem, it fits and it sits straight. Now look at a 64-byte-**aligned** object: not allowed. The block's alignment is only 16, so `placement new` would land it on an unaligned address.

And what does unaligned get you? This is UB, and the kind no sanitizer reliably catches. On the Cortex-M3 it is especially not a theoretical concern: over-aligned data running into instructions like `LDRD`/`STM` makes the hardware HardFault outright, without so much as a chance to report an error back to us. So both conditions must be intercepted at compile time, one line of `static_assert` each.

The guards are wrapped in `if constexpr (requires ...)`, and you should take note of how tight that grip is: a pool that does not expose geometry such as `BLOCK_SIZE`/`BLOCK_ALIGN` gets no pressure from the facade — it still works. This exact tightness is locked in by a dedicated test, which we meet below.

```cpp
        auto raw_buffer = pool.raw_allocate();
        if(!raw_buffer) {
            return std::unexpected {raw_buffer.error()};
        }

        // Placement new the stuff at the given buffer
        // With the given arguments
        return ::new (*raw_buffer) ObjectType(std::forward<CreationArgs>(args)...);
    }
```

Look at what `Make` does: it draws a block of raw memory from the pool, `placement new` constructs the object in place, and the constructor arguments are perfectly forwarded; on failure the error travels back unchanged, and not one extra object gets constructed.

```cpp
    template<MemoryPool PoolStuff, typename ObjectType>
    MemoryAllocationError Destroy(PoolStuff& pool, ObjectType* obj) {
        if (!obj) {
            return MemoryAllocationError::Ok;
        }
        std::destroy_at(obj);
        return pool.raw_deallocate(obj);
    }
}
```

Now look at `Destroy` — just three lines: a null pointer passes straight through, `destroy_at` runs the destructor first, then the block goes back to the pool.

## Putting the Facade on the Rack

This article tops off the list in `test/CMakeLists.txt`, so let us write it out in its complete form (from here on, character for character identical with the reference solution):

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
zeros_add_test(test_bitmap_pool)
zeros_add_test(test_typeable)

# ---- negative compile test: the alignof guard in typeable.hpp ----
# Make must reject over-aligned objects AT COMPILE TIME. We prove the
# guard by compiling a snippet that tries exactly that and requiring the
# build to FAIL. The toolchain is pinned to C++23 so a failure can only
# come from the static_assert, never from a missing std::expected.
# (test_typeable.cpp passing doubles as the positive control.)
try_compile(zeros_make_overaligned_compiles
    ${CMAKE_CURRENT_BINARY_DIR}/neg-make-overaligned
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/neg_make_overaligned.cpp
    CMAKE_FLAGS
        -DCMAKE_CXX_STANDARD=23
        -DCMAKE_CXX_STANDARD_REQUIRED=ON
        -DINCLUDE_DIRECTORIES=${CMAKE_SOURCE_DIR}/include
)
if(zeros_make_overaligned_compiles)
    message(FATAL_ERROR
        "neg_make_overaligned.cpp compiled: Make's alignof guard is missing or broken")
endif()
```

The wiring of that negative-test stretch at the tail gets a dedicated section below. First, `test/test_typeable.cpp` — eleven cases, the full 249 lines:

```cpp
// Host unit tests for ZerOS/kernel/mem/typeable.hpp:
// the Make/Destroy placement-new facade over any MemoryPool.
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <vector>

#include "ZerOS/kernel/mem/bitmap_allocate.hpp"
#include "ZerOS/kernel/mem/typeable.hpp"

using ZerOS::memory::BitmapPool;
using ZerOS::memory::Destroy;
using ZerOS::memory::Make;
using ZerOS::memory::MemoryAllocationError;
using ZerOS::memory::MemoryPool;

namespace {

// The canonical pool tenant: non-trivial ctor/dtor we can observe.
struct Gadget {
    int a;
    const char* tag;

    static inline int alive = 0;

    Gadget(int a_, const char* tag_) : a(a_), tag(tag_) { ++alive; }
    ~Gadget() { --alive; }
};

} // namespace

TEST_CASE("Make constructs in place and forwards arguments", "[typeable]") {
    BitmapPool<64, 4, false> pool;
    Gadget::alive = 0;

    auto r = Make<Gadget>(pool, 41, "answer");
    REQUIRE(r.has_value());
    CHECK((*r)->a == 41);
    CHECK(std::strcmp((*r)->tag, "answer") == 0);
    CHECK(Gadget::alive == 1);

    // blocks are max-aligned; the object must be too
    CHECK(reinterpret_cast<std::uintptr_t>(*r) % alignof(Gadget) == 0);

    CHECK(Destroy(pool, *r) == MemoryAllocationError::Ok);
    CHECK(Gadget::alive == 0);
}

TEST_CASE("move-only creation arguments compile and arrive intact", "[typeable]") {
    struct MoArg {
        int v;
        explicit MoArg(int v_) : v(v_) {}
        MoArg(const MoArg&) = delete;
        MoArg& operator=(const MoArg&) = delete;
    };

    // Holder only accepts MoArg&&: if Make ever forwarded by copy,
    // this translation unit would not compile (-Werror build).
    struct Holder {
        int got;
        explicit Holder(MoArg&& a) : got(a.v) {}
    };

    BitmapPool<64, 2, false> pool;
    auto r = Make<Holder>(pool, MoArg{7});
    REQUIRE(r.has_value());
    CHECK((*r)->got == 7);
    CHECK(Destroy(pool, *r) == MemoryAllocationError::Ok);
}

TEST_CASE("Destroy runs the destructor and recycles the block", "[typeable]") {
    BitmapPool<64, 4, false> pool;
    Gadget::alive = 0;

    auto r = Make<Gadget>(pool, 1, "a");
    REQUIRE(r.has_value());
    void* block = *r;

    CHECK(Destroy(pool, *r) == MemoryAllocationError::Ok);
    CHECK(Gadget::alive == 0); // destroy_at really ran

    // first-fit: the hole we just made is the next Make's landing spot
    auto again = Make<Gadget>(pool, 2, "b");
    REQUIRE(again.has_value());
    CHECK(*again == block);
    CHECK((*again)->a == 2);
}

TEST_CASE("Destroy of nullptr is a no-op Ok", "[typeable]") {
    BitmapPool<64, 2, false> pool;
    CHECK(Destroy(pool, static_cast<Gadget*>(nullptr)) == MemoryAllocationError::Ok);
}

TEST_CASE("Make propagates pool exhaustion", "[typeable]") {
    BitmapPool<64, 2, false> pool;
    Gadget::alive = 0;

    REQUIRE(Make<Gadget>(pool, 1, "a").has_value());
    REQUIRE(Make<Gadget>(pool, 2, "b").has_value());

    auto r = Make<Gadget>(pool, 3, "c");
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == MemoryAllocationError::OutOfMemory);
    CHECK(Gadget::alive == 2); // nobody was constructed to fail
}

TEST_CASE("poison pools do not misfire across Make/Destroy cycles", "[typeable][poison]") {
    // raw_deallocate refills the block with POISON_VALUE; a clean cycle
    // (nobody writes after free) must reallocate without a false alarm.
    BitmapPool<64, 2, true> pool;

    for (int i = 0; i < 8; ++i) {
        auto r = Make<Gadget>(pool, i, "cycle");
        REQUIRE(r.has_value());
        CHECK((*r)->a == i);
        CHECK(Destroy(pool, *r) == MemoryAllocationError::Ok);
    }
}

TEST_CASE("an object exactly filling the block is accepted", "[typeable]") {
    struct ExactFit {
        std::uint64_t w[8];
    };
    static_assert(sizeof(ExactFit) == 64); // == BLOCK_SIZE, the static_assert boundary

    BitmapPool<64, 2, false> pool;
    auto r = Make<ExactFit>(pool); // zero creation args -> value-initialization
    REQUIRE(r.has_value());
    (*r)->w[7] = 0xDEADBEEFull;
    CHECK((*r)->w[7] == 0xDEADBEEFull);
    CHECK(Destroy(pool, *r) == MemoryAllocationError::Ok);
}

TEST_CASE("the alignof guard admits exactly-max-aligned objects", "[typeable][align]") {
    // The boundary case: alignment equal to the block alignment is fine.
    struct MaxAligned {
        alignas(std::max_align_t) std::byte blob[16];
    };
    static_assert(alignof(MaxAligned) == alignof(std::max_align_t));

    BitmapPool<64, 2, false> pool;
    auto r = Make<MaxAligned>(pool);
    REQUIRE(r.has_value());
    CHECK(reinterpret_cast<std::uintptr_t>(*r) % alignof(MaxAligned) == 0);
    CHECK(Destroy(pool, *r) == MemoryAllocationError::Ok);
}

TEST_CASE("many objects live side by side and die in scramble order", "[typeable]") {
    struct Note {
        unsigned id;
        explicit Note(unsigned i) : id(i) {}
    };
    static_assert(sizeof(Note) <= 32);

    BitmapPool<32, 8, true> pool;

    std::vector<Note*> live;
    for (unsigned i = 0; i < 8; ++i) {
        auto r = Make<Note>(pool, i);
        REQUIRE(r.has_value());
        CHECK((*r)->id == i);
        live.push_back(*r);
    }
    REQUIRE_FALSE(Make<Note>(pool, 99u).has_value());

    // scrambled teardown: each id must be intact right up to its Destroy
    for (unsigned i : {3u, 0u, 7u, 4u, 1u, 6u, 2u, 5u}) {
        CHECK(live[i]->id == i);
        CHECK(Destroy(pool, live[i]) == MemoryAllocationError::Ok);
    }

    // all blocks recycled: the full house fits again
    for (unsigned i = 0; i < 8; ++i) {
        REQUIRE(Make<Note>(pool, i).has_value());
    }
}

TEST_CASE("a destructor may allocate from its own pool", "[typeable]") {
    // Destroy runs destroy_at BEFORE raw_deallocate. A destructor that
    // allocates must land on a DIFFERENT block, never on the block it is
    // currently standing on. (Flip the order in Destroy and this fails:
    // the freed own block is the first-fit hole -> placement new would
    // overwrite the object whose destructor is still running.)
    using Pool = BitmapPool<64, 2, false>;
    Pool pool;

    Gadget* reborn = nullptr;
    struct Resurrector {
        Pool* host;
        Gadget** out;
        explicit Resurrector(Pool* h, Gadget** o) : host(h), out(o) {}
        ~Resurrector() {
            // no Catch2 macros inside a destructor (they throw); park the
            // result and assert outside.
            if (auto r = Make<Gadget>(*host, 7, "reborn")) {
                *out = *r;
            }
        }
    };
    static_assert(sizeof(Resurrector) <= 64);

    auto r = Make<Resurrector>(pool, &pool, &reborn);
    REQUIRE(r.has_value());
    auto* self = *r;

    CHECK(Destroy(pool, self) == MemoryAllocationError::Ok);

    REQUIRE(reborn != nullptr);         // the pool had a spare block
    CHECK(reborn != reinterpret_cast<Gadget*>(self)); // and it was NOT ours
    CHECK(reborn->a == 7);
    CHECK(Destroy(pool, reborn) == MemoryAllocationError::Ok);
}

TEST_CASE("Make also accepts pools without a BLOCK_SIZE constant", "[typeable]") {
    // The size guard is `requires`-gated; a bare MemoryPool must still work.
    struct BarePool {
        alignas(std::max_align_t) std::byte storage[64];
        bool used = false;

        std::expected<void*, MemoryAllocationError> raw_allocate() {
            if (used) {
                return std::unexpected(MemoryAllocationError::OutOfMemory);
            }
            used = true;
            return static_cast<void*>(storage);
        }
        MemoryAllocationError raw_deallocate(void* p) {
            if (p != storage || !used) {
                return MemoryAllocationError::NotOwned;
            }
            used = false;
            return MemoryAllocationError::Ok;
        }
        void* try_allocate() {
            auto r = raw_allocate();
            return r ? *r : nullptr;
        }
    };
    static_assert(MemoryPool<BarePool>);

    BarePool pool;
    auto r = Make<Gadget>(pool, 5, "bare");
    REQUIRE(r.has_value());
    CHECK((*r)->a == 5);
    CHECK(Destroy(pool, *r) == MemoryAllocationError::Ok);
}
```

Let me pick the three brightest to talk about.

The **Resurrector case** turns the order of two lines of code inside `Destroy` into a tested contract: when a destructor allocates from its own pool, the new object must land on a **different** block. The comment even teaches you how to reproduce the bug — flip the order of `destroy_at` and `raw_deallocate` inside `Destroy`, and this case fails immediately: the object's own block, freshly freed, becomes the first-fit hole, and `placement new` would overwrite in place an object whose destructor has not finished running. There is also a small discipline at work: no Catch2 macros inside destructors (they report by throwing), so the result is parked in an outside pointer and asserted once the destructor has returned — and besides, exceptions are a banned word in the firmware world anyway, so the test stays consistent with that.

Then come the **boundary-value cases**: watch them pin the equals signs on both sides of the `static_assert`s — `ExactFit` is exactly 64 bytes, pressing right on the `sizeof` guard's equals sign; `MaxAligned` is exactly `max_align_t`-aligned, pressing right on the `alignof` guard's equals sign. The guards are written `<=`, and both cases have to prove that equality is indeed let through.

The **BarePool case** locks in the tightness of the `requires` gate — weigh this measure for yourself: a bare pool carrying no geometry constants whatsoever still works with `Make`, as long as it satisfies the concept's trio of functions. The guard means "check when the pool is willing to expose its geometry", not "bar the door on those that do not".

## Negative Compile Tests: Proving the Defense Exists Is a Test Too

Everything above was positive cases: the code passes what it should. One style of testing is still missing — `static_assert` is a line of defense, so how do we prove it **exists**? Some future refactor slips and deletes the guard: who raises the alarm? Create `test/neg_make_overaligned.cpp`, and this file's requirement is that it must fail to compile:

```cpp
// Negative compile test: this translation unit MUST fail to build.
//
// Make<> has to reject, at compile time, objects whose alignment exceeds
// the pool block's alignment (see BLOCK_ALIGN and typeable.hpp). The CMake
// side compiles this file with try_compile and requires FAILURE -- the
// only acceptable error source is the static_assert in Make.
//
// sizeof stays within BLOCK_SIZE on purpose, so the failure can only come
// from the alignof guard, never from the sizeof guard.
#include <cstddef>

#include "ZerOS/kernel/mem/bitmap_allocate.hpp"
#include "ZerOS/kernel/mem/typeable.hpp"

struct OverAligned {
    // One notch past max_align_t: perfectly legal C++, impossible to place
    // safely in a max-aligned pool block (unaligned placement new == UB).
    alignas(2 * alignof(std::max_align_t)) std::byte blob[64];
};

static_assert(sizeof(OverAligned) <= 64); // size guard alone must NOT trip

int main() {
    ZerOS::memory::BitmapPool<64, 2, false> pool;
    auto r = ZerOS::memory::Make<OverAligned>(pool);
    (void)r;
    return 0;
}
```

The wiring sits at the tail of `test/CMakeLists.txt`, which we pasted above: `try_compile` builds this file and stores the outcome in `zeros_make_overaligned_compiles`; if it **compiles successfully**, that instead triggers a `FATAL_ERROR`. The whole logic runs backwards — a normal test proves "the code is right"; this kind proves "the defense is still there".

The attribution is just as deliberate — look at the causal chain in the comments: the type's size is pinned within 64, so the failure cannot come from the `sizeof` guard; the toolchain is pinned to C++23, so the failure cannot come from a missing `std::expected`; therefore the failure can only come from the `alignof` `static_assert` — a single variable. `test_typeable` passing doubles as the positive control: everything that should pass does pass, and what should be intercepted truly is.

Let me actually run it for you. Comment out the `alignof` guard line in `typeable.hpp` and reconfigure:

```text
CMake Error at test/CMakeLists.txt:51 (message):
  neg_make_overaligned.cpp compiled: Make's alignof guard is missing or
  broken
```

There it is: the configure stage walks off the job on the spot, before the build even gets its turn. Put the guard back, and everything is as it was. This is the machine-enforced guarantee that "the line of defense cannot be silently dismantled".

## Final Acceptance

The closing acceptance for the memory line's four articles, three commands:

```shell
cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

The real output on my machine:

```text
Test project /tmp/zeros-mem/build-host
    Start 1: test_bitmap
1/3 Test #1: test_bitmap ......................   Passed    0.01 sec
    Start 2: test_bitmap_pool
2/3 Test #2: test_bitmap_pool .................   Passed    0.04 sec
    Start 3: test_typeable
3/3 Test #3: test_typeable ....................   Passed    0.02 sec

100% tests passed out of 3

Total Test time (real) =   0.07 sec
```

The three binaries' individual report cards:

```text
All tests passed (158 assertions in 6 test cases)
All tests passed (40295 assertions in 7 test cases)
All tests passed (102 assertions in 11 test cases)
```

Twenty-four cases, 40555 assertions, most of them out of that fuzz: twenty thousand operations, every allocation verifying its stamp, every free verifying ownership — that is how the number piles up. All tests passing means the first four articles of this station are cleared, and `git add -A` commits it. The numbers you get should match these character for character: the seed is fixed, and it makes no difference how many machines you swap in.

An intuition about size, handed to you in advance as well: compiled into the firmware, this memory stack's added cost trends toward zero — the bitmaps' all-zero default state lands in `.bss`, and the pool's constructor is `constexpr`. You will verify these numbers with your own eyes in the next article, once we are on the board.

## The Next Stop

Stable on host, the next article hauls this whole memory stack onto a `-nostdlib` board. Walls will be hit — three of them in one go: `memset` suddenly goes missing (the linker reports undefined reference, with the line number pinned to the poisoning line), global constructors find nobody picking up the bill (`.init_array` is outlawed from then on), and the `__cxa_guard_*` entourage that function-local `static` drags along loses its footing too. Hit them one wall at a time, and once you are through, the pool in your hands is truly alive on the Blue Pill. The reference solution is in the repo as always, `b4a5daf`.
