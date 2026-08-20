---
title: "卷八 · 领域应用 课后练习参考答案（Homework）"
description: "卷八课后练习的逐题详细解答：21 道题（七个子领域 + 跨领域综合与 L5 挑战）每题给出解题思路、逐步解答（每步标注知识点链接回教材章节）与真实验证输出。所有命令与输出在 WSL Arch（g++ 16.1.1 / clang++ 22.1.8）真实运行得到；编译错误类题目的报错为真实节选（注明截断）。"
chapter: 8
order: 2
tags:
  - host
  - intermediate
  - cpp-modern
  - 嵌入式
  - 网络编程
  - 状态机
  - 循环缓冲区
  - 智能指针
  - 模板
difficulty: intermediate
platform: host
cpp_standard: [17, 20, 23]
reading_time_minutes: 91
prerequisites: []
related: []
---

# 卷八 · 领域应用 课后练习参考答案（Homework）

> 所有命令与输出在 WSL Arch（g++ 16.1.1 / clang++ 22.1.8）真实运行得到。UB 类题目的输出「只是这台机器这次的选择」，换编译器/优化级别可能不同——这正是每道题要你体会的东西。编译错误类输出为真实报错**节选**（注明已截断），ASan 报告的 shadow 字节图按惯例省略。8.1-C 的发现请特别注意：**旧版教材的环形缓冲区满判定存在真实缺陷**（当年的本机实测揭出，现行第 37 篇已修订），解答里如实给出复现与修复。

## 8.1-A {#hw-8-1-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-8-1-a)

**思路**：置位用 `|=`、清位用 `&= ~`、翻转用 `^=`，三者都是「读-改-写」的位运算三件套；进阶自测里**只有 `1 << 32` 是 UB**——`1 << 31` 在 C++20（本套默认标准）里良定义，本机 sanitizer 不报它是正确行为；「工具盲区」的讨论要回到 C++11 的老规则口径下才成立（见步骤 2 与实测矩阵）。

1. 手算链：`0x1C00 | 0x2000 = 0x3C00`；`0x3C00 & ~0x0400 = 0x3800`；翻转 bit11（`0x0800`）两次回到 `0x3800`；bit13 读回 `(0x3800 >> 13) & 1 = 1`。→ 知识点：[第 11 篇](../embedded/01-led/06-hal-gpio-output.md)（`HAL_GPIO_WritePin` 背后的寄存器位操作）、[类型安全的寄存器访问](../embedded/02-type-safe-register-access.md)（置位/清位封装 `set_bits`/`clear_bits` 一节）
2. `1 << n` 要分两条看。`n=32`：移位位数等于位宽，**任何标准下都是 UB**，UBSan 报 `shift exponent 32 is too large` 报得对。`n=31`：C++20 的移位规则把结果定义为「与 $1×2^{31}$ 模 2^32 同余的唯一可表示值」——two's complement 下就是 INT_MIN（-2147483648），**良定义**；C++17 同样良定义（CWG 1457 之后的措辞：$2^{31}$ 可被对应无符号类型 `unsigned int` 表示，该值经**实现定义转换**得 INT_MIN）。所以 UBSan 对 `1 << 31` 一声不吭（普通构建输出 `a=-2147483648`）**不是漏报，是正确行为**。真盲区要回 C++11：发布版老规则要求「$E1×2^{E2}$ 可被**结果类型**（`int`）表示」，$2^{31}$ 超上限，按 C++11 原文 `1 << 31` 是 UB——但 `-std=c++11` 下两个编译器的 shift sanitizer 照样不报它（见下方矩阵；CWG 1457 是 DR，编译器在所有模式下都按修复后的规则实现，所以连编译产物都一致）。**别把 sanitizer 绿当安全证明，也别把标准条款当摆设**。→ 知识点：[第 11 篇](../embedded/01-led/06-hal-gpio-output.md)（`GPIO_PIN_13` 这类掩码全是无符号常量，`1u << 13` 的 `u` 就是在躲这个坑）

**代码**：

```cpp
#include <cstdint>
#include <cstdio>

int main() {
    std::uint32_t odr = 0x1C00u;   // simulated GPIO ODR: pins 10/11/12 high, pin13 low
    std::printf("init        odr = 0x%08X\n", odr);
    odr |= (1u << 13);             // set bit 13 (PC13)
    std::printf("set  bit13  odr = 0x%08X\n", odr);
    odr &= ~(1u << 10);            // clear bit 10
    std::printf("clr  bit10  odr = 0x%08X\n", odr);
    odr ^= (1u << 11);             // toggle bit 11 twice -> unchanged
    odr ^= (1u << 11);
    std::printf("tgl  bit11x2 odr = 0x%08X\n", odr);
    std::printf("bit13 now = %u\n", (odr >> 13) & 1u);
    return 0;
}
```

```cpp
#include <cstdio>

int main() {
    volatile int n = 31;
    int a = 1 << n;        // C++20/C++17: well-defined (=INT_MIN); C++11 wording: UB
    n = 32;
    int b = 1 << n;        // UB in every standard: exponent >= width
    std::printf("a=%d b=%d\n", a, b);
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra reg_bits.cpp -o reg_bits && ./reg_bits
init        odr = 0x00001C00
set  bit13  odr = 0x00003C00
clr  bit10  odr = 0x00003800
tgl  bit11x2 odr = 0x00003800
bit13 now = 1

$ g++ -std=c++20 -Wall -Wextra shift_ub.cpp -o shift_ub_plain && ./shift_ub_plain
a=-2147483648 b=1
$ g++ -std=c++20 -Wall -Wextra -fsanitize=undefined shift_ub.cpp -o shift_ub && ./shift_ub
shift_ub.cpp:7:15: runtime error: shift exponent 32 is too large for 32-bit type 'int'
a=-2147483648 b=1
$ clang++ -std=c++20 -Wall -Wextra -fsanitize=undefined shift_ub.cpp -o shift_ub_clang && ./shift_ub_clang
shift_ub.cpp:7:15: runtime error: shift exponent 32 is too large for 32-bit type 'int'
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior shift_ub.cpp:7:15
a=-2147483648 b=1
$ g++ -std=c++17 -Wall -Wextra -fsanitize=undefined shift_ub.cpp -o shift_ub17 && ./shift_ub17
shift_ub.cpp:7:15: runtime error: shift exponent 32 is too large for 32-bit type 'int'
a=-2147483648 b=1
$ clang++ -std=c++17 -Wall -Wextra -fsanitize=undefined shift_ub.cpp -o shift_ub17c && ./shift_ub17c
shift_ub.cpp:7:15: runtime error: shift exponent 32 is too large for 32-bit type 'int'
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior shift_ub.cpp:7:15
a=-2147483648 b=1
$ g++ -std=c++11 -Wall -Wextra -fsanitize=undefined shift_ub.cpp -o shift_ub11 && ./shift_ub11
shift_ub.cpp:7:15: runtime error: shift exponent 32 is too large for 32-bit type 'int'
a=-2147483648 b=1
$ clang++ -std=c++11 -Wall -Wextra -fsanitize=undefined shift_ub.cpp -o shift_ub11c && ./shift_ub11c
shift_ub.cpp:7:15: runtime error: shift exponent 32 is too large for 32-bit type 'int'
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior shift_ub.cpp:7:15
a=-2147483648 b=1
```

三个标准口径下 6 份 sanitizer 输出**逐字相同**：唯一被报的永远是 `1 << 32`（7:15 这一行）；`1 << 31` 在任何 `-std` 下都静默通过——在 c++20/c++17 这是正确行为（它确实良定义），在 c++11 老规则口径下才是「UB 存在、工具不报」的真盲区。

要点：`1u << 13` 里的 `u` 不是装饰——它把运算搬进无符号域，避开了符号位陷阱。`1 << 31` 这单事则说明：sanitizer 的检查口径跟着「编译器实际实现的规则」走（CWG 1457 是 DR，在所有模式生效），而不是跟着你 `-std=` 声称的标准版本走——**别把 sanitizer 绿当安全证明，也别把标准条款当摆设**。

## 8.1-B {#hw-8-1-b}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-8-1-b)

**思路**：非阻塞消抖的精髓是「记录变化时刻、而不是停下等」——每次原始值变化就重置计时器，只有连续 20ms 不变才确认。无符号减法的模运算性质天然处理 32 位 tick 溢出。

1. 核心三行：`sample != last_raw_` 时更新 `last_raw_` 并重置 `last_change_`；`(now - last_change_) >= 20` 且 `last_raw_ != stable_` 时确认转换。→ 知识点：[第 24 篇](../embedded/02-button/06-non-blocking-debounce.md)「非阻塞消抖算法」一节（记录变化时间，检查是否稳定了足够长时间）
2. 跟踪序列：t=11 是最后一次反弹，此后原始值保持 1 直到 t=31（差值 20 恰好到窗口）——所以 PRESSED 落在 t=31；释放方向同理，t=63 之后稳定到 t=85 确认 RELEASED。中间每次反弹都把计时器打回零，这正是「抖动的 5~20ms 跳变被计时器不断重置过滤掉」的机制。→ 知识点：[第 24 篇](../embedded/02-button/06-non-blocking-debounce.md)「非阻塞消抖算法」一节、[第 25 篇](../embedded/02-button/07-debounce-state-machine.md)（状态机版把同样的判断顺序写成了 `DebouncingPress` 三个分支）
3. 溢出验证：`0x10 - 0xFFFFFFF8` 在无符号域等于 24，不受 49.7 天回绕影响——所以第 24 篇说「你不需要担心溢出问题」。注意输出里 `[t=  16] stable -> PRESSED` 就是这次溢出测试自己确认的转换（now=0x10=16，差值 24≥20）。→ 知识点：[第 24 篇](../embedded/02-button/06-non-blocking-debounce.md)「溢出的安全性」一节

**代码**：

```cpp
#include <cstdint>
#include <cstdio>

struct Debouncer {
    static constexpr std::uint32_t kDebounceMs = 20;
    std::uint8_t last_raw = 0;      // last raw sample
    std::uint8_t stable = 0;        // confirmed stable level
    std::uint32_t last_change = 0;  // timestamp of last raw change

    void feed(std::uint8_t sample, std::uint32_t now) {
        if (sample != last_raw) {   // raw signal changed -> restart the clock
            last_raw = sample;
            last_change = now;
        }
        if ((now - last_change) >= kDebounceMs && last_raw != stable) {
            stable = last_raw;      // stable for 20ms -> confirm transition
            std::printf("[t=%4u] stable -> %s\n", now, stable ? "PRESSED" : "RELEASED");
        }
    }
};

int main() {
    Debouncer d;
    const struct { std::uint32_t t; std::uint8_t s; } trace[] = {
        {0, 0}, {1, 1}, {3, 0}, {5, 1}, {9, 0}, {11, 1}, {31, 1}, {60, 0}, {61, 1}, {63, 0}, {85, 0}
    };
    for (const auto& e : trace) d.feed(e.s, e.t);

    Debouncer w;
    w.last_raw = 1; w.stable = 0; w.last_change = 0xFFFFFFF8u;
    w.feed(1, 0x10u);   // now ticks past the wrap -> delta still computes correctly
    std::printf("wrap-around: delta=%u stable=%u\n", 0x10u - 0xFFFFFFF8u, w.stable);
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra debounce.cpp -o debounce && ./debounce
[t=  31] stable -> PRESSED
[t=  85] stable -> RELEASED
[t=  16] stable -> PRESSED
wrap-around: delta=24 stable=1
```

