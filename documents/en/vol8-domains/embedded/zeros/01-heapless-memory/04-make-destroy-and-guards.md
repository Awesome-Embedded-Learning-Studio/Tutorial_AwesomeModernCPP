---
title: "Make/Destroy: Typed Birth and Death, and the Compile-Time Line of Defense"
description: "Closing out the memory line: Make/Destroy builds a typed facade on placement new, with ObjectType as the first template parameter so the pool type can be deduced; the sizeof and alignof guards go into static_asserts (a 64-byte object in a 16-byte-aligned block is fine, a 64-byte-aligned object is not — over-aligned data meets LDRD and the hardware HardFaults); the Resurrector case turns Destroy's destroy-first-release-second order into a tested contract; a negative compile test uses try_compile on a TU that must fail to prove the guard exists, and in a real run removing the alignof guard makes configure FATAL_ERROR on the spot; final acceptance = ctest, three targets, 24 cases, 40555 assertions, all passing (real output)"
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
  - "A World Without a Heap · Part 3: The Fixed-Size Block Pool"
related:
  - "Fixed-Size Block Pool: An Allocator with One Bit per Block"
translation:
  source: documents/vol8-domains/embedded/zeros/01-heapless-memory/04-make-destroy-and-guards.md
  source_hash: 68e8f5722e13d9a9fda4786a3c4067550c83209b828ba1dbeee4f88e55c49a01
  translated_at: '2026-09-25T07:55:39+00:00'
  engine: anthropic
  token_count: 4800
---

# Make/Destroy: Typed Birth and Death, and the Compile-Time Line of Defense

The pool hands out `void*`; the kernel objects we want are types. A facade is what sits missing in between. This article finishes the memory line: with the facade seated, the memory stack is complete, and the grand acceptance run begins.

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

        auto raw_buffer = pool.raw_allocate();
        if(!raw_buffer) {
            return std::unexpected {raw_buffer.error()};
        }

        // Placement new the stuff at the given buffer
        // With the given arguments
        return ::new (*raw_buffer) ObjectType(std::forward<CreationArgs>(args)...);
    }

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

Look at what `Make` does: it draws a block of raw memory from the pool, constructs the object in place with `placement new`, and perfect-forwards the constructor arguments; on failure the error passes back untouched, and not one extra object gets constructed. `Destroy` is the reverse: `destroy_at` runs the destructor first, then the block goes back to the pool.

The parameter order is deliberate — the comment's opening sentence is exactly about it: `ObjectType` appears only in the return type, so it can never be deduced and must be specified explicitly — hence it goes first in the template parameter list, leaving the pool type after it to be deduced from the arguments. At the call site that reads `Make<Gadget>(pool, 41, "answer")`: the pool type tags along automatically, and you never have to spell it out.

Those two guard lines are the core defense this article asks you to keep your eyes on. The block must be able to hold the object (`sizeof`), and the object must also sit square (`alignof`): the example in the comment is exact — a 64-byte object in a 16-byte-aligned block is fine; a 64-byte-**aligned** object is not, because `placement new` would land it on an unaligned address, which is UB of the kind no sanitizer reliably catches. On a Cortex-M3 this is no theoretical matter: over-aligned data meeting instructions like `LDRD`/`STM` HardFaults the hardware outright. So both conditions must be intercepted at compile time — one `static_assert` per line.

The guards sit inside `if constexpr (requires ...)`, and you should note how loose this is: if a pool does not expose geometry information like `BLOCK_SIZE`/`BLOCK_ALIGN`, the facade does not insist — it still works. That exact tension is locked down by a dedicated test; more below.

## Putting the Facade on the Rack

The `test/CMakeLists.txt` listing gets its final additions in this article; here it is complete (from here on it matches the reference answer character for character):

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

The negative-test wiring at the tail end gets a dedicated section below. First, write `test/test_typeable.cpp` — eleven test cases, 249 lines in full:

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

Let's pick the three brightest to talk about.

