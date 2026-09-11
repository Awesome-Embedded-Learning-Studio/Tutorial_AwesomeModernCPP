---
title: "Make/Destroy:类型化的出生入死与编译期防线"
description: "内存线的收口:Make/Destroy 用 placement new 做类型化门面,ObjectType 放模板参数第一位让池类型可推导;sizeof 与 alignof 双守卫进 static_assert(64 字节对象进 16 字节对齐块没问题,64 字节对齐的对象不行——过对齐遇 LDRD 硬件 HardFault);Resurrector 用例把 Destroy 先析构再归还的顺序变成被测契约;负向编译测试用 try_compile 编一个必须失败的 TU 证明守卫存在,实测拆掉 alignof 守卫 configure 当场 FATAL_ERROR;总验收 = ctest 三目标 24 用例 40555 断言全过(真实输出)"
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
  - "没有堆的世界·第 3 篇:定长块池"
related:
  - "定长块池:一块一个 bit 的分配器"
---

# Make/Destroy:类型化的出生入死与编译期防线

池发的是 `void*`,咱们要的内核对象是类型,中间差一层门面。这一篇把内存线写完:门面落座,内存栈就齐了,总验收走起。

新建 `include/ZerOS/kernel/mem/typeable.hpp`:

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

咱们看 `Make` 干的事:从池里领一块裸内存,`placement new` 在原地构造对象,构造实参完美转发过去;失败时错误原样传回,一个多余的对象都不会被构造。`Destroy` 反过来:先 `destroy_at` 跑析构,再把块还给池。

形参顺序有个讲究,注释头一句就是它:`ObjectType` 只出现在返回类型里,永远推导不出来,必须显式指定——所以放在模板参数表第一位,后面的池类型才能从实参推导。调用点写出来就是 `Make<Gadget>(pool, 41, "answer")`,池类型自动跟上,不用您拼写一遍。

守卫那两行,是您这一篇要盯住的核心防线。块要装得下(`sizeof`),对象还要坐得正(`alignof`):注释举的例子精确——64 字节的对象放进 16 字节对齐的块,没问题;64 字节**对齐**的对象,不行,`placement new` 会把它落在没对齐的地址上,这是 UB,而且是没有哪个 sanitizer 能可靠抓住的那种。在 Cortex-M3 上这不是理论问题,过对齐的数据遇上 `LDRD`/`STM` 这类指令,硬件直接 HardFault。所以这两条必须在编译期拦,一行 `static_assert` 一条。

守卫套在 `if constexpr (requires ...)` 里,您留意这个松紧:池要是没暴露 `BLOCK_SIZE`/`BLOCK_ALIGN` 这种几何信息,门面不强迫,照样能用——这个松紧度是有测试专门锁着的,下面见。

## 给门面上刑

`test/CMakeLists.txt` 的清单这一篇添到头,咱们给它成完整版(从此和参考答案一字不差):

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

尾巴上那一段负向测试的接线,咱们放到下一节专门讲。先写 `test/test_typeable.cpp`,十一个用例,249 行全文:

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

挑三颗最亮的说。

**Resurrector 用例**把 `Destroy` 里两行代码的顺序变成了被测契约:析构器从自家池里再分配,新对象必须落在**别的**块上。注释直接教您怎么复现 bug——把 `Destroy` 里 `destroy_at` 和 `raw_deallocate` 的顺序翻过来,这个用例立刻失败:自家块刚被释放就成了 first-fit 的洞,`placement new` 会把一个还没跑完析构的对象原地覆盖。还有个小纪律:析构函数里不能用 Catch2 宏(它们靠异常报告),所以先把结果存到外部指针,出了析构再断言,反正异常在固件世界里也是禁词,测试跟它保持一致。

然后是**边界值用例**,您看它专门钉 `static_assert` 的等号两侧:`ExactFit` 恰好 64 字节压着 `sizeof` 守卫的等号,`MaxAligned` 恰好 `max_align_t` 对齐压着 `alignof` 守卫的等号。守卫写的是 `<=`,两边都得证明自己放行。

**BarePool 用例**锁的是 `requires` 门控的松紧,您掂掂这个分寸:一个没有任何几何常量的裸池,只要满足 concept 三件套,`Make` 照样能用——守卫是"池愿意暴露几何才检查",不是"不暴露就拒之门外"。

## 负向编译测试:证明"防线存在"也是测试

上面全是正例:代码该过的过。咱们还差一种测法——`static_assert` 这道防线,怎么证明它**存在**?未来某次重构手一滑把守卫删了,谁来报警?新建 `test/neg_make_overaligned.cpp`,这个文件的要求是:必须编译失败:

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

接线就在 `test/CMakeLists.txt` 尾巴上,咱们上面贴过了:`try_compile` 编这个文件,结果存进 `zeros_make_overaligned_compiles`;**编译成功**反而 `FATAL_ERROR`。整个逻辑是反的——正常测试证明"代码是对的",这一种证明"防线还在"。

归因也讲究,您看注释里的因果链:类型尺寸压在 64 以内,失败不可能来自 `sizeof` 守卫;工具链固定在 C++23,失败不可能来自缺 `std::expected`,所以失败只可能来自 `alignof` 那条 `static_assert`,单一变量。`test_typeable` 通过,兼任正对照:能过的都过了,该拦的真拦了。

真跑给您看。把 `typeable.hpp` 里 `alignof` 守卫那行注释掉,重新配置:

```text
CMake Error at test/CMakeLists.txt:51 (message):
  neg_make_overaligned.cpp compiled: Make's alignof guard is missing or
  broken
```

您看,配置阶段直接罢工,连构建都轮不到。把守卫加回去,一切如常。这就是"防线不会被无声拆掉"的机器保证。

## 总验收

内存线四篇的收工验收,三条命令:

```shell
cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

笔者本机的真实输出:

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

三个二进制各自的成绩单:

```text
All tests passed (158 assertions in 6 test cases)
All tests passed (40295 assertions in 7 test cases)
All tests passed (102 assertions in 11 test cases)
```

24 个用例、40555 条断言,大多数来自那个 fuzz:两万次操作,每次分配验 stamp、释放验归属,数字就是这么攒出来的。测试全部通过,这一站的前四篇就算过,`git add -A` 提交。您跑出来的数字应该跟这里一字不差:种子是固定的,机器换多少台都一样。

体积上的直觉也提前给您:这套内存栈编译进固件,额外开销趋近于零——位图全零默认态落 `.bss`,池的构造是 `constexpr`,这些数字下一篇上了板子您亲眼验。

## 下一篇

host 上写稳了,下一篇把这整套内存栈搬上 `-nostdlib` 的板子。会撞墙,一撞就是三面:`memset` 会突然失踪(链接器报 undefined reference,行号精确到毒化那一行)、全局构造没人埋单(`.init_array` 从此立法禁止)、函数局部 `static` 拖家带口的 `__cxa_guard_*` 也没了着落。一面一面撞过去,撞完您手里的池子就真的在 Blue Pill 上活了。参考答案照旧在仓库里,`b4a5daf`。