第三行 `[t= 16]` 来自溢出测试：`last_change = 0xFFFFFFF8`、`now = 0x10`，差值 24 毫秒达到窗口，稳定电平确认从 0 翻到 1——溢出前后的时间差照样算得对。

## 8.1-C {#hw-8-1-c}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-8-1-c)

**思路**：旧版教材（现行版已修复，此处审查的是修复前的实现）的 `next(v) = (v+1) & (2N-1)` 把 `head_`/`tail_` 放进 `0..2N-1` 的定义域，但数组下标用的是 `mask(v) = v & (N-1)` 的值域 `0..N-1`——两个域不一致，`full()` 的判定窗口因此放大了两倍，容量承诺失守。先复现，再定位，再修复。

1. **复现**：照旧版实现，N=8 时 push 10 个全部 accepted、`size()` 报到 10，drain 出来是 `8 9 2 3 4 5 6 7 8 9`——`0` 和 `1` 消失了、`8` 和 `9` 出现了两次。→ 知识点：[第 37 篇](../embedded/03-uart/07-circular-buffer-lock-free-spsc.md)（旧版实现与「最多存 N-1 个」的表述；现行版已改用索引差判定）
2. **定位**：`full()` 判的是 `next(head_) == tail_`（在 0..2N-1 域内），只有 `head_` 绕到 $2N-1$ 时才可能等于 `tail_=0`；而数组只有 N 个槽——`head_=8` 时 `mask(8)=0` 就已经回到槽 0，**在 `full()` 生效之前**就把 `tail_` 还没读走的数据覆盖了。旧版教材说「留一个位置不写」，代码却没有真正把那个位置留出来。→ 知识点：[循环缓冲区](../embedded/03-circular-buffer.md)「如何区分"空"和"满"？（经典难题）」一节（留一槽区分空满的初衷）
3. **修复**：head/tail 改成**单调递增**的 `size_t`（不加 $2N-1$ 掩码），只在数组下标处 `& (N-1)`；`full()` 直接判 `head_ - tail_ == N - 1`。→ 知识点：[第 37 篇](../embedded/03-uart/07-circular-buffer-lock-free-spsc.md)「2 的幂技巧」一节（位与替代取模的思想保留，但定义域回到自然数）
4. 回绕安全性：无符号整数的模运算保证 `head_ - tail_` 在计数器回绕后依然正确（$2^{64}$ 次 push 才回绕一次，永不到达）——和第 8.1-B 的时间戳是同一个数学事实。→ 知识点：[第 24 篇](../embedded/02-button/06-non-blocking-debounce.md)「溢出的安全性」一节（同一条无符号减法规律）

**代码**（实现 A 照抄旧版教材方案，实现 B 是修复版）：

```cpp
#include <array>
#include <cstddef>
#include <cstdio>

// implementation A: exactly the next()/mask() scheme from the old (pre-fix) chapter
template <std::size_t N>
class TextbookRing {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of 2");
public:
    bool push(std::byte b) noexcept {
        if (full()) return false;
        buf_[mask(head_)] = b;
        head_ = next(head_);
        return true;
    }
    bool pop(std::byte& out) noexcept {
        if (empty()) return false;
        out = buf_[mask(tail_)];
        tail_ = next(tail_);
        return true;
    }
    bool empty() const noexcept { return head_ == tail_; }
    bool full()  const noexcept { return next(head_) == tail_; }
    std::size_t size() const noexcept {
        return head_ >= tail_ ? head_ - tail_ : N - tail_ + head_;
    }
private:
    static constexpr std::size_t mask(std::size_t v) noexcept { return v & (N - 1); }
    static constexpr std::size_t next(std::size_t v) noexcept { return (v + 1) & (2 * N - 1); }
    std::array<std::byte, N> buf_{};
    std::size_t head_ = 0, tail_ = 0;
};

// implementation B: monotonic counters, mask only on the array index
template <std::size_t N>
class FixedRing {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of 2");
public:
    bool push(std::byte b) noexcept {
        if (full()) { ++dropped_; return false; }
        buf_[head_ & (N - 1)] = b;
        ++head_;
        return true;
    }
    bool pop(std::byte& out) noexcept {
        if (empty()) return false;
        out = buf_[tail_ & (N - 1)];
        ++tail_;
        return true;
    }
    bool empty() const noexcept { return head_ == tail_; }
    bool full()  const noexcept { return head_ - tail_ == N - 1; }
    std::size_t size() const noexcept { return head_ - tail_; }
    std::size_t dropped() const noexcept { return dropped_; }
private:
    std::array<std::byte, N> buf_{};
    std::size_t head_ = 0, tail_ = 0, dropped_ = 0;
};

int main() {
    std::printf("== implementation A (chapter scheme), N=8 ==\n");
    TextbookRing<8> t;
    int accepted = 0;
    for (int i = 0; i < 10; ++i) if (t.push(static_cast<std::byte>(i))) ++accepted;
    std::printf("pushed 10, accepted=%d, size()=%zu (chapter claims capacity N-1=7)\n",
                accepted, t.size());
    std::byte b;
    std::printf("drain: ");
    while (t.pop(b)) std::printf("%u ", static_cast<unsigned>(b));
    std::printf("\n");

    std::printf("== implementation B (fixed), N=8 ==\n");
    FixedRing<8> f;
    for (int i = 0; i < 10; ++i) {
        bool r = f.push(static_cast<std::byte>(i));
        std::printf("push %2d: %-4s size=%zu\n", i, r ? "ok" : "FULL", f.size());
    }
    std::printf("dropped=%zu\n", f.dropped());
    std::printf("pop 3: ");
    for (int i = 0; i < 3; ++i) { f.pop(b); std::printf("%u ", static_cast<unsigned>(b)); }
    std::printf("\n");
    for (int i = 100; i < 105; ++i) f.push(static_cast<std::byte>(i));
    std::printf("after refill: size=%zu dropped=%zu\n", f.size(), f.dropped());
    std::printf("drain: ");
    while (f.pop(b)) std::printf("%u ", static_cast<unsigned>(b));
    std::printf("\n");
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra ring_bug.cpp -o ring_bug && ./ring_bug
== implementation A (chapter scheme), N=8 ==
pushed 10, accepted=10, size()=10 (chapter claims capacity N-1=7)
drain: 8 9 2 3 4 5 6 7 8 9
== implementation B (fixed), N=8 ==
push  0: ok   size=1
push  1: ok   size=2
push  2: ok   size=3
push  3: ok   size=4
push  4: ok   size=5
push  5: ok   size=6
push  6: ok   size=7
push  7: FULL size=7
push  8: FULL size=7
push  9: FULL size=7
dropped=3
pop 3: 0 1 2
after refill: size=7 dropped=5
drain: 3 4 5 6 100 101 102
```

实现 A 的 drain 里 `0`、`1` 被静默覆盖成了 `8`、`9`——这如果发生在 UART 接收路径上，就是「丢字节还不出错」的最阴险形态。实现 B 容量恰好 7、满时拒绝并计数（`dropped=5` = 前 3 个 FULL + 补推 2 个 FULL）、环绕路径 drain 顺序 `3 4 5 6 100 101 102` 完全正确。**说明**：本练习集全部环形缓冲区（8.C-3 的 L5 也是）都按实现 B 的「单调计数器」方案写。当年这道题的实测揪出了旧版教材的满判定缺陷，现行第 37 篇已改用「索引差判定」（容量 N）；实现 B 的单调计数器（容量 N-1）与它是同样成立的两种修法——当初的选择是本机实测逼出来的，不是凭空改动。

## 8.1-D {#hw-8-1-d}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-8-1-d)

**思路**：编码是「每个 bit 一个坑」；解码的关键在 16x 过采样——起始边沿迟到 2 个 tick 后，第 i 个 bit 的中心位于 $2 + 8 + 16\times (i+1)$，在中心左右各取 1 个 tick 做三取二多数表决，把采样抖动挡在门外。

1. 编码 `0x41`：LSB 在前，`d0..d7 = 1,0,0,0,0,0,1,0`，帧为 `0 10000010 1` 即 `0100000101`——和手算一致，也对应教材「先发 bit0」的表述。→ 知识点：[第 32 篇](../embedded/03-uart/02-uart-protocol-basics.md)「数据位（Data Bits）」一节（LSB 先行）
2. 过采样：起始边沿迟到 2 tick 模拟时钟偏差；bit 中心 = $2 + 8 + 16\times k$（半个 bit = 8 tick）。多数表决让单个 tick 的噪声/抖动改变不了判决。→ 知识点：[第 32 篇](../embedded/03-uart/02-uart-protocol-basics.md)「过采样：接收方如何找到 bit 的中心」一节
3. 帧错误检测：起始位必须 0、停止位必须 1。把停止位中心的 3 个采样点都改成 0 后，多数表决结果为 0，解码器报 `framing_error=1`。注意只改 1 个采样点是不行的——多数表决会把它当噪声滤掉，这正是「3 取 2」的设计意图。→ 知识点：[第 32 篇](../embedded/03-uart/02-uart-protocol-basics.md)「起始位（Start Bit）」「停止位（Stop Bit）」两节

**代码**：

```cpp
#include <array>
#include <cstdint>
#include <cstdio>

std::array<std::uint8_t, 10> encode_8n1(std::uint8_t byte) {
    std::array<std::uint8_t, 10> frame{};
    frame[0] = 0;                                   // start bit (low)
    for (int i = 0; i < 8; ++i) frame[1 + i] = (byte >> i) & 1u;  // d0..d7
    frame[9] = 1;                                   // stop bit (high)
    return frame;
}

std::array<std::uint8_t, 162> oversample(const std::array<std::uint8_t, 10>& frame) {
    std::array<std::uint8_t, 162> stream{};
    int pos = 2;                                    // sampling edge arrives 2 ticks late
    for (int b = 0; b < 10; ++b)
        for (int k = 0; k < 16; ++k) stream[pos++] = frame[b];
    return stream;
}

struct DecodeResult { std::uint8_t value = 0; bool ok = false; bool framing_error = false; };

DecodeResult decode_8n1(const std::array<std::uint8_t, 162>& stream) {
    auto sample = [&](int center) {   // majority vote over 3 ticks at the bit center
        int ones = 0;
        for (int k = -1; k <= 1; ++k) ones += stream[center + k];
        return ones >= 2 ? 1 : 0;
    };
    DecodeResult r{};
    int tick = 2 + 8;                 // start-bit center: edge offset + half a bit
    if (sample(tick) != 0) { r.framing_error = true; return r; }      // start bit must be low
    std::uint8_t v = 0;
    for (int i = 0; i < 8; ++i) {
        tick += 16;
        if (sample(tick)) v |= static_cast<std::uint8_t>(1u << i);
    }
    tick += 16;
    if (sample(tick) != 1) { r.framing_error = true; return r; }      // stop bit must be high
    r.value = v; r.ok = true;
    return r;
}

int main() {
    const std::uint8_t byte = 0x41;   // 'A' = 0b01000001
    auto frame = encode_8n1(byte);
    std::printf("frame for 0x%02X: ", byte);
    for (int i = 0; i < 10; ++i) std::printf("%u", frame[i]);
    std::printf("   (start d0..d7 stop)\n");

    auto stream = oversample(frame);
    auto r = decode_8n1(stream);
    std::printf("decode: ok=%d value=0x%02X ('%c') framing_error=%d\n",
                r.ok, r.value, r.value, r.framing_error);

    auto bad = stream;
    const int stop_center = 2 + 16 * 9 + 8;   // force the whole stop bit low
    bad[stop_center - 1] = 0; bad[stop_center] = 0; bad[stop_center + 1] = 0;
    auto r2 = decode_8n1(bad);
    std::printf("corrupted stop bit: ok=%d framing_error=%d\n", r2.ok, r2.framing_error);
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra uart_codec.cpp -o uart_codec && ./uart_codec
frame for 0x41: 0100000101   (start d0..d7 stop)
decode: ok=1 value=0x41 ('A') framing_error=0
corrupted stop bit: ok=0 framing_error=1
```

