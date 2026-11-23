---
title: "mini STL in Action (4): RingBuffer, the Shortest Introduction to a Ring"
description: "A fixed-capacity circular queue: monotonic read and write pointers that only increase, determining empty and full states with a single subtraction, and turning modulo into a bitwise AND with powers of two. An assembly experiment comparing three modulo operations, coverage-style push with Census balancing, and the missing half-circle in the destructors for continuous intervals on a ring layout."
chapter: 4
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - 容器
  - 循环缓冲区
  - 优化
difficulty: intermediate
platform: host
reading_time_minutes: 13
cpp_standard: [20]
prerequisites:
  - "mini STL in Action (3): Vector, the Five Pieces, Exception Safety, and Concepts"
related:
  - "mini STL in Action (3): Vector, the Five Pieces, Exception Safety, and Concepts"
  - "mini STL in Action (5): CircularDeque (1), Ring Invariants and Iterators"
translation:
  source: documents/vol8-domains/data-structure/04-ring-buffer.md
  source_hash: 1ce90c5eaee8015fef13521e032d49836f90da92c083af94116bf74715d352d4
  translated_at: '2026-11-22T13:30:08+00:00'
  engine: qwen3:14b
  token_count: 14501
---
# mini STL in Action (4): RingBuffer, the Shortest Introduction to a Ring

At the end of the previous article, we mentioned that Chromium even has a comment stating that "even taking an extra modulo is too expensive." This is not a paraphrase of the original comment. Let's bring the original text into the discussion. It is located in `base/containers/ring_buffer.h`:

```cpp
  // This specialization of |IsFilledIndex| is a micro-optimization that enables
  // us to do e.g. `CHECK(IsFilledIndex(n))` without calling |BufferIndex|
  // twice. Since |BufferIndex| involves a % operation, it's not quite free at a
  // micro-scale.
```

In plain English: `BufferIndex` contains a modulo operation, and the cost is real on a micro scale. Therefore, they prefer to add a small specialization rather than perform two modulo operations in a single access. `ring_buffer.h` was added to Chromium in 2013, and it is only 139 lines long, even shorter than our first mirror `vector_buffer.h`. In this article, we will follow this modulo operation and create the shortest container in the series: `RingBuffer`, a fixed-capacity container where the earliest elements leave first, and the oldest is overwritten when full.

## Both Ends of the Queue Should Be O(1)

Why use a ring structure? Let's see with our current `Vector`. A queue requires elements to be added to the end and removed from the front. `Vector` is only good at one end: `push_back` is O(1), but `erase(begin())` requires moving all the elements forward by one, which takes O(n) time. The cost of each dequeue is entirely spent on moving elements, and this usage pattern completely misuses the strengths of `Vector`.

Another scenario is even more extreme: there's no need to dequeue at all, only the most recent data is needed. A flight data recorder is a typical example: it only writes, has a fixed capacity, and the most important data is the last few seconds before a crash, with earlier data being overwritten. Embedded circular logs, exception sampling, and oscilloscope rolling windows are all of the same type. We will define the semantics of this article accordingly: `push` is always successful, and when the buffer is full, the oldest element is overwritten. This is called a covering style. Another style is the rejecting style, which refuses to write when full and returns a status. We will leave this as an exercise for variation, and it's easy to modify after the main line is implemented.

Chromium's `RingBuffer` is a ready-made reference for the covering style. It has only one member: a `std::array<T, kSize>` and a monotonic `current_index_`. `SaveToBuffer` writes and increments the count, and there is no dequeue interface. You can only read the most recent `kSize` elements. Since it uses `std::array` for storage, `T` must be default-constructible and assignable. We will continue using the raw memory from stage1, where the birth and death of objects are managed by the user. Additionally, we need to support `pop`, which will change the layout.

## Ring Index: Physical Index = Logical Sequence % N

The array is linear, but the queue is circular. A pair of indices completes the transformation. We will give `RingBuffer` two members: `read_pos` and `write_pos`, both are monotonic integers, recording the logical sequence number of the current read and write positions. The writing procedure is to place the element in the grid corresponding to `write_pos % N`, then increment `write_pos`. Reading is symmetric: read the grid corresponding to `read_pos % N`, then increment `read_pos`. The sequence number always moves forward, and all the circling is handed over to the modulo operation, thus connecting the beginning and end of the array logically.

