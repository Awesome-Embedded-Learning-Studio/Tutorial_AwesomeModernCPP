---
title: "定长块池:一块一个 bit 的分配器"
description: "位图转正上岗:先写 MemoryPool concept 把池契约交给编译器(raw_allocate 返回 expected、try_allocate 留给 ISR);再写两级位图定长块池 BitmapPool——L1 每 L2 字一个满标志、L2 每块一位,first-fit 找空;free 后 0x67 毒化检测是裸机上自助的 use-after-free 探测,ever_poisoned_ 门控防新鲜 .bss 误报;野指针/内部指针/double-free 归 NotOwned;验收 = test_bitmap_pool 七用例 40295 断言全过(含固定种子 20260904 两万次操作的 fuzz,stamp/~stamp 双值证独占)"
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
  - "没有堆的世界·第 2 篇:把位图写出来"
related:
  - "把位图写出来:定容量的 Bitmap"
---

# 定长块池:一块一个 bit 的分配器

位图在手,这一篇咱们让概念篇里画的格子真正落地:一个能发块、能收块、能验住客身份的定长块池。

## 先写契约,再写实现

第一篇咱们吹过牛:接口约束写成编译器可检查的形式,实现少一个函数,`static_assert` 当场失败。现在轮到它出场。新建 `include/ZerOS/kernel/mem/pool.hpp`:

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

契约的内容您一眼能看完:能发一块(`raw_allocate` 返回 `expected<void*, 错误码>`),能收一块(`raw_deallocate` 返回错误码),还有给中断留的快路(`try_allocate` 直接返回裸指针,失败就是 `nullptr`——ISR 里没功夫拆 `expected`)。

错误码是一个小分类学:`OutOfMemory` 是真的没格子了,`Poisoned` 是检测到有人写过已释放的块,`NotOwned` 是您拿来归还的指针根本不是这池的——野指针、内部指针、double-free 全归它管。为什么错误通道用 `expected` 不用 `optional`?下面池子的注释给了两条理由:多数实现的 `optional` 要多付 8 字节;更重要的是错误分类不该在用户接口那层丢掉。

## 主角:两级位图定长块池

新建 `include/ZerOS/kernel/mem/bitmap_allocate.hpp`:

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

咱们把结构捋一遍。

**两级位图**。`bitmap_l2_` 每块一位记占用;`bitmap_l1_` 每个 L2 字一位记"这个字满了没"。找空块的路径就两步:L1 里 `find_first_zero` 找到第一个没满的字,再在这个字里 `first_zero_in_word` 落到具体的 bit——上一篇位图里那个"跳字+落位"的两段式,原样上岗。块数少的时候看不出便宜,块数一多,整字整字地跳就把搜索压成常数级;而且您应该已经眼熟了,商用 RTOS 的优先级就绪位图就是这么找"最高优先级就绪任务"的,头注释里那句 "allocators, schedulers" 不是白写的,这个结构到调度器那一站会再出场一次。

**毒化检测**。`owns_poison_policy` 开着的时候,块一归还就整块填 `0x67`,并且 `ever_poisoned_` 把这一位标上;下次这个块再被分配出来之前,先验一遍还是不是满块 0x67——不是,说明有人写过已释放的内存,直接返回 `Poisoned`。这就是概念篇说的"账本和库存分离"换来的能力:空闲链把指针藏在块肚子里,块的内容天然是脏的,想验无从验;位图的块释放后干干净净,填进去什么就是什么,被谁动过手指一查便知。板上没有 ASan,这是咱们自助的 use-after-free 探测。`ever_poisoned_` 的门控是为了不冤枉好人:从来没毒化过的新鲜 `.bss` 块本来就是全零,不该被当成"毒被改了"。

**归还的资格审查**。`index_of_given_ptr` 先看指针在不在池的地界里、是不是块对齐的,再由 `release_block` 看这块是不是真占着:野指针、指向块中间的内部指针、别人池子的指针、double-free,统统 `NotOwned`。注意这里 `p < buffer_` 这行:两个不相干对象比指针大小,严格讲是未指明行为,不过这是 host 侧代码、实践上人人这么写,真要较真是可以改成整数比较的——启动代码那边咱们守着"转整数再比"的纪律,两处对照,您自己掂量。

**几何的编译期约束**。咱们要求块尺寸是 `max_align_t` 的倍数,不然第二块开始就不对齐了;`buffer_` 挂 `ALL_ALIGNED`,`#define` 用完就 `#undef`,宏卫生。文件尾巴一行 `static_assert(MemoryPool<BitmapPool<64, 8, true>>)`:池自己向 concept 证明自己,少实现一个接口,这行就把构建拦下来。

## 拿两万次操作招呼它

`test/CMakeLists.txt` 的清单添一行:

```cmake
zeros_add_test(test_bitmap)
zeros_add_test(test_bitmap_pool)
```

然后 `test/test_bitmap_pool.cpp`,七个用例,全文:

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

两个用例值得您写的时候慢下来。

您写 100 块那个用例时,注释值得逐行读:它记的是三个真实的历史 bug——l2 位图误用字数当位数、l1 误用 `L1_SIZE` 当位宽、释放时只有当字变**空**才清 l1,最后这个 bug 的后果是 l1 的位一旦置上就永远冻着,整个字再也找不回来。用例的做法是把 100 块全占满,只挖一个洞,再要求这个洞必须能被找到:洞要是找不到,只能是 l1 到 l2 的路断了。

fuzz 用例是这一篇的压舱石,值得您为它多停五分钟。种子固定 `20260904`:失败必须逐位可复现,换机器也一样;55/45 的分配偏置让池真的会打满再排干,而不是不痛不痒地摸两下;每个活块写进 `stamp` 和 `~stamp` 两个值,释放前验一遍:stamp 破了,说明有两个主人或一次野写碰过这块。两万次操作下来,任何一个不变量崩了都会当场翻车。

## 验收

```shell
cmake --build build-host
./build-host/test/test_bitmap_pool
```

笔者本机的真实输出:

```text
All tests passed (40295 assertions in 7 test cases)
```

四万条断言,大头全在 fuzz 里,您跑多少遍都一个数——种子是死的。全过即过。

池子能发 `void*` 了,但内核对象要的是类型。下一篇咱们写最后一层门面:`Make<T>(pool, ...` 让对象在池里出生,`Destroy` 让它体面入土,外加两道编译期防线和一个专门证明"防线存在"的负向测试。