## 8.2-A {#hw-8-2-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-8-2-a)

**思路**：字节序的本质是「多字节整数在内存里的摆放顺序」；`htons`/`htonl` 把主机序统一成网络序（大端），小端主机上翻转、大端主机上空操作，所以「无论什么 CPU 都对」。

1. `13013 = 0x32D5`；小端主机上裸内存是 `D5 32`（低字节在前），`htons` 之后变成 `32 D5`——大端序。`htonl(0x7F000001)` 后内存为 `7F 00 00 01`，恰好和「127.0.0.1 点分十进制的自然顺序」一致（这也是 `s_addr` 塞进 `sockaddr_in` 前必须 `htonl` 的原因）。往返 `ntohs(htons(x)) == x` 恒真。→ 知识点：[00 · 传统 socket 编程](../networking/00-traditional-socket-basics.md)「bind() 之前停下来：字节序」一节
2. 判断题**错**。二进制文件直接拷贝时，大端机器读小端机器写的文件（或反过来）会把多字节字段整个读反；协议/存档字段必须在边界处做 `hton*`/`ntoh*` 转换（或干脆用定宽大端序列化，见 8.6-A）。→ 知识点：同上（「凡是塞进 `sockaddr_in` 的多字节整数，一律过一遍 htonl/htons」）
3. 因为 `htonl`/`htons` 在大端机器上是空操作、小端机器上翻转——写它，两种 CPU 的行为都被归一到网络序。→ 知识点：同上

**代码**：

```cpp
#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>

int main() {
    std::uint16_t port = 13013;              // 0x32D5
    std::uint32_t ip = 0x7F000001u;          // 127.0.0.1
    std::uint16_t np = htons(port);
    std::uint32_t nip = htonl(ip);
    std::printf("port=%u htons(port)=0x%04X ntohs(htons(port))=%u\n", port, np, ntohs(np));
    const auto* b = reinterpret_cast<const unsigned char*>(&np);
    std::printf("htons bytes in memory: %02X %02X\n", b[0], b[1]);
    const auto* q = reinterpret_cast<const unsigned char*>(&nip);
    std::printf("htonl bytes in memory: %02X %02X %02X %02X\n", q[0], q[1], q[2], q[3]);
    std::printf("ntohl(htonl(ip))==ip: %d\n", ntohl(nip) == ip);
    std::uint16_t x = 0x0001u;
    std::printf("host is little-endian: %d\n",
                *reinterpret_cast<unsigned char*>(&x) == 1);
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra byteorder.cpp -o byteorder && ./byteorder
port=13013 htons(port)=0xD532 ntohs(htons(port))=13013
htons bytes in memory: 32 D5
htonl bytes in memory: 7F 00 00 01
ntohl(htonl(ip))==ip: 1
host is little-endian: 1
```

## 8.2-B {#hw-8-2-b}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-8-2-b)

**思路**：RAII 把「记得释放」从程序员的脑子里挪进类型系统——资源绑定到栈对象上，构造获取、析构释放；fd 是独占资源所以禁拷贝只移动，moved-from 对象置成空（-1），析构看到空就什么都不做。

1. 作用域退出：`{ Fd f(3); }` 出块时析构必被调用，`active` 从 1 回 0——无论中间有没有提前 `return`。→ 知识点：[01 · 现代 socket 封装](../networking/01-modern-socket-wrapping.md)「RAII：让漏 close 变成不可能」一节
2. 移动语义：`Fd b = std::move(a);` 把 fd 偷过来、把 `a` 置空，任意时刻只有一个对象持有资源；`b.reset()` 手动释放恰好一次；随后 `a`、`c`（moved-from）的析构都安全地什么都不做，没有 double close。→ 知识点：同上（「-1 = 空状态」与移动构造的设计点）
3. 编译失败：拷贝构造被 `= delete`，`Fd b = a;` 报 `use of deleted function 'Fd::Fd(const Fd&)'`。→ 知识点：同上（「禁拷贝、只移动——因为 fd 是独占资源」）

**代码**：

```cpp
#include <cstdio>
#include <utility>

static int active = 0;   // simulates the kernel fd table

class Fd {
public:
    Fd() = default;
    explicit Fd(int fd) : fd_(fd) { ++active; std::printf("ctor: fd=%d active=%d\n", fd_, active); }
    ~Fd() { reset(); }

    Fd(const Fd&) = delete;             // exclusive resource: no copies
    Fd& operator=(const Fd&) = delete;

    Fd(Fd&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }      // steal, leave empty
    Fd& operator=(Fd&& o) noexcept {
        if (this != &o) { reset(); fd_ = o.fd_; o.fd_ = -1; }
        return *this;
    }

    void reset() {
        if (fd_ >= 0) { --active; std::printf("close: fd=%d active=%d\n", fd_, active); fd_ = -1; }
    }
    int get() const { return fd_; }
    explicit operator bool() const { return fd_ >= 0; }
private:
    int fd_ = -1;                        // -1 == empty; dtor of an empty Fd does nothing
};

int main() {
    std::printf("== 1) scope exit closes automatically ==\n");
    { Fd f(3); }
    std::printf("== 2) move transfers ownership, moved-from becomes empty ==\n");
    Fd a(4);
    Fd b = std::move(a);
    std::printf("after move: a.get()=%d (empty), b.get()=%d\n", a.get(), b.get());
    b.reset();
    std::printf("after manual reset: active=%d\n", active);
    std::printf("== 3) moved-from object destructs safely ==\n");
    Fd c(5);
    Fd d = std::move(c);   // c is now empty; its destructor will do nothing
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra raii.cpp -o raii && ./raii
== 1) scope exit closes automatically ==
ctor: fd=3 active=1
close: fd=3 active=0
== 2) move transfers ownership, moved-from becomes empty ==
ctor: fd=4 active=1
after move: a.get()=-1 (empty), b.get()=4
close: fd=4 active=0
after manual reset: active=0
== 3) moved-from object destructs safely ==
ctor: fd=5 active=1
close: fd=5 active=0

$ g++ -std=c++20 -Wall -Wextra raii_bad.cpp -o raii_bad
raii_bad.cpp: In function 'int main()':
raii_bad.cpp:12:12: error: use of deleted function 'Fd::Fd(const Fd&)'
   12 |     Fd b = a;   // copy: must not compile
      |            ^
raii_bad.cpp:4:5: note: declared here
    4 |     Fd(const Fd&) = delete;
      |     ^~
（节选：`note: use '-fdiagnostics-all-candidates' ...` 候选提示行与 `-Wunused-variable` 警告行已省略）
```

注意第 2 段里 `a.get()` 已经是 -1——移动是「偷」，不是「复制」；这也正是为什么教材说 fd 转交线程时要用 `std::move`。

## 8.2-C {#hw-8-2-c}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-8-2-c)

**思路**：LT 是「状态满足就一直就绪」，ET 是「只在边沿通知一次」。ET 下每个事件只 read 一次，剩下的数据没有新边沿来触发下一次通知，就被「遗忘」在缓冲区里；循环读到底（`-1/EAGAIN` 或 `0`/EOF 都收尾）是唯一正确的收尾姿势。

1. LT：8192 字节一次事件内循环 read 9 次——8 次读满 1024，**第 9 次 read 返回 0**（EOF）收尾：本实验写端在写完 8192 字节后立即 `close`，所以不是 EAGAIN 先到、而是 EOF 先到（实测第 9 次 `read=0`）。→ 知识点：[02 · epoll](../networking/02-epoll-io-multiplexing.md)「LT vs ET」「内核层的真正差异」两节
2. ET-once：事件只通知一次，只读走 1024 字节，200ms 超时后仍然没有新事件——**7168 字节卡死在缓冲区**。这就是教材里「100KB 丢 87KB」的 socketpair 版。→ 知识点：同上（「你必须在这一次通知里把数据读空」）
3. ET-loop：ET 模式 + 循环读到底（`read` 返回 `-1/EAGAIN` 或 `0` 都收尾），9 次 read 全量读空——和 LT 结果一致；本实验第 9 次同样是 `read` 返回 0（EOF，写端已 close）。读端必须 `O_NONBLOCK`：阻塞 fd 在「读空」的最后一刻不会返回 EAGAIN，而是**阻塞等下一段数据**，直接把事件循环卡死——本实验靠写端关断用 EOF 兜了底，真实连接上写端不关，收尾靠的就是 EAGAIN。→ 知识点：同上（「ET 的命门：非阻塞 + 循环读到 EAGAIN」）
4. 「最阴险的 bug」对应哪一行：`mode=ET-once bytes=1024` 这一行——如果发的只是 4KB 小消息（一次就读完），ET-once 照样全对；只有大 burst 才暴露。→ 知识点：同上（「这就是『别被测试骗了』的典型」）

**代码**：