Here is a table for N = 8, with the logical sequence number ranging from 0 to 15, and the physical index always looping between 0 and 7:

| Logical Sequence | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Physical Index | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |

The living elements are always the grids corresponding to the logical sequence number `[read_pos, write_pos)`. It may be a continuous segment in the middle of the array, or it may wrap around from the end to the beginning. Chromium only uses one `current_index_` because it only writes and never deletes, and a single write pointer is sufficient. We need to support `pop`, so both read and write pointers must be included.

## Pointers Only Increase, 584 Years Until Wraparound

You might wonder, won't the sequence number overflow if it keeps increasing? Let's do a quick calculation to be assured. Both sequence numbers use `std::uint64_t`, and 2^64 is 18,446,744,073,709,551,616, approximately 1.84 × 10^19. Even if it is used as a high-frequency log counter, writing 1 billion times per second, it would take 1.84 × 10^10 seconds to exhaust 2^64. A year is approximately 31,557,600 seconds, and dividing the two numbers gives about 584 years. No software can live that long, so the sequence number can safely be monotonic. Chromium's `current_index_` is `size_t`, and on 64-bit platforms, it is also 8 bytes, using the same design.

The convenience gained from monotonic pointers will be realized in the next section.

## Determining Empty and Full: Three Textbook Solutions

The classic problem of the ring: after one full rotation, `read_pos` and `write_pos` are equal. At this point, is it empty or full? The two states look the same. There are three textbook solutions, and we will go through them one by one.

The first is called "sacrificing one slot," always leaving one slot unused. When `write_pos` reaches the slot next to `read_pos`, it is considered full. The cost is directly paid in the capacity, losing one slot, and the modulo comparison is scattered across various operations.

The second is a counter, maintaining a separate `size_` member. It is straightforward and easy to understand. The cost is an extra read and write for each operation, and we have to carry an extra state that must always be kept in sync. If we forget to synchronize once, the empty and full states will all be messed up.

The last one is the approach we used in the previous section, monotonic indices. The 64-bit sequence number only increases, and empty is `read_pos == write_pos`, full is `write_pos - read_pos == N`, with a single integer subtraction giving the result, and no extra state is needed. We will use this approach for the main line, and it is the same decision as the 584-year wraparound.

Sacrificing one slot is not bad, but that technique is left for the next stop, `CircularDeque`, where the ring migration is more compatible with it. When you compare them, you will feel the difference.

## Power of Two: Modulo Written as Bitwise AND

When N is a power of two, `x % N` is equivalent to `x & (N - 1)` for non-negative x. The reasoning is not deep, let's go through it in binary: N = 8 in binary is 1000, N - 1 is 0111. Using it as a bitwise AND leaves exactly the lower three bits of x, which are the remainder of x divided by 8. We don't have to worry about negative numbers, as the sequence number always moves forward, and the premise is naturally satisfied. Therefore, we define N as the template parameter of `RingBuffer`, and we also add a check in the class:

```cpp
template <typename Sources, std::size_t N> class RingBuffer {
    // N 不是 2 的幂,& (N - 1) 的等价性就没了,拦在编译期
    static_assert(N != 0 && (N & (N - 1)) == 0, "N must be a power of two");
```

You might wonder, won't the compiler optimize `x % 8` for constant modulo? Yes, and we will see it with assembly. Why write `& (N - 1)` manually? Writing it makes the dependency explicit: readers can see at a glance that the equivalence depends on N being a power of two, and if we ever want to relax the constraint, we can find it easily. Chromium's approach is the opposite: `ring_buffer.h` has no requirement that kSize be a power of two, and they directly write `% kSize`, leaving all optimization to the compiler. Both approaches are valid, and we will state our reasons, and you can choose either side.

## Small Assembly Experiment: Three Modulo Outcomes

Saying the compiler will optimize is not enough. We will write three small functions, each with a different origin of N, and run them with `-O2 -S` using our local g++ 16.2.1, to see what each modulo becomes. The type is `std::uint64_t`, consistent with the actual type of the sequence number:

```cpp
#include <cstdint>

// 三种取模,对应环形缓冲里 N 的三种来历
std::uint64_t mod_pow2(std::uint64_t x) {
    return x % 8; // 编译期常量,2 的幂
}
std::uint64_t mod_other(std::uint64_t x) {
    return x % 6; // 编译期常量,不是 2 的幂
}
std::uint64_t mod_runtime(std::uint64_t x, std::uint64_t n) {
    return x % n; // 运行时变量
}
```

The body of `mod_pow2` is only one bitwise AND, the same treatment as our manually written `& 7`:

```asm
movq	%rdi, %rax
andl	$7, %eax
ret
```

Now let's look at `mod_other`, which is more complex:

```asm
movabsq	$-6148914691236517205, %rax
mulq	%rdi
movq	%rdx, %rax
shrq	$2, %rax
leaq	(%rax,%rax,2), %rdx
movq	%rdi, %rax
addq	%rdx, %rdx
subq	%rdx, %rax
ret
```

We will translate the instructions one by one: the compiler uses the magic number -6148914691236517205 for a wide multiplication and takes the high bits, then shifts right by two bits to get x / 6, and finally subtracts 6 × (x / 6) from x to get the remainder. This is the strength reduction of division, cheaper than real division but much more expensive than a single bitwise AND. Finally, we see a runtime variable, and the actual division instruction is used:

```asm
movq	%rdi, %rax
xorl	%edx, %edx
divq	%rsi
movq	%rdx, %rax
ret
```

64-bit division is famously slow on x86-64. According to Agner Fog's instruction table, the latency of 64-bit division on Skylake is several dozen cycles, while a bitwise AND only takes a few cycles. With the three outcomes in front of us, we have a big advantage: N is a template parameter, and we have all the privileges of a constant. Writing `% N` and writing `& (N - 1)` generate the same `and` instruction. The only remaining significance of manually writing the bitwise AND is to write the agreement into the code for the readers to see.

## Single-File Version of RingBuffer, Built with One Command

The stage2 engineering version is crafted by the author, and the construction manual is already in the accompanying directory. In this article, we will make the teaching version self-contained as a single file. The following three code segments, when concatenated in order, form a complete `ring_buffer_demo.cpp`. The header is `Check`, and the crash-style check from stage1 is copied as-is:

```cpp
// ring_buffer_demo.cpp - RingBuffer 教学单文件版
// 编译:g++ -std=c++20 ring_buffer_demo.cpp -o ring_buffer_demo
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <source_location>
#include <utility>

namespace tamcpp::ministl::debug {
// 崩溃式检查,stage1 的 check.hpp 原样搬来
static inline void Check(bool IsExpectedTrue, const char* msg,
                         std::source_location loc = std::source_location::current()) {
    if (IsExpectedTrue) {
        return; // 哈,就是对的?无事发生~
    }
    std::fprintf(stderr, "[TAMCPP Check Crash]%s-%d(%s): %s\n", loc.file_name(), loc.line(),
                 loc.function_name(), msg);
    std::abort(); // 再见了小朋友
}
} // namespace tamcpp::ministl::debug
```

We will present the main character in full, with query, push, pop, front, visit_at, and clear, all included:

```cpp
namespace tamcpp::ministl {

template <typename Sources, std::size_t N> class RingBuffer {
    // N 不是 2 的幂,& (N - 1) 的等价性就没了,拦在编译期
    static_assert(N != 0 && (N & (N - 1)) == 0, "N must be a power of two");

  public:
    RingBuffer() : ring_(static_cast<Sources*>(::operator new(N * sizeof(Sources)))) {}

    // 教学版只留移动这一件,五件套的完整版进 stage2 工程再补
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&& other) noexcept
        : ring_(other.ring_), read_pos_(other.read_pos_), write_pos_(other.write_pos_) {
        other.ring_ = nullptr;
        other.read_pos_ = other.write_pos_ = 0;
    }

    ~RingBuffer() {
        // 活元素绕了一圈,物理上不保证连续,沿逻辑序号走才收得干净
        for (std::uint64_t i = read_pos_; i != write_pos_; ++i) {
            ring_[i & (N - 1)].~Sources();
        }
        ::operator delete(ring_);
    }

    [[nodiscard]] bool empty() const { return read_pos_ == write_pos_; }
    [[nodiscard]] bool full() const { return write_pos_ - read_pos_ == N; }
    [[nodiscard]] std::size_t size() const {
        return static_cast<std::size_t>(write_pos_ - read_pos_);
    }

    void push(Sources v) {
        if (full()) {
            // 满了:最老的让位,格子腾给最新的
            ring_[read_pos_ & (N - 1)].~Sources();
            ++read_pos_;
        }
        std::construct_at(ring_ + (write_pos_ & (N - 1)), std::move(v));
        ++write_pos_; // 序号只增不减,绕圈全交给 & (N - 1)
    }

    void pop() {
        debug::Check(!empty(), "pop on empty RingBuffer");
        ring_[read_pos_ & (N - 1)].~Sources();
        ++read_pos_;
    }

    Sources& front() {
        debug::Check(!empty(), "front on empty RingBuffer");
        return ring_[read_pos_ & (N - 1)];
    }
    const Sources& front() const {
        debug::Check(!empty(), "front on empty RingBuffer");
        return ring_[read_pos_ & (N - 1)];
    }

    Sources& visit_at(std::size_t logical) {
        // 界是 size 不是 N:空格子里没有活着的对象
        debug::Check(logical < size(), "visit_at out of range");
        return ring_[(read_pos_ + logical) & (N - 1)];
    }
    const Sources& visit_at(std::size_t logical) const {
        debug::Check(logical < size(), "visit_at out of range");
        return ring_[(read_pos_ + logical) & (N - 1)];
    }

    void clear() {
        for (std::uint64_t i = read_pos_; i != write_pos_; ++i) {
            ring_[i & (N - 1)].~Sources();
        }
        read_pos_ = write_pos_ = 0;
    }

  private:
    Sources* ring_;
    std::uint64_t read_pos_{0};  // 读到哪个逻辑序号
    std::uint64_t write_pos_{0}; // 写到哪个逻辑序号,单调递增
};
} // namespace tamcpp::ministl
```

Memory follows the stage1 approach, `::operator new` allocates N slots, managing only capacity, not objects. All three queries are subtraction and comparison, with no extra state stored. The covering semantics are all in the push operation: when the buffer is not full, construct and advance, just like `Vector`'s `push_back`. When it is full, the oldest element is located at the grid pointed to by `read_pos`, we destruct it, advance `read_pos` by one, and the freed position is exactly where the new element will be placed. The order is: destruct the oldest, advance the read pointer, construct the new, and advance the write pointer. No step can be out of order, or the Census count will be unbalanced (this type was seen in section 03, birth adds one, death subtracts one). We will see the result at the acceptance phase. Exception safety is also handled here: construction is before advancement. If construction throws when not full, the sequence number remains in place, and the buffer is untouched. When full, the oldest has already given way. If the new construction fails, it is truly lost, which is the cost of the covering style.

The return type of `pop` is `void`. If you want to dequeue a value, you have to use `front` separately. This is intentional. If `pop` returns the element by value, the copy construction during return may throw, and the element would have already been deleted from the buffer, resulting in data loss. Splitting into `front` and `pop`, where value retrieval and dequeueing each go their own way, allows each step to throw an exception. The standard library's `std::queue::pop` also returns `void`, and you can verify the signature on cppreference.

The `Check` line for popping an empty buffer follows the division of labor established in the series introduction: whoever the mirror is, the error handling follows. We will show both approaches: `RingBuffer` follows Chromium, and if the precondition is violated, it crashes on the spot, not throwing an exception for the caller to catch. The `std::vector::at` from the standard library is another group, throwing `std::out_of_range` on out-of-bounds access, and the caller can catch and recover. `front` on an empty buffer and `visit_at` out-of-bounds are both treated uniformly with `Check` in our version. If you ask why the boundary for `visit_at` is `size` instead of `N`, think about what's in the grids: empty grids have no objects, and accessing them is undefined behavior, so `Check` must block it in advance.

## Coverage and Wraparound, Census Acceptance

We will add the third segment of code, the count type and `main`, to complete the file:

```cpp
// 出生 +1、死亡 -1 的计数类型,Census 在 03 篇登过场
struct Census {
    inline static int alive = 0;
    int tag;
    explicit Census(int t) : tag(t) { ++alive; }
    Census(const Census& other) : tag(other.tag) { ++alive; }
    Census(Census&& other) noexcept : tag(other.tag) { ++alive; }
    ~Census() { --alive; }
};

int main() {
    using tamcpp::ministl::RingBuffer;

    RingBuffer<int, 8> rb;
    for (int i = 1; i <= 13; ++i) {
        rb.push(i); // 1..5 进来就被后面挤掉了
    }
    std::printf("size=%zu front=%d (期望 6)\n", rb.size(), rb.front());
    std::printf("出队顺序:");
    while (!rb.empty()) {
        std::printf(" %d", rb.front());
        rb.pop();
    }
    std::printf("\n");

    {
        RingBuffer<Census, 8> c;
        for (int i = 0; i < 13; ++i) {
            c.push(Census{i});
        }
        std::printf("压入 13 个 Census,活着 %d 个(期望 8)\n", Census::alive);
        for (int i = 0; i < 3; ++i) {
            c.pop();
        }
        std::printf("出队 3 个,活着 %d 个(期望 5),front 的 tag=%d(期望 8)\n", Census::alive,
                    c.front().tag);
        std::printf("逻辑序号 3 上的 tag=%d(期望 11)\n", c.visit_at(3).tag);
    }
    std::printf("作用域结束,活着 %d 个(期望 0)\n", Census::alive);
    return 0;
}
```

In our first round, we use `int` to test the order, pushing 1 to 13 and then dequeuing all at once, the real output is:

```text
size=8 front=6 (期望 6)
出队顺序: 6 7 8 9 10 11 12 13
```

Looking at the results of this round: 13 elements come in, and the surviving ones are 6 to 13, the earlier ones leave first, and the order is not messed up at all, and `front` is exactly the sixth element to come in. In the second round, we switch to Census:

```text
压入 13 个 Census,活着 8 个(期望 8)
出队 3 个,活着 5 个(期望 5),front 的 tag=8(期望 8)
逻辑序号 3 上的 tag=11(期望 11)
作用域结束,活着 0 个(期望 0)
```

We will match the three numbers: 5 elements overwritten, 3 dequeued, and 5 destructed at the end of the scope. Each death has a corresponding birth, and 5 plus 3 plus 5 is exactly 13, all matching. The two lines of `tag` verify the access, and we count: after popping 3 elements, `front` is the element with `tag` 8, which is the ninth element to come in, and `visit_at(3)` counts three more from it.

## Destruction Wrapping Around, Continuous Interval Assumption Fails

The `DestroySources(begin, end)` from stage1 has an implicit assumption that the living elements are a continuous segment. In the world of `Vector`, this is always true, but in the ring world, it is not: the living elements wrap from the end of the array back to the beginning, and physically, they are two segments. We will do an independent small experiment, simulating the state after pushing 10 and dequeuing 7, with `read_pos = 7` and `write_pos = 10`, and the living elements are spread across physical indices 7, 0, and 1:

```cpp
// 错误示范:把 [0, size) 当连续区间交出去
// 它走到的是物理 0、1、2 三格:2 号格从未出生,7 号格不在区间里
DestroySources(ring, ring + static_cast<std::size_t>(write_pos - read_pos));
std::printf("连续区间版走完,活着 %d 个(数字碰巧对了)\n", Census::alive);

ring[7].~Census(); // 7 号格里的对象还活着,补杀一次,真相现形
std::printf("补杀 7 号格,活着 %d 个\n", Census::alive);
```

```text
活着 3 个,物理位置在 7、0、1 三格
连续区间版走完,活着 0 个(数字碰巧对了)
补杀 7 号格,活着 -1 个:绕回的半圈被漏掉了
```

This output is worth looking at closely. The count 0 seems balanced, but the source is all wrong: `DestroySources` goes through physical indices 0, 1, and 2. Objects in 0 and 1 have died, and index 2 has never had an object born. Calling destructors on unconstructed memory is undefined behavior. The living object in index 7 was never touched. One missed kill and one wrong kill, the counts cancel out to 0. `Census` is just a plain type that counts, and this time the wrong destructors didn't trigger immediately. But if the elements are changed to types that hold resources, the missed half-circle would be a leak, and the wrong kill would be a double release. Killing index 7, the count drops to -1, and the missed part finally appears.