The **Resurrector case** turns the order of two lines inside `Destroy` into a tested contract: when a destructor allocates from its own pool, the new object must land on a **different** block. The comment shows you exactly how to reproduce the bug — flip the order of `destroy_at` and `raw_deallocate` inside `Destroy`, and this case fails on the spot: the just-freed own block becomes the first-fit hole, and `placement new` would overwrite in place an object whose destructor is still running. There is also a small discipline here: no Catch2 macros inside a destructor (they report by throwing), so the result gets parked in an outside pointer and asserted after the destructor exits — fitting, since exceptions are a banned word in the firmware world anyway, and the tests stay consistent with that.

Then the **boundary-value cases**: watch how they pin the equality on both sides of the `static_assert`s — `ExactFit` is exactly 64 bytes, pressing on the `sizeof` guard's equals sign; `MaxAligned` is exactly `max_align_t`-aligned, pressing on the `alignof` guard's equals sign. The guards read `<=`, so both sides have to prove they deserve to pass.

The **BarePool case** locks down how tight the `requires` gate is; weigh this balance yourself: a bare pool with no geometry constants whatsoever still works with `Make`, as long as it satisfies the concept's three operations — the guard means "check when the pool is willing to expose its geometry", not "refuse entry to those that don't".

## Negative Compile Tests: Proving the Defense Exists Is a Test Too

Everything above is positive: the code that should pass, passes. One kind of testing is still missing — for a line of defense like `static_assert`, how do you prove it **exists**? Some future refactor slips a hand and deletes the guard: who raises the alarm? Create `test/neg_make_overaligned.cpp`; the requirement on this file is that it must fail to compile:

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

The wiring sits at the tail of `test/CMakeLists.txt`, which we pasted above: `try_compile` builds this file and stores the result in `zeros_make_overaligned_compiles`; a **successful compile** is what triggers the `FATAL_ERROR` instead. The whole logic is inverted — a normal test proves "the code is right"; this kind proves "the defense is still there".

The attribution is deliberate too; look at the causal chain in the comments: the type's size stays within 64, so the failure cannot come from the `sizeof` guard; the toolchain is pinned to C++23, so it cannot come from a missing `std::expected`. The failure can only come from the `alignof` `static_assert` — a single variable. `test_typeable` passing doubles as the positive control: everything that should pass passes, and what should be blocked really is blocked.

Here's a real run for you. Comment out the `alignof` guard line in `typeable.hpp` and reconfigure:

```text
CMake Error at test/CMakeLists.txt:51 (message):
  neg_make_overaligned.cpp compiled: Make's alignof guard is missing or
  broken
```

There it is: the configure stage goes on strike outright, and the build never even gets its turn. Put the guard back, and everything is normal again. This is the machine-checked guarantee that "the defense cannot be silently torn down".

## Final Acceptance

The wrap-up acceptance for the four memory-line articles takes three commands:

```shell
cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

The real output from my machine:

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

The report cards of the three binaries, one by one:

```text
All tests passed (158 assertions in 6 test cases)
All tests passed (40295 assertions in 7 test cases)
All tests passed (102 assertions in 11 test cases)
```

24 cases, 40555 assertions — most of them from that fuzz: twenty thousand operations, each allocation verifying its stamp, each free verifying ownership; that is how the numbers pile up. With all tests passing, the first four articles of this station count as done: `git add -A` and commit. The numbers you get should match these character for character: the seed is fixed, so it does not matter how many machines you swap between.

An early intuition on footprint, too: compiled into firmware, this memory stack adds overhead approaching zero — the bitmaps' all-zero default state lands in `.bss`, and the pool's construction is `constexpr`. You will verify those numbers with your own eyes once the board enters the picture in the next article.

## Next Article

Now that it is solid on host, the next article moves this whole memory stack onto a `-nostdlib` board. Walls will be hit — three of them at once: `memset` suddenly goes missing (the linker reports an undefined reference, with the line number pointing exactly at the poisoning line), nobody foots the bill for global constructors (`.init_array` is outlawed from then on), and the `__cxa_guard_*` entourage that function-local `static`s drag along is left without support too. Hit the walls one by one, and once you are through, the pools in your hands are truly alive on the Blue Pill. The reference answer is in the repository as usual: `b4a5daf`.