```cpp
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

enum class Mode { Lt, EtOnce, EtLoop };

static long run_once(Mode m) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) { std::perror("socketpair"); return -1; }
    fcntl(sv[0], F_SETFL, fcntl(sv[0], F_GETFL, 0) | O_NONBLOCK);   // reader non-blocking
    int ep = epoll_create1(0);
    epoll_event ev{};
    ev.events = EPOLLIN;
    if (m != Mode::Lt) ev.events |= EPOLLET;
    ev.data.fd = sv[0];
    epoll_ctl(ep, EPOLL_CTL_ADD, sv[0], &ev);

    constexpr std::size_t kTotal = 8192;
    std::array<char, kTotal> wbuf{};
    std::memset(wbuf.data(), 'x', kTotal);
    std::size_t written = 0;
    while (written < kTotal) {
        ssize_t w = write(sv[1], wbuf.data() + written, kTotal - written);
        if (w < 0 && errno == EINTR) continue;
        written += static_cast<std::size_t>(w);
    }
    close(sv[1]);

    std::array<char, 1024> rbuf;
    std::size_t got = 0, events = 0, read_calls = 0;
    bool done = false;
    while (!done) {
        epoll_event evs[1];
        int n = epoll_wait(ep, evs, 1, 200);
        if (n == 0) break;                         // 200ms silence: ET read-once lost data
        if (n < 0) { if (errno == EINTR) continue; break; }
        ++events;
        for (;;) {
            ssize_t r = read(sv[0], rbuf.data(), rbuf.size());
            ++read_calls;
            if (r > 0) got += static_cast<std::size_t>(r);
            if (m == Mode::EtOnce) break;          // THE TRAP: read once per event
            if (r < 0 && errno == EAGAIN) { done = true; break; }  // drained
            if (r == 0) { done = true; break; }
        }
    }
    close(sv[0]);
    close(ep);
    std::printf("mode=%-7s bytes=%zu events=%zu read_calls=%zu\n",
                m == Mode::Lt ? "LT" : (m == Mode::EtOnce ? "ET-once" : "ET-loop"),
                got, events, read_calls);
    return static_cast<long>(got);
}

int main() {
    run_once(Mode::Lt);
    run_once(Mode::EtOnce);
    run_once(Mode::EtLoop);
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra epoll_pair.cpp -o epoll_pair && ./epoll_pair
mode=LT      bytes=8192 events=1 read_calls=9
mode=ET-once bytes=1024 events=1 read_calls=1
mode=ET-loop bytes=8192 events=1 read_calls=9
```

ET-once 丢了 $8192 - 1024 = 7168$ 字节，且 `events=1` 说明之后 200ms 内 epoll 再没通知过——数据不是「没了」，是「永远等不到下一次被处理」。

## 8.3-A {#hw-8-3-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-8-3-a)

**思路**：行主序就是「一行存完再存下一行」，换算公式 `offset = i * Cols + j`；因为底层是**一段**连续数组，行内和跨行其实都是连续的——「二维」只是外面那层壳的换算。

1. W(2, 1) → $2 \times 3 + 1 = 7$（第 2 行从下标 6 开始，第 1 列就是 7）。→ 知识点：[行主序](../ai/tiny_ml/stage1/04-row-major.md)「公式：i * Cols + j」一节
2. 打印出的 4×3 表格：第 0 行 0 1 2、第 1 行 3 4 5……与手算一一对应。→ 知识点：同上（「一行一行抄」的内存图）
3. 两个指针差都是 1：`&W[1][0] - &W[0][2]` 也是 1，因为第 1 行第 0 列在内存里就紧跟在第 0 行第 2 列后面——底层是连续数组，不是「12 个散布的格子」。这个约定和 NumPy 默认的 C order 一致，Stage 5 的 Python 权重才能和 C++ 的 W(i, j) 一位对一位地对拍。→ 知识点：同上、[固定维度 Tensor](../ai/tiny_ml/stage1/06-tensor.md)「决策三：std::array 存储 + 行主序」一节

**代码**：

```cpp
#include <array>
#include <cstddef>
#include <cstdio>

int main() {
    constexpr std::size_t kRows = 4, kCols = 3;
    std::array<float, kRows * kCols> w{};
    for (std::size_t i = 0; i < w.size(); ++i) w[i] = static_cast<float>(i);
    std::printf("flat storage: ");
    for (float v : w) std::printf("%.0f ", v);
    std::printf("\n4x3 table via W[i][j] = flat[i*%zu+j]:\n", kCols);
    for (std::size_t i = 0; i < kRows; ++i) {
        for (std::size_t j = 0; j < kCols; ++j) std::printf("%5.0f ", w[i * kCols + j]);
        std::printf("\n");
    }
    std::printf("W(2,1) -> flat offset %zu\n", 2 * kCols + 1);
    std::printf("same row contiguity:    &W[0][1]-&W[0][0] = %td\n",
                &w[0 * kCols + 1] - &w[0 * kCols]);
    std::printf("cross-row contiguity:   &W[1][0]-&W[0][2] = %td\n",
                &w[1 * kCols] - &w[0 * kCols + 2]);
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra rowmajor.cpp -o rowmajor && ./rowmajor
flat storage: 0 1 2 3 4 5 6 7 8 9 10 11
4x3 table via W[i][j] = flat[i*3+j]:
    0     1     2
    3     4     5
    6     7     8
    9    10    11
W(2,1) -> flat offset 7
same row contiguity:    &W[0][1]-&W[0][0] = 1
cross-row contiguity:   &W[1][0]-&W[0][2] = 1
```

## 8.3-B {#hw-8-3-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-8-3-b)

**思路**：热路径 `operator()` 返回引用不检查，带检查的 `at` 走 `std::expected` 的错误路径；`at` 不返回引用是因为标准直接堵死了 `expected<T&, E>`；`internals_{}` 的 `{}` 是值初始化的开关。

1. `at(99, 0)` 返回 `kOutOfRange`、`noexcept` 保证不抛异常——进程退出码 0。→ 知识点：[固定维度 Tensor](../ai/tiny_ml/stage1/06-tensor.md)「决策一：at 返回 std::expected 的值」、常见坑 1（越界检查用 `||` 不是 `&&`）
2. 默认构造零初始化来自 `internals_{}`：没有 `{}` 时 `std::array` 的元素是 indeterminate，t(0,0) 读未初始化垃圾（UB）——教材常见坑 2 里 msan 抓到的就是这个。→ 知识点：[固定维度 Tensor](../ai/tiny_ml/stage1/06-tensor.md)常见坑 2
3. 行主序地址恒等逐元素成立：`operator()` 只是 `internals_[i*Cols+j]` 的引用返回，没有第二个存储。→ 知识点：[行主序](../ai/tiny_ml/stage1/04-row-major.md)
4. `expected<float&, int>` 编译失败：libstdc++ 的 `<expected>` 第 372 行 `static_assert( ! is_reference_v<_Tp> );` 直接断言，随后还有「union 成员不得是引用类型」的连锁报错——这就是教材决策一的硬约束来源。→ 知识点：[固定维度 Tensor](../ai/tiny_ml/stage1/06-tensor.md)常见坑 3（「std::expected 不接引用类型」）

**代码**：

```cpp
#include <array>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <span>

template <std::size_t Rows, std::size_t Cols, typename StorageType = float>
class Tensor {
public:
    enum class Error { kOutOfRange };
    static_assert(Rows > 0 && Cols > 0, "dims must be positive");
    constexpr Tensor() = default;
    constexpr explicit Tensor(std::array<StorageType, Rows * Cols> internals)
        : internals_(internals) {}
    constexpr std::expected<StorageType, Error>
    at(std::size_t i, std::size_t j) const noexcept {
        if (i >= Rows || j >= Cols) return std::unexpected{Error::kOutOfRange};
        return internals_[i * Cols + j];
    }
    constexpr StorageType& operator()(std::size_t i, std::size_t j) noexcept { return internals_[i * Cols + j]; }
    constexpr const StorageType& operator()(std::size_t i, std::size_t j) const noexcept { return internals_[i * Cols + j]; }
    constexpr std::size_t row() const noexcept { return Rows; }
    constexpr std::size_t col() const noexcept { return Cols; }
    constexpr std::size_t size() const noexcept { return internals_.size(); }
    constexpr std::span<const StorageType, Rows * Cols> view() const noexcept { return internals_; }
    constexpr const std::array<StorageType, Rows * Cols>& storage() const noexcept { return internals_; }
private:
    std::array<StorageType, Rows * Cols> internals_{};   // value-init: zero for float
};

int main() {
    Tensor<2, 2> t(std::array{1.f, 2.f, 3.f, 4.f});
    std::printf("t(1,0)=%.0f size=%zu row=%zu col=%zu\n", t(1, 0), t.size(), t.row(), t.col());

    Tensor<2, 2> z;                                   // default construction
    std::printf("default-constructed: z(0,0)=%.0f\n", z(0, 0));

    auto r = t.at(99, 0);                             // out of range -> expected error path
    if (!r) std::printf("at(99,0) -> error path (no exception, process alive)\n");

    bool same = true;                                 // row-major address identity
    for (std::size_t i = 0; i < 2; ++i)
        for (std::size_t j = 0; j < 2; ++j)
            if (&t(i, j) != &t.storage()[i * 2 + j]) same = false;
    std::printf("row-major address identity: %s\n", same ? "true" : "false");

    std::printf("view() shares memory: %d\n", t.view().front() == t.storage()[0]);
    static_assert(Tensor<4, 3>{}.size() == 12, "compile-time size");
    std::printf("static_assert(Tensor<4,3>.size()==12) passed\n");
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++23 -Wall -Wextra tensor.cpp -o tensor && ./tensor
t(1,0)=3 size=4 row=2 col=2
default-constructed: z(0,0)=0
at(99,0) -> error path (no exception, process alive)
row-major address identity: true
view() shares memory: 1
static_assert(Tensor<4,3>.size()==12) passed
$ ./tensor; echo "exit=$?"
...（输出同上）
exit=0

$ g++ -std=c++23 expected_ref.cpp -o expected_ref   # at 返回 expected<float&, int>
In file included from expected_ref.cpp:1:
/usr/include/c++/16/expected: In instantiation of 'class std::expected<float&, int>':
expected_ref.cpp:8:65:   required from here
/usr/include/c++/16/expected:372:24: error: static assertion failed
  372 |       static_assert( ! is_reference_v<_Tp> );
      |                        ^~~~~~~~~~~~~~~~~~~
  '!(bool)std::is_reference_v<float&>' evaluates to false
/usr/include/c++/16/expected:1354:26: error: non-static data member 'std::expected<float&, int>::<unnamed union>::_M_val' in a union may not have reference type 'std::remove_cv_t<float&>' {aka 'float&'}
  1354 |         remove_cv_t<_Tp> _M_val;
      |                          ^~~~~~
（其余连锁报错已省略）
```

## 8.3-C {#hw-8-3-c}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-8-3-c)

**思路**：维度是模板参数 → 形状是类型的一部分 → 编译期就能对整个计算求值、对形状错误报编译错；运行时形状方案只能把错误拖到运行时再炸。

1. `constexpr` 化后 `static_assert(total(t) == 21.f)`、`static_assert(add(t,t)(0,1) == 4.f)` 都在编译期完成——程序运行只打印确认行，一次浮点运算都没做。→ 知识点：[形状塞进类型](../ai/tiny_ml/stage1/05-shape-in-type.md)（类型系统当免费的形状检查器）、[固定维度 Tensor](../ai/tiny_ml/stage1/06-tensor.md)（`constexpr` 成员让 `static_assert(t.size()==12)` 能过）
2. 形状失配 `add(Tensor<2,3>, Tensor<3,2>)` 编译失败，关键行 `deduced conflicting values for non-type parameter 'R' ('2' and '3')`——模板推导阶段就发现矛盾，错误信息精确指向形状。→ 知识点：[形状塞进类型](../ai/tiny_ml/stage1/05-shape-in-type.md)
3. `std::vector` 方案的形状是运行时的：`v.size()` 编译期不可知，形状错误只能在运行时以「越界、算错、崩溃」的形式出现；编译期方案的错误时机提前到了「还没运行」，暴露形式也从行为错误变成可读的编译诊断。→ 知识点：[形状塞进类型](../ai/tiny_ml/stage1/05-shape-in-type.md)、[为什么不用现成的](../ai/tiny_ml/stage1/03-why-not-built-in.md)（vector 候选死在哪几条硬约束上）