The correct version is in the destructor above, following the logical sequence from `read_pos` around to `write_pos`, going through the whole circle, and each living element is destructed exactly once. Copying follows the same idea, and when you complete the five pieces, pay attention: copy from the oldest to the newest in logical sequence, and the new buffer's `read_pos` starts from 0. Directly `memcpy`ing physical memory would copy the dead slots as well, and the other object's wraparound state would be copied over, and the two pointers would not match. Moving is even easier, the pointers and two sequence numbers are taken over, the other side is cleared, and marked `noexcept`. Why mark it, the real answer is in the `move_if_noexcept` test in section 03, and we won't go into detail.

## Death Test Cases and Compile-Time Interception

Death test cases follow the old method from stage1, and we will pop an empty buffer, the correct outcome is a crash on the spot. Replace `main` with the following three lines, save it as `ring_buffer_death.cpp`, and recompile and run it with the previous type definitions, the real output is:

```cpp
int main() {
    tamcpp::ministl::RingBuffer<int, 8> rb;
    rb.pop(); // 空缓冲上出队,Check 当场收场
}
```

```text
[TAMCPP Check Crash]ring_buffer_death.cpp-66(void tamcpp::ministl::RingBuffer<Sources, N>::pop() [with Sources = int; long unsigned int N = 8]): pop on empty RingBuffer
```

The process ends with SIGABRT, exit code 134. If the program can still print "running here," the check is fake. ctest treats all child processes that die from signals as negative, so we padded the judge script `expect_check_death.sh` in stage1, and the engineering version follows the same process.

We will also show you the compile-time interception, with N set to 6:

```text
error: static assertion failed: N must be a power of two
   27 |     static_assert(N != 0 && (N & (N - 1)) == 0, "N must be a power of two");
      |                             ~~~~~~~~~~~~~~^~~~
  • the comparison reduces to '(4 == 0)'
```

GCC even shows you the result of `N & (N - 1)` when N is 6, which is 4. Compile-time interception is earlier than runtime crashes, and the error doesn't even start the process.

At this point, the single-file version of `RingBuffer` can enter and exit, cover and wrap around, and all the acceptance numbers match. The engineering version will fall into `stage2_ring_buffer/`, complete the five pieces, and the black-box test will go through everything again independently. The subsequent `CircularDeque` will require variable capacity and iterators, and the "sacrificing one slot" method for determining fullness is waiting for us there, and the ring migration technique will follow closely.

## Build and Reproduction

```bash
# 单文件教学版,本篇三段代码按顺序拼成 ring_buffer_demo.cpp
g++ -std=c++20 ring_buffer_demo.cpp -o ring_buffer_demo
./ring_buffer_demo                          # FIFO 绕回 + Census 配平
# 死法用例:main 换成上面那三行重新编译
g++ -std=c++20 ring_buffer_death.cpp -o ring_buffer_death
./ring_buffer_death                         # 期望 Check 崩溃,退出码 134
# 析构绕圈实验
g++ -std=c++20 destroy_wrap_demo.cpp -o destroy_wrap_demo && ./destroy_wrap_demo
# 汇编实验
g++ -std=c++20 -O2 -S mod.cpp -o mod.s      # 三种取模的机器码都在 mod.s 里
```

The accompanying engineering directory `code/volumn_codes/vol8-labs/ministl/stage2_ring_buffer/` has already been set up, and the construction manual `HANDBOOK.md` is in place. The engineering version header file `include/tamcpp_ministl/ring_buffer.hpp` is waiting for the author to craft it according to the manual.

## References

- Accompanying code: `code/volumn_codes/vol8-labs/ministl/stage2_ring_buffer/`
- [Chromium `base/containers/ring_buffer.h`](https://chromium.googlesource.com/chromium/src/+/main/base/containers/ring_buffer.h)(this article's mirror, 139 lines, the original comment is above `IsFilledIndexByBufferIndex`)
- [cppreference:`std::queue::pop`](https://en.cppreference.com/w/cpp/container/queue/pop)(same decision to return `void`)
- [Agner Fog Instruction Tables](https://www.agner.org/optimize/instruction_tables.pdf)(source of division latency, cross-check with [uops.info](https://uops.info/))
- [mini STL in Action (1): RawBuffer, Only Capacity, Not Objects](01-raw-buffer.md)(origin of `Check` and `DestroySources`)
- [mini STL in Action (3): Vector, the Five Pieces, Exception Safety, and Concepts](03-vector-copy-move-and-concepts.md)(complete discussion of `noexcept` and the five pieces)
