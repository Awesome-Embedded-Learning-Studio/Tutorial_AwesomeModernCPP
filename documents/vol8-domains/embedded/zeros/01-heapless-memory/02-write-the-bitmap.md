---
title: "把位图写出来:定容量的 Bitmap"
description: "概念篇想清楚了的路,这一篇走:新建 include/ZerOS/base/bitmap.hpp,定容量模板,位级 set/clear/test 加字级 word() 访问(std::bitset 拒绝给的那一层);ctz 双轨(__builtin_ctz 在 Cortex-M3/M4 编成 RBIT+CLZ,纯软件兜底);tail_mask 做全部尾字检查的锚;文件尾四条 static_assert 立即调用 lambda,每次编译都是一次回归。测试基建同步立起来:根 CMakeLists 加 ZEROS_BUILD_TESTS 互斥开关、.clangd 按语言注入旗标、Catch2 v3.7.1 经 FetchContent 进场;验收 = test_bitmap 六用例 158 断言全过(真实输出)"
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
  - "没有堆的世界·第 1 篇:内存怎么发,位图是什么"
related:
  - "没有堆的世界:内存怎么发,位图是什么"
---

# 把位图写出来:定容量的 Bitmap

上一篇咱们把位图在手上盘过了:一个 bit 记一块,查空就是找第一个 0。这一篇把它写成真代码,顺便把测试的台子立起来——从这一篇起,咱们写的每样东西都有测试伺候,位图这种要陪咱们走到最后的零件,更得第一天就上保险。

咱们新建 `include/ZerOS/base/bitmap.hpp`:

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

    // —— 位级 ——
    constexpr void set(std::size_t i) { words_[i >> 5] |= one_hot(i); }
    constexpr void clear(std::size_t i) { words_[i >> 5] &= ~one_hot(i); }
    [[nodiscard]] constexpr bool test(std::size_t i) const {
        return (words_[i >> 5] & one_hot(i)) != 0;
    }

    // —— 字级 (what std::bitset refuses to give us) ——
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

    // —— CLZ 查找 ——
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

几个设计点,您写的时候值得停下来看看。

头注释把契约写成了合同条款:padding 位必须保持 0、`set`/`clear` 是读改写不是原子的、越界是 UB,最后补一句"break them and the helpers will lie to you"——违约了,这些工具函数就敢骗您。这不是吓唬人,`word()` 给的就是裸的 32 位平面,批量写整字的时候尾部 padding 保不保干净,责任在调用者。为什么非要有字级访问?注释也给了答案:这是 `std::bitset` 拒绝给的东西,而池的 L1 摘要、整字的批量置零,都指着它过日子。

咱们看 `ctz` 的双轨:GCC/Clang 用 `__builtin_ctz`,到 Cortex-M3/M4 上编译成 RBIT+CLZ,两条都是单周期指令;别的编译器有纯软件兜底,一个 bit 一个 bit 数。查找函数(`find_first_zero`/`find_first_set`)整字整字地跳,字内一步 ctz 落位:这个"跳字+落位"的两段式,就是上一篇盘的直觉,也是后面两级位图的雏形。

尾部怎么处理是位图最容易翻车的地方,`tail_mask` 是全部检查的锚:尾字的"满"要跟 `valid_mask` 比而不是 `0xFFFFFFFF`,注释专门提醒了。而 `(bit_count & 31) == 0` 时移 32 位是 UB,所以那个分支返回 `~0u`——一行注释交代一个边界,这种地方值得您多看一眼。

您再看拷贝构造:被删了,注释原话:"A Copy cast is not thought as popular, i think!"。位图是内核的记账本体,谁复制一份谁就把状态复制走了,不让复制省心。

文件尾巴上那四个 `static_assert` 最有意思,您看:立即调用的 lambda,每次编译这个头,回归就跟着跑了一遍——host 测试编它,固件构建也编它,谁都逃不掉。"padding 不得伪造命中"那条就是专门锁尾字安全性的。

## 测试的台子

代码写完就得有法子收拾它。位图以后要陪咱们走完全程,测试基建现在立,以后每个新零件都往这套台子上挂。

先改根 `CMakeLists.txt`,加一个互斥开关:

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

工程搭建那一站咱们把架构 flags 关在工具链文件里、根上只放跨目标通用的东西,分红到这里到账:host 配置连工具链文件都不用指,`cmake -B build-host -DZEROS_BUILD_TESTS=ON` 就是普通桌面工程,开异常、跑测试、挂 sanitizer,谁也不碍着谁;默认配置还是固件,一个字没变。

配置之外,咱们还得给编辑器补一个小文件 `.clangd`。起因是孤立头文件(还没被任何 TU 包含的那种)会被 clangd 的 fallback 处理,标准停在 gnu++17,concept 语法一片红;按语言注入跟 CMake 一致的旗标,就好了:

```yaml
# 注入旗标按语言分块:.hpp/.cpp 按 C++23(conf HAL 头是 C 语境,吃 C++ 旗标会拒)。
# 起因:孤立头文件(未被任何 TU 包含)吃 clangd fallback,标准停在 gnu++17,
# concept 等 C++20 语法误报;注入与 CMake CXX_STANDARD 23 一致的旗标后,
# 对已在 compile_commands.json 的 TU 是同值覆盖,无行为变化。
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

然后是 `test/CMakeLists.txt`,咱们把测试框架选成 Catch2,FetchContent 拉进来,不往仓库里塞代码:

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
# 后面几篇会往这份清单里添测试;参考答案里是添满的样子
```

咱们把内核头通过一个 `INTERFACE` 库喂给测试,跟固件构建彻底解耦;每个测试目标统一 `-Werror` 加 ASan/UBSan——host 上挂这个不费事,不挂可惜。最后一行现在只挂了 `test_bitmap`,后面几篇每写一个新零件就添一行,这是渐进的走法,您拿 `diff` 对参考答案时,这份清单的差异是预期的。

## 给位图上刑

`test/test_bitmap.cpp`,六个用例,全文:

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

用例挑的尺寸都有讲究,您看 `70`:`32 + 32 + 6`,三个字,尾字只有 6 个真位——位级往返和跨字查找都被它逼出来;`5` 更狠,尾字 27 个 padding 位,专门验证"padding 不得伪造命中"那条契约。`word(0) = ~0u` 那行是唯一动裸平面的地方,写满之后 `first_zero_in_word` 必须报 `npos`,字级和位级两套视图对得上。

## 验收

三步:

```shell
cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
./build-host/test/test_bitmap
```

笔者本机的真实输出:

```text
All tests passed (158 assertions in 6 test cases)
```

六个用例、158 条断言,全过即过。您跑出来应该一字不差:用例是死的,没有随机,没有环境差异。

下一篇,位图转正上岗:咱们写池的契约,把两级位图的定长块池立起来——两万次操作的 fuzz,已经在后面等着它了。