**代码**：

```cpp
#include <array>
#include <cstddef>
#include <cstdio>

template <std::size_t Rows, std::size_t Cols, typename StorageType = float>
class Tensor {
public:
    constexpr Tensor() = default;
    constexpr explicit Tensor(std::array<StorageType, Rows * Cols> a) : internals_(a) {}
    constexpr StorageType& operator()(std::size_t i, std::size_t j) noexcept { return internals_[i * Cols + j]; }
    constexpr const StorageType& operator()(std::size_t i, std::size_t j) const noexcept { return internals_[i * Cols + j]; }
    static constexpr std::size_t row() noexcept { return Rows; }
    static constexpr std::size_t col() noexcept { return Cols; }
    static constexpr std::size_t size() noexcept { return Rows * Cols; }
private:
    std::array<StorageType, Rows * Cols> internals_{};
};

template <std::size_t R, std::size_t C, typename S>
constexpr Tensor<R, C, S> add(const Tensor<R, C, S>& a, const Tensor<R, C, S>& b) {
    std::array<S, R * C> out{};
    for (std::size_t i = 0; i < R; ++i)
        for (std::size_t j = 0; j < C; ++j) out[i * C + j] = a(i, j) + b(i, j);
    return Tensor<R, C, S>(out);
}

template <std::size_t R, std::size_t C, typename S>
constexpr S total(const Tensor<R, C, S>& t) {
    S s{};
    for (std::size_t i = 0; i < R; ++i)
        for (std::size_t j = 0; j < C; ++j) s += t(i, j);
    return s;
}

int main() {
    constexpr Tensor<3, 2> t(std::array{1.f, 2.f, 3.f, 4.f, 5.f, 6.f});
    static_assert(t.size() == 6, "size is a compile-time constant");
    static_assert(total(t) == 21.f, "whole computation happens at compile time");
    constexpr auto s = add(t, t);
    static_assert(s(0, 1) == 4.f, "elementwise add is compile-time too");
    std::printf("all compile-time static_asserts passed\n");

    Tensor<2, 3> a(std::array{1.f, 2.f, 3.f, 4.f, 5.f, 6.f});
    Tensor<2, 3> b = a;
    auto c = add(a, b);
    std::printf("runtime add: c(1,2)=%.0f\n", c(1, 2));
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++23 -Wall -Wextra cexpr_tensor.cpp -o cexpr_tensor && ./cexpr_tensor
all compile-time static_asserts passed
runtime add: c(1,2)=12

$ g++ -std=c++23 shape_mismatch.cpp -o shape_mismatch
shape_mismatch.cpp: In function 'int main()':
shape_mismatch.cpp:21:17: error: no matching function for call to 'add(Tensor<2, 3>&, Tensor<3, 2>&)'
   21 |     auto c = add(a, b);   // 2x3 + 3x2: shape mismatch, must not compile
      |              ~~~^~~~~~
  there is 1 candidate
    candidate 1: 'template<long unsigned int R, long unsigned int C, class S> constexpr Tensor<R, C, S> add(const Tensor<R, C, S>&, const Tensor<R, C, S>&)'
      template argument deduction/substitution failed:
        deduced conflicting values for non-type parameter 'R' ('2' and '3')
（节选：候选函数签名后的声明位置行已省略）
```

## 8.4-A {#hw-8-4-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-8-4-a)

**思路**：四层语义模型一张表就能答完第①问；`Borrowed` 的两处编译失败背后是两道防线——临时对象被 `Borrowed(T&&) = delete` 拦（右值直接绑定被删重载），左值拷贝初始化被「explicit 出局 + 被删的 `T&&` 开火」拦（两版对照见步骤 3）。

1. 填空表：`T&` 不可空/不延长/不能判空；`T*` 可空/不延长/不能判空；`Borrowed<T>` 不可空（设计上）/不延长/不能判空；`ObserverPtr<T>` 可空/不延长/不能判空；`WeakPtr<T>` 可空/不延长/**能判空**；`std::weak_ptr<T>` 可空/`lock()` **临时延长**/**能判空**。→ 知识点：[非拥有指针全景](../cpp-deep-dives/pointer-semantics/01-non-owning-pointer-overview.md)「核心概念：四层语义模型」表格
2. `borrow(msg)`（`msg` 是 `const std::string`）构造 `Borrowed<const std::string>`，统计 `'l'` 出现 2 次。→ 知识点：同上「手搓 Borrowed」一节
3. 编译失败 ①（临时对象）：`Borrowed(T&&) = delete` 让「借用一个马上就销毁的临时」在编译期被拒——这是对 Rust 借用检查最接近的模拟。编译失败 ②（左值拷贝初始化 `= s`）：g++ 16 实际报 `use of deleted function 'Borrowed<T>::Borrowed(T&&)'`——**真正开火的不是「explicit 拒绝拷贝初始化」，而是被删的 `Borrowed(T&&)` 重载**：`= s` 的拷贝初始化语境里 explicit 的 `Borrowed(T&)` 直接出局，g++ 把剩下的转换构造 `Borrowed(T&&)`（T 已被类模板实参固定为 `std::string`，即 `Borrowed(std::string&&)`）当作最近似候选选中——选中即开火、被删即报错。只有**删掉** `Borrowed(T&&) = delete` 的版本才会轮到 explicit 挡路（报 `conversion from 'std::string' ... requested`，两版对照见下方输出）。→ 知识点：同上（「为什么构造函数是 explicit」）

**代码**：

```cpp
#include <cstdio>
#include <string>

template <typename T> class Borrowed {
public:
    explicit Borrowed(T& ref) noexcept : ptr_(&ref) {}
    Borrowed(T&&) = delete;             // cannot borrow a temporary
    Borrowed(std::nullptr_t) = delete;  // cannot borrow null
    T& get() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
private:
    T* ptr_;
};

template <typename T> Borrowed<T> borrow(T& ref) noexcept { return Borrowed<T>(ref); }

static int count_char(Borrowed<const std::string> s, char c) {
    int n = 0;
    for (char ch : s.get()) if (ch == c) ++n;
    return n;
}

int main() {
    const std::string msg = "hello borrowed";
    std::printf("count of 'l' = %d\n", count_char(borrow(msg), 'l'));
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra borrow.cpp -o borrow && ./borrow
count of 'l' = 2

$ g++ -std=c++20 -Wall -Wextra borrow_temp.cpp -o borrow_temp
borrow_temp.cpp: In function 'int main()':
borrow_temp.cpp:14:48: error: use of deleted function 'Borrowed<T>::Borrowed(T&&) [with T = std::__cxx11::basic_string<char>]'
   14 |     Borrowed<std::string> b(std::string("temp"));  // borrow a temporary: must not compile
      |                                                ^
borrow_temp.cpp:6:5: note: declared here
    6 |     Borrowed(T&&) = delete;             // cannot borrow a temporary
      |     ^~~~~~~~
（节选：`note: use '-fdiagnostics-all-candidates' ...` 候选提示行与 `-Wunused-variable` 警告行已省略）

$ g++ -std=c++20 -Wall -Wextra borrow_explicit.cpp -o borrow_explicit
borrow_explicit.cpp: In function 'int main()':
borrow_explicit.cpp:15:31: error: use of deleted function 'Borrowed<T>::Borrowed(T&&) [with T = std::__cxx11::basic_string<char>]'
   15 |     Borrowed<std::string> b = s;   // copy-init from lvalue: the deleted T&& fires
      |                               ^
borrow_explicit.cpp:6:5: note: declared here
    6 |     Borrowed(T&&) = delete;             // cannot borrow a temporary
      |     ^~~~~~~~
（节选：候选提示行与 `-Wunused-variable` 警告行已省略）

$ g++ -std=c++20 -Wall -Wextra borrow_explicit_nodelete.cpp -o borrow_explicit_nodelete
borrow_explicit_nodelete.cpp: In function 'int main()':
borrow_explicit_nodelete.cpp:14:31: error: conversion from 'std::string' {aka 'std::__cxx11::basic_string<char>'} to non-scalar type 'Borrowed<std::__cxx11::basic_string<char> >' requested
   14 |     Borrowed<std::string> b = s;   // copy-init from lvalue: explicit ctor is the only wall
      |                               ^
borrow_explicit_nodelete.cpp:5:14: note: explicit conversion function was not considered
    5 |     explicit Borrowed(T& ref) noexcept : ptr_(&ref) {}
      |              ^~~~~~~~
（节选：`-Wunused-variable` 警告行已省略）
```

对照两版：带 `Borrowed(T&&) = delete` 的公布代码（中段），`= s` 实际报的是 `use of deleted function 'Borrowed(T&&)'`——旧版文档贴的 `conversion from 'std::string' ... requested` 是**没有** `T&& = delete` 那版的报错（下段）。所以「explicit 拒绝拷贝初始化」在这里不是真正开火的机制：拷贝初始化语境里 explicit 的 `Borrowed(T&)` 直接出局（`-fdiagnostics-all-candidates` 里被标为 ignored），真正开火的是被删的 `Borrowed(T&&)` 转换构造——g++ 选中它、发现被删、报错点名。两处编译失败合起来才是 Borrowed 的完整防线：临时对象被 `T&& = delete` 拦，左值拷贝初始化被「explicit 出局 + 被删重载开火」拦。

## 8.4-B {#hw-8-4-b}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-8-4-b)

**思路**：`T* + raw Flag*` 的命门是 Flag 的生命周期绑在 Owner 上——`is_valid()` 解引用悬垂 `Flag*` 本身就是 UB；ASan 把「看起来能跑」当场抓包。`shared_ptr<Flag>` 让控制块独立于 Owner，读到的一定是活着的 Flag。

1. 普通构建：本机输出 `is_valid() = 0`——因为析构函数先写了 `alive = false`，那块内存恰好还没被复用，**读到了「碰巧还对的」值**。→ 知识点：[WeakPtr 反模式](../cpp-deep-dives/pointer-semantics/02-unsafe-weakptr-ub.md)「最小 UB 复现」一节
2. ASan 构建：`heap-use-after-free`，READ of size 1 落在 `is_valid()` 那一行（unsafe_weak.cpp:9），`freed by` 栈帧直指 `unique_ptr` 析构——悬垂解引用实锤。→ 知识点：同上（「看起来能工作是 UB 最危险的表现形式」）
3. `SimpleWeakPtr`：Owner 销毁只 `invalidate`，Flag 对象因 `weak` 里的 `shared_ptr` 还活着而存活，`is_valid()` 访问的是存在的对象——ASan 全绿、安全返回 false。它解决的是「Flag 内存没了」这个**生命周期安全**问题；`T` 的并发访问安全是另一个维度（教材第 3 篇的 TOCTOU 竞态）。→ 知识点：[SimpleWeakPtr](../cpp-deep-dives/pointer-semantics/03-simple-weakptr.md)「为什么这样就安全了」「线程安全讨论」两节

**代码**（Unsafe 版）：

```cpp
#include <cstdio>
#include <memory>

struct Flag { bool alive = true; };

template <typename T> class UnsafeWeakPtr {
public:
    UnsafeWeakPtr(T* p, Flag* f) : ptr_(p), flag_(f) {}
    bool is_valid() const { return flag_ && flag_->alive; }
    T* get() const { return is_valid() ? ptr_ : nullptr; }
private:
    T* ptr_;
    Flag* flag_;
};

template <typename T> class UnsafeWeakPtrFactory {
public:
    explicit UnsafeWeakPtrFactory(T* o) : owner_(o) {}
    UnsafeWeakPtr<T> get_weak_ptr() { return UnsafeWeakPtr<T>(owner_, &flag_); }
    ~UnsafeWeakPtrFactory() { flag_.alive = false; }
private:
    T* owner_;
    Flag flag_;
};

struct Widget {
    int value = 42;
    UnsafeWeakPtrFactory<Widget> factory{this};
    UnsafeWeakPtr<Widget> weak() { return factory.get_weak_ptr(); }
};

int main() {
    UnsafeWeakPtr<Widget> w = [] {
        auto p = std::make_unique<Widget>();
        return p->weak();
    }();   // Widget (and its Flag) destroyed here
    std::printf("is_valid() = %d\n", w.is_valid() ? 1 : 0);
    if (auto* p = w.get()) std::printf("value = %d\n", p->value);
    else std::printf("invalid -- but this result itself is the product of UB\n");
    return 0;
}
```

Safe 版把 `Flag*` 换成 `std::shared_ptr<Flag>`（Factory 构造时 `std::make_shared<Flag>()`，析构时 `flag_->invalidate()`），其余结构一致。

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra unsafe_weak.cpp -o unsafe_weak && ./unsafe_weak
is_valid() = 0
invalid -- but this result itself is the product of UB

$ g++ -std=c++20 -Wall -Wextra -fsanitize=address -g unsafe_weak.cpp -o unsafe_weak_asan && ./unsafe_weak_asan
=================================================================
==595==ERROR: AddressSanitizer: heap-use-after-free on address 0x77c3a43e0050 at pc 0x653bba42caaa bp 0x7fff814e95b0 sp 0x7fff814e95a0
READ of size 1 at 0x77c3a43e0050 thread T0
    #0 0x653bba42caa9 in UnsafeWeakPtr<Widget>::is_valid() const /tmp/cpp-v8-hw/unsafe_weak.cpp:9
    #1 0x653bba42c4cc in main /tmp/cpp-v8-hw/unsafe_weak.cpp:37
...
0x77c3a43e0050 is located 16 bytes inside of 24-byte region [0x77c3a43e0040,0x77c3a43e0058)
freed by thread T0 here:
    #0 0x7b93a5b2e421 in operator delete(void*, unsigned long) (/usr/lib/libasan.so.8+0x12e421)
    #1 0x653bba42cd78 in std::default_delete<Widget>::operator()(Widget*) const /usr/include/c++/16/bits/unique_ptr.h:92
    #2 0x653bba42c9e6 in std::unique_ptr<Widget, std::default_delete<Widget> >::~unique_ptr() /usr/include/c++/16/bits/unique_ptr.h:408
    #3 0x653bba42c34f in operator() /tmp/cpp-v8-hw/unsafe_weak.cpp:36
...
SUMMARY: AddressSanitizer: heap-use-after-free /tmp/cpp-v8-hw/unsafe_weak.cpp:9 in UnsafeWeakPtr<Widget>::is_valid() const
（shadow 字节图已省略）

$ g++ -std=c++20 -Wall -Wextra safe_weak.cpp -o safe_weak && ./safe_weak
is_valid() = 0
invalid, safely detected
$ g++ -std=c++20 -Wall -Wextra -fsanitize=address -g safe_weak.cpp -o safe_weak_asan && ./safe_weak_asan
is_valid() = 0
invalid, safely detected        ← ASan 零报告
```

两个版本普通构建输出**一模一样**——这就是最要命的地方：没有 ASan，你根本分不清哪个安全哪个 UB。

## 8.5-A {#hw-8-5-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-8-5-a)

**思路**：四个判断题全对——这是计算机图形学的通识底座；双缓冲模拟把「未完成画面不可见」从概念变成可打印的输出。

1. a~d 都是对的标准表述。帧缓冲是「内存 = 像素数组」的直接映射；双缓冲用 front/back 两块缓冲避免扫描过程中更新产生的撕裂；事件循环是 GUI 程序的主线程骨架。→ 知识点：[GUI 与图形（规划中）](../gui-graphics/index.md)（子领域规划主题：图形基础、最小 GUI 框架）
2. 输出里第二次 `draw()` 和第一次完全一样（全 `.`）——两个像素写进了 back，而 `draw()` 读的是 front，所以屏幕纹丝不动；`flip()` 交换两块缓冲后，第三次 `draw()` 才显示 `#`。这一步「写了但看不见」正是双缓冲「渲染完成前不上屏」语义的直接证据。→ 知识点：同上

**代码**：

```cpp
#include <array>
#include <cstdio>
#include <utility>

struct Framebuffer { std::array<char, 64> pixels{}; };   // 8x8, '.' dark '#' lit

struct Display {
    void draw() {
        std::printf("visible: ");
        for (char c : front.pixels) std::putchar(c == '#' ? '#' : '.');
        std::printf("\n");
    }
    void flip() { std::swap(front, back); }
    Framebuffer front, back;
};

int main() {
    Display d;
    std::printf("-- before any draw call --\n");
    d.draw();
    d.back.pixels[0] = '#';
    d.back.pixels[9] = '#';
    std::printf("-- wrote 2 pixels to the BACK buffer, no flip yet --\n");
    d.draw();
    d.flip();
    std::printf("-- after flip() --\n");
    d.draw();
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra dblbuf.cpp -o dblbuf && ./dblbuf
-- before any draw call --
visible: ................................................................
-- wrote 2 pixels to the BACK buffer, no flip yet --
visible: ................................................................
-- after flip() --
visible: #........#......................................................
```

## 8.5-B {#hw-8-5-b}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-8-5-b)

**思路**：事件循环 = 队列 + 回调 + 分发；`std::variant` + `std::visit` 让「发生了什么」变成类型而不是整数，给事件加字段时接口不动。

1. `std::visit` + 泛型 lambda + `if constexpr` 按事件类型分发，输出顺序即入队顺序（FIFO）。→ 知识点：[第 27 篇](../embedded/02-button/09-cpp-variant-and-visit.md)「std::visit：类型安全的分发」一节
2. 对比：`enum class` + switch 方案给 `ClickEvent` 加时间戳，得另加一个 `struct` 参数或全局变量；`variant` 方案只需在 `struct ClickEvent` 里加一个字段，`MiniLoop` 的分发代码一字不改。这正是第 27 篇「为什么用空结构体而不是 enum class」的扩展性论证，换个 GUI 场景成立。→ 知识点：[第 27 篇](../embedded/02-button/09-cpp-variant-and-visit.md)「我们的事件定义」「和 union 的对比」两节

**代码**：

```cpp
#include <cstdio>
#include <functional>
#include <queue>
#include <type_traits>
#include <variant>

struct ClickEvent { int x, y; };
struct ResizeEvent { int w, h; };
using Event = std::variant<ClickEvent, ResizeEvent>;

class MiniLoop {
public:
    void post(Event e) { queue_.push(e); }
    void on_click(std::function<void(int, int)> cb) { click_cb_ = std::move(cb); }
    void on_resize(std::function<void(int, int)> cb) { resize_cb_ = std::move(cb); }
    void run() {
        while (!queue_.empty()) {
            std::visit([&](auto&& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, ClickEvent>) {
                    if (click_cb_) click_cb_(e.x, e.y);
                } else {
                    if (resize_cb_) resize_cb_(e.w, e.h);
                }
            }, queue_.front());
            queue_.pop();
        }
    }
private:
    std::queue<Event> queue_;
    std::function<void(int, int)> click_cb_;
    std::function<void(int, int)> resize_cb_;
};

int main() {
    MiniLoop loop;
    loop.on_click([](int x, int y) { std::printf("dispatch: click at (%d,%d)\n", x, y); });
    loop.on_resize([](int w, int h) { std::printf("dispatch: resize to %dx%d\n", w, h); });
    loop.post(ClickEvent{10, 20});
    loop.post(ResizeEvent{800, 600});
    loop.post(ClickEvent{30, 40});
    loop.run();
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra evloop.cpp -o evloop && ./evloop
dispatch: click at (10,20)
dispatch: resize to 800x600
dispatch: click at (30,40)
```

## 8.6-A {#hw-8-6-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-8-6-a)

**思路**：`sizeof(Record) == 12` 多出的 2 字节是对齐 padding；实测（`offsetof`）它**不在字段之间、而在结构体末尾**——`char[4]` 对齐为 1、`temp` 之后无需补缝，补的 2 字节是为了让整个结构体大小对齐到 4（`uint32_t` 的对齐要求）；手工打包把「逻辑字段」逐段搬进固定的 10 字节线上格式，绕开结构体布局与字节序两个平台相关点。

1. 布局（`offsetof` 实测）：`id` 0~3、`temp` 4~5、`tag` 6~9、padding **10~11（末尾）**——所以裸 `fwrite` 会写出 12 字节、含 2 字节垃圾。别想当然猜「缝在字段之间」，对齐补在哪里以 `offsetof`/`sizeof` 实测为准。→ 知识点：[数据存储（规划中）](../data-storage/index.md)（规划主题：文件格式与序列化的动机）
2. 手工打包：大端字节序逐字段摆放，`tag` 用 `memcpy`（4 字节无字节序问题）；`unpack` 逆序重组，字段全部还原。线上 10 字节 `12 34 56 78 00 F5 54 4D 50 00`（245 = 0x00F5）。→ 知识点：同上
3. 手工打包解决的两个问题：**padding 导致的布局差异**（不同编译器/ABI 可能补不同的缝）和**字节序差异**（大小端机器上多字节字段的内存序不同）——布局稳定 + 字节序显式，二进制才敢跨机器流动。→ 知识点：同上（与 [00 · 传统 socket 编程](../networking/00-traditional-socket-basics.md) 的 `htonl`/`htons` 是同一套思想）

**代码**：

```cpp
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

struct Record {                 // logical record -- never written directly
    std::uint32_t id;
    std::uint16_t temp;         // temperature in deci-celsius (245 == 24.5 C)
    char tag[4];
};

std::array<unsigned char, 10> pack(const Record& r) {
    std::array<unsigned char, 10> b{};
    b[0] = static_cast<unsigned char>((r.id >> 24) & 0xFF);
    b[1] = static_cast<unsigned char>((r.id >> 16) & 0xFF);
    b[2] = static_cast<unsigned char>((r.id >> 8) & 0xFF);
    b[3] = static_cast<unsigned char>(r.id & 0xFF);
    b[4] = static_cast<unsigned char>((r.temp >> 8) & 0xFF);
    b[5] = static_cast<unsigned char>(r.temp & 0xFF);
    std::memcpy(&b[6], r.tag, 4);
    return b;
}

Record unpack(const std::array<unsigned char, 10>& b) {
    Record r{};
    r.id = (static_cast<std::uint32_t>(b[0]) << 24) | (static_cast<std::uint32_t>(b[1]) << 16) |
           (static_cast<std::uint32_t>(b[2]) << 8) | static_cast<std::uint32_t>(b[3]);
    r.temp = static_cast<std::uint16_t>((static_cast<std::uint16_t>(b[4]) << 8) |
                                        static_cast<std::uint16_t>(b[5]));
    std::memcpy(r.tag, &b[6], 4);
    return r;
}

int main() {
    Record r{0x12345678u, 245, {'T', 'M', 'P', '\0'}};
    std::printf("sizeof(Record) = %zu   <-- raw fwrite would dump this many bytes\n", sizeof(Record));
    auto bytes = pack(r);
    std::printf("packed wire bytes (%zu): ", bytes.size());
    for (unsigned char c : bytes) std::printf("%02X ", c);
    std::printf("\n");
    auto r2 = unpack(bytes);
    std::printf("unpack: id=0x%08X temp=%u tag=%s\n", r2.id, r2.temp, r2.tag);
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra binrec.cpp -o binrec && ./binrec
sizeof(Record) = 12   <-- raw fwrite would dump this many bytes
packed wire bytes (10): 12 34 56 78 00 F5 54 4D 50 00
unpack: id=0x12345678 temp=245 tag=TMP
```

## 8.6-B {#hw-8-6-b}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-8-6-b)

**思路**：开放寻址的查找是「从哈希位置出发一路探测」；探测链上每个槽都曾是插入时探测过的，所以第一个空槽之后不可能再有目标键——可以安全提前返回 `nullopt`。

1. `put` 在空槽或同键槽停下（覆盖即同键分支）；`get` 遇到第一个空槽即返回「不存在」。→ 知识点：[数据存储（规划中）](../data-storage/index.md)（规划主题：键值存储）
2. 8 个槽位、`std::array` 定容、`std::string_view` 键——全程零堆分配，和嵌入式「静态分配」的约束一脉相承（表满返回 false 而不是偷偷扩容）。→ 知识点：[静态存储与栈上分配策略](../embedded/02-static-and-stack-allocation.md)
3. 验证输出：`led=0`（覆盖生效）、`missing=-1`（`value_or(-1)` 兜底）。→ 知识点：同上

**代码**：

```cpp
#include <array>
#include <cstdio>
#include <cstddef>
#include <optional>
#include <string_view>

struct Entry {
    std::string_view key;   // borrowed: points into static string literals
    int value = 0;
    bool used = false;
};

class FixedMap {            // open addressing + linear probing, 8 slots, no heap
public:
    bool put(std::string_view key, int value) {
        std::size_t i = hash(key);
        for (std::size_t k = 0; k < slots_.size(); ++k) {
            Entry& e = slots_[(i + k) % slots_.size()];
            if (!e.used || e.key == key) {       // empty slot or overwrite
                e.key = key; e.value = value; e.used = true;
                return true;
            }
        }
        return false;                           // table full
    }
    std::optional<int> get(std::string_view key) const {
        std::size_t i = hash(key);
        for (std::size_t k = 0; k < slots_.size(); ++k) {
            const Entry& e = slots_[(i + k) % slots_.size()];
            if (!e.used) return std::nullopt;   // probing stops at first empty slot
            if (e.key == key) return e.value;
        }
        return std::nullopt;
    }
    static std::size_t slot_count() { return 8; }
private:
    static std::size_t hash(std::string_view s) { return s.size(); }   // toy hash
    std::array<Entry, 8> slots_{};
};

int main() {
    FixedMap m;
    m.put("led", 1);
    m.put("uart", 2);
    m.put("led", 0);          // overwrite existing key
    m.put("button", 3);
    m.put("tensor", 4);
    std::printf("led=%d uart=%d button=%d tensor=%d missing=%d\n",
                m.get("led").value_or(-1), m.get("uart").value_or(-1),
                m.get("button").value_or(-1), m.get("tensor").value_or(-1),
                m.get("nope").value_or(-1));
    std::printf("slots=%zu\n", FixedMap::slot_count());
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra kvtable.cpp -o kvtable && ./kvtable
led=0 uart=2 button=3 tensor=4 missing=-1
slots=8
```

## 8.7-A {#hw-8-7-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-8-7-a)

**思路**：复杂度表是通识；实测里冒泡的比较次数严格等于 $\frac{n(n-1)}{2}$，而 `std::sort` 用 introsort 把最坏情况压到 O(n log n)——实测约 43 倍（约 1.6 个数量级）的差距就是「复杂度」这个抽象概念的真身。

1. 填空表：冒泡最坏/平均 O(n²)（稳定）；插入 O(n²)/O(n²)（稳定，近似有序时近 O(n)）；选择 O(n²)/O(n²)（不稳定）；快速 O(n²)/O(n log n)（不稳定）；归并 O(n log n)/O(n log n)（稳定）；堆排序 O(n log n)/O(n log n)（不稳定）。→ 知识点：[算法与数据结构（规划中）](../algorithms/index.md)（规划主题：复杂度分析、经典算法）
2. `std::sort` 是 introsort（快速排序 + 堆排序兜底 + 小规模插入排序），最坏 O(n log n)——快速排序一出现坏划分就切到堆排序，所以标准库敢承诺。→ 知识点：同上
3. 实测：冒泡 499500 次比较 = $\frac{1000×999}{2}$ 精确命中理论值（两重循环、每轮少一个）；`std::sort` 11508 次，实测 $\frac{499500}{11508} ≈ 43.4$ 倍——约 1.6 个数量级。**「两个数量级」是理论比值**：n/log₂n = $\frac{1000}{9.97} ≈ 100$；实测只有 43 倍是因为 n=1000 还太小、n log n 的常数与低阶项没被摊薄，n 越大差距越悬殊（n² vs n log n）。→ 知识点：同上

**代码**：

```cpp
#include <algorithm>
#include <cstdio>
#include <random>
#include <vector>

static long g_cmp = 0;
static bool lt(int a, int b) { ++g_cmp; return a < b; }

static void bubble(std::vector<int>& v) {
    for (std::size_t i = 0; i < v.size(); ++i)
        for (std::size_t j = 0; j + 1 < v.size() - i; ++j)
            if (lt(v[j + 1], v[j])) std::swap(v[j], v[j + 1]);
}

int main() {
    std::mt19937 rng(42);
    std::vector<int> v(1000), v2;
    for (int& x : v) x = static_cast<int>(rng() % 10000);
    v2 = v;
    g_cmp = 0; bubble(v);
    std::printf("bubble sort comparisons (n=1000): %ld\n", g_cmp);
    g_cmp = 0;
    std::sort(v2.begin(), v2.end(), [](int a, int b) { return lt(a, b); });
    std::printf("std::sort comparisons (n=1000):   %ld\n", g_cmp);
    std::printf("both sorted identically: %d\n", v == v2 ? 1 : 0);
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra -O2 cmpcmp.cpp -o cmpcmp && ./cmpcmp
bubble sort comparisons (n=1000): 499500
std::sort comparisons (n=1000):   11508
both sorted identically: 1
```

## 8.7-B {#hw-8-7-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-8-7-b)

**思路**：二叉堆是「完全二叉树 + 堆序性质」；上滤维护新元素路径上的堆序，下滤在删顶后把新的根沉到正确位置，每步走一半深度，所以都是 O(log n)。

1. `push` 尾插后沿父链上滤（`parent = (i-1)/2`）；`pop` 用最后一个元素补顶、沿孩子链下滤（$2i+1$/$2i+2$ 取较大者）。→ 知识点：[算法与数据结构（规划中）](../algorithms/index.md)（规划主题：高级数据结构、手写 STL 组件）
2. 10 个随机数 pop 序列严格降序，且与 `std::priority_queue` 逐元素一致——手写实现与标准库语义对齐。→ 知识点：同上
3. O(log n) 的原因：每次交换后节点深度改变一层，而堆高是 log n；上滤维护「从新叶到根」的堆序，下滤维护「从根到叶」的堆序。→ 知识点：同上

**代码**：

```cpp
#include <algorithm>
#include <cstdio>
#include <functional>
#include <queue>
#include <random>
#include <vector>

class MaxHeap {                       // hand-written binary max-heap
public:
    void push(int x) {
        data_.push_back(x);
        std::size_t i = data_.size() - 1;
        while (i > 0 && data_[parent(i)] < data_[i]) {   // sift up
            std::swap(data_[parent(i)], data_[i]);
            i = parent(i);
        }
    }
    int pop() {
        int top = data_.front();
        data_.front() = data_.back();
        data_.pop_back();
        std::size_t i = 0;
        for (;;) {                                       // sift down
            std::size_t l = left(i), r = right(i), m = i;
            if (l < data_.size() && data_[l] > data_[m]) m = l;
            if (r < data_.size() && data_[r] > data_[m]) m = r;
            if (m == i) break;
            std::swap(data_[m], data_[i]);
            i = m;
        }
        return top;
    }
    bool empty() const { return data_.empty(); }
private:
    static std::size_t parent(std::size_t i) { return (i - 1) / 2; }
    static std::size_t left(std::size_t i) { return 2 * i + 1; }
    static std::size_t right(std::size_t i) { return 2 * i + 2; }
    std::vector<int> data_;
};

int main() {
    MaxHeap h;
    std::mt19937 rng(7);
    std::vector<int> input;
    for (int i = 0; i < 10; ++i) { int x = static_cast<int>(rng() % 100); input.push_back(x); h.push(x); }
    std::printf("input: ");
    for (int x : input) std::printf("%d ", x);
    std::printf("\npop:   ");
    std::vector<int> out;
    while (!h.empty()) out.push_back(h.pop());
    for (int x : out) std::printf("%d ", x);
    std::printf("\n");
    std::printf("descending order: %d\n",
                std::is_sorted(out.begin(), out.end(), std::greater<int>()) ? 1 : 0);
    std::priority_queue<int> ref;                     // cross-check
    for (int x : input) ref.push(x);
    bool same = true;
    for (int x : out) { if (ref.top() != x) same = false; ref.pop(); }
    std::printf("matches std::priority_queue: %d\n", same ? 1 : 0);
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra myheap.cpp -o myheap && ./myheap
input: 15 92 21 86 83 47 87 79 88 61
pop:   92 88 87 86 83 79 61 47 21 15
descending order: 1
matches std::priority_queue: 1
```

## 8.C-1 {#hw-8-c-1}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-8-c-1)

**思路**：流式解析器的全部状态就是一个固定缓冲 + 一个长度；分片大小完全不参与逻辑——`feed` 只认「字节」不认「包边界」，这正是 TCP 流语义的正确心态。

1. 行终结处理 `\r`（容忍 CRLF）与 `\n`（结算一行）；普通字符在 `len_ < size-1` 时才追加——超长部分静默丢弃、缓冲区永不越界。→ 知识点：[第 42 篇](../embedded/03-uart/12-command-processor-and-main-walkthrough.md)「任务二：UART 接收 → 命令解析」一节（`line_len < line_buf.size() - 1` 防溢出的同一写法）
2. 分片喂入：`{2,1,4,3,7,2,5,9,1,1,1,1}` 循环切分 81 字节的流，共分发 5 行；超长行被截成 15 字符 `'an overlong lin'`（缓冲 16 格留 1 格给 `\0`）。ASan 构建零报告。→ 知识点：[00 · 传统 socket 编程](../networking/00-traditional-socket-basics.md)（`read` 返回任意字节数，流没有「消息边界」）
3. 去掉 `line_len < line_buf.size() - 1` 后，超长行会一直写穿 `line_` 栈数组——ASan 的 stack-buffer-overflow 会当场点名（第 42 篇原话就是这个条件的用意）。→ 知识点：[第 42 篇](../embedded/03-uart/12-command-processor-and-main-walkthrough.md)（同一行代码）

**代码**：

```cpp
#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>

class LineParser {
public:
    // feed() may be called with fragments of ANY size -- a byte stream is a byte stream
    void feed(const char* data, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            char c = data[i];
            if (c == '\r') continue;              // tolerate CRLF
            if (c == '\n') {
                if (len_ > 0) {
                    line_[len_] = '\0';
                    on_line(std::string_view(line_.data(), len_));
                    len_ = 0;
                }
            } else if (len_ < line_.size() - 1) { // overlong lines: drop the tail
                line_[len_++] = c;
            }
        }
    }
    static int& line_count() { static int n = 0; return n; }
private:
    void on_line(std::string_view lv) {
        ++line_count();
        std::printf("line %d: '%s'\n", line_count(), lv.data());
    }
    std::array<char, 16> line_{};
    std::size_t len_ = 0;
};

int main() {
    LineParser p;
    const char* stream =
        "LED ON\r\nHELP\nLED OFF\n"
        "an overlong line that definitely exceeds sixteen bytes\nTAIL\n";
    const std::size_t total = std::strlen(stream);
    std::printf("stream length = %zu bytes\n", total);
    const std::size_t frag[] = {2, 1, 4, 3, 7, 2, 5, 9, 1, 1, 1, 1};   // irregular fragments
    std::size_t pos = 0, k = 0;
    while (pos < total) {
        std::size_t n = frag[k % (sizeof(frag) / sizeof(frag[0]))];
        if (pos + n > total) n = total - pos;
        p.feed(stream + pos, n);
        pos += n; ++k;
    }
    std::printf("total lines dispatched: %d\n", LineParser::line_count());
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra lineparse.cpp -o lineparse && ./lineparse
stream length = 81 bytes
line 1: 'LED ON'
line 2: 'HELP'
line 3: 'LED OFF'
line 4: 'an overlong lin'
line 5: 'TAIL'
total lines dispatched: 5
$ g++ -std=c++20 -Wall -Wextra -fsanitize=address -g lineparse.cpp -o lineparse_asan && ./lineparse_asan
（输出与上面完全相同，ASan 零报告）
```

## 8.C-2 {#hw-8-c-2}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-8-c-2)

**思路**：`span` 是「借用」在 C++ 里的标准形态——指针 + 长度，不拥有、不拷贝；`LayerView` 只存视图，数据仍归 Tensor 所有，所有权边界清晰所以推理器敢用。

1. `forward` 按行主序做乘加：y = [1, 2, 3, 12.5]（第 3 行权重全 2，$2+4+6+0.5 = 12.5$）。→ 知识点：[行主序](../ai/tiny_ml/stage1/04-row-major.md)（`W[i*in+j]` 的换算）
2. `view().data() == storage().data()`——视图与底层数组同址；w(0,0) = 100 后再次 forward，`y[0]` 立刻变成 100：借用的视图看到的是所有者的最新数据，不是副本。→ 知识点：[固定维度 Tensor](../ai/tiny_ml/stage1/06-tensor.md)「扁平 span 视图（不拷贝）」、[非拥有指针全景](../cpp-deep-dives/pointer-semantics/01-non-owning-pointer-overview.md)（第一层：借用）
3. 推理器敢用是因为 Tensor 的所有权集中、生命周期明确（栈上/静态存储，视图不逃逸）；一旦 `LayerView` 存得比 Tensor 久（比如异步回调里带着视图跑），span 就变成悬垂引用——和 `Borrowed` 的边界一模一样：借用适合「短暂同步使用」，别存下来以后再用。→ 知识点：[非拥有指针全景](../cpp-deep-dives/pointer-semantics/01-non-owning-pointer-overview.md)（四层语义模型与「异步回调：危险」那一行）

**代码**：

```cpp
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>

template <std::size_t Rows, std::size_t Cols, typename S = float>
class Tensor {
public:
    constexpr Tensor() = default;
    constexpr explicit Tensor(std::array<S, Rows * Cols> a) : internals_(a) {}
    constexpr S& operator()(std::size_t i, std::size_t j) noexcept { return internals_[i * Cols + j]; }
    constexpr const S& operator()(std::size_t i, std::size_t j) const noexcept { return internals_[i * Cols + j]; }
    constexpr std::span<const S, Rows * Cols> view() const noexcept { return internals_; }
    constexpr const std::array<S, Rows * Cols>& storage() const noexcept { return internals_; }
private:
    std::array<S, Rows * Cols> internals_{};
};

// LayerView is NON-OWNING: it borrows weight/bias memory via spans.
// It must never outlive the Tensors it observes.
class LayerView {
public:
    LayerView(std::span<const float> weights, std::span<const float> bias,
              std::size_t in, std::size_t out)
        : w_(weights), b_(bias), in_(in), out_(out) {}
    // y = W x + b, row-major: y[i] = sum_j W[i*in+j] * x[j] + b[i]
    void forward(std::span<const float> x, std::span<float> y) const {
        for (std::size_t i = 0; i < out_; ++i) {
            float acc = b_[i];
            for (std::size_t j = 0; j < in_; ++j) acc += w_[i * in_ + j] * x[j];
            y[i] = acc;
        }
    }
private:
    std::span<const float> w_;
    std::span<const float> b_;
    std::size_t in_, out_;
};

int main() {
    Tensor<4, 3> w(std::array{1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 2.f, 2.f, 2.f});
    Tensor<1, 4> b(std::array{0.f, 0.f, 0.f, 0.5f});
    std::array<float, 3> x{1.f, 2.f, 3.f};
    std::array<float, 4> y{};
    LayerView lv(w.view(), b.view(), 3, 4);
    lv.forward(x, y);
    std::printf("y = [%.1f %.1f %.1f %.1f]\n", y[0], y[1], y[2], y[3]);
    std::printf("view() shares storage: %d\n", w.view().data() == w.storage().data());
    w(0, 0) = 100.f;                       // owner mutates; the borrowed view sees it
    lv.forward(x, y);
    std::printf("after w(0,0)=100: y[0] = %.1f\n", y[0]);
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++23 -Wall -Wextra layerview.cpp -o layerview && ./layerview
y = [1.0 2.0 3.0 12.5]
view() shares storage: 1
after w(0,0)=100: y[0] = 100.0
```

## 8.C-3 {#hw-8-c-3}

**难度 L5** · 题面见 [homework](./01-homework.md#hw-8-c-3)

**思路**：SPSC 无锁队列的正确性建立在两个不变量上——「只有一个线程写 head、只有一个线程写 tail」保证无竞争写；「写者先写数据、再 release 发布 head」「读者先 acquire 读 head、再读数据」保证数据先于索引可见。acquire/release 是这把锁的「锁」。

1. 结构：`head_`/`tail_` 各自 `alignas(64)` 独占 cache line，避免伪共享；容量 N-1 用 `head - tail` 判满，留一槽区分空满（和第 37 篇的初衷一致，只是计数器不绕 2N 域）。→ 知识点：[第 37 篇](../embedded/03-uart/07-circular-buffer-lock-free-spsc.md)（SPSC 分工与留一槽思想）、[中断安全的代码编写](../embedded/05-interrupt-safe-coding.md)（共享变量跨执行流的可见性问题）
2. 内存序：`push` 里 `head` 自己读写（只有生产者碰）可用 relaxed；读 `tail` 必须 acquire——不 acquire，编译器/CPU 可能把「消费者还没发布的 tail 旧值」缓存住，或把 buf 写入重排到判满之后；写回 `head` 必须 release——保证「buf_[i] = v」先于 head 发布，消费者 acquire 到新 head 时一定能看到完整数据。→ 知识点：同上（vol5 的 memory order 章节）
3. 全 relaxed 的后果：数据写入可能被重排到 head 发布之后，消费者看到新 head 却读到旧数据（或编译器缓存了 head/tail 导致死循环）——这是教科书级的 order 错误，TSan 会当场报 race。→ 知识点：同上
4. 验收：普通 `-O2` 与 TSan 构建都跑通 1000 万条、严格递增校验通过、TSan 零报告。→ 知识点：同上

**代码**：

```cpp
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

// Vyukov-style SPSC ring buffer: one producer, one consumer, no locks.
// Capacity is N-1 (one slot stays empty to distinguish full from empty).
template <typename T, std::size_t N>
class SpscRing {
    static_assert(N > 1 && (N & (N - 1)) == 0, "N must be a power of 2");
public:
    bool push(const T& v) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = head + 1;
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        if (next - tail > N - 1) return false;        // full
        buf_[head & (N - 1)] = v;
        head_.store(next, std::memory_order_release);
        return true;
    }
    bool pop(T& v) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (tail == head) return false;               // empty
        v = buf_[tail & (N - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }
private:
    std::array<T, N> buf_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

int main() {
    constexpr std::size_t kTotal = 10'000'000;
    SpscRing<std::uint32_t, 1024> ring;
    std::vector<std::uint32_t> received;
    received.reserve(kTotal);

    std::thread producer([&] {
        for (std::uint32_t i = 0; i < kTotal; ++i)
            while (!ring.push(i)) {}   // spin while full
    });
    std::thread consumer([&] {
        std::uint32_t v;
        std::size_t got = 0;
        while (got < kTotal)
            if (ring.pop(v)) { received.push_back(v); ++got; }
    });
    producer.join();
    consumer.join();

    std::printf("received=%zu\n", received.size());
    bool ok = true;
    for (std::size_t i = 0; i < kTotal; ++i)
        if (received[i] != i) { ok = false; break; }
    std::printf("payload order intact (no loss, no dup): %d\n", ok ? 1 : 0);
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra -O2 -pthread spsc_ring.cpp -o spsc_ring && ./spsc_ring
received=10000000
payload order intact (no loss, no dup): 1
$ g++ -std=c++20 -Wall -Wextra -g -O1 -fsanitize=thread spsc_ring.cpp -o spsc_ring_tsan && ./spsc_ring_tsan
received=10000000
payload order intact (no loss, no dup): 1        ← TSan 零报告
```
