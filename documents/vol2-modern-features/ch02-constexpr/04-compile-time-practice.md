---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: 综合运用 constexpr 实现编译期查表、字符串处理、状态机和设计模式
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'Chapter 2: constexpr 基础'
- 'Chapter 2: constexpr 构造函数与字面类型'
- 'Chapter 2: consteval 与 constinit'
reading_time_minutes: 17
related:
- 元编程精要（C++20-23）
tags:
- host
- cpp-modern
- intermediate
- constexpr
- 编译期计算
- 零开销抽象
title: 编译期计算实战：从查表到编译期字符串
---
# 编译期计算实战：从查表到编译期字符串

前面的三篇里，咱们把 `constexpr` 的基础机制、字面类型，还有 C++20 的 `consteval`/`constinit` 一件件看了下来。到了这一篇，笔者认为知识储备已经够了，这些工具也该组合起来了，让它们去做点真正有用的事。

新语法这一篇就不再引入了，咱们直接动手写东西：从编译期查表起步，一路走过编译期的字符串和状态机，再到模板与设计模式的配合，最后落在嵌入式的真实场景里，看看这些技术到底能换来什么。

## 第一步——编译期查表（Lookup Table）

查表是性能优化里最古老也最可靠的策略之一：用空间换时间，把复杂计算的输入-输出映射提前算好、存成数组，运行的时候只做数组索引。真正麻烦的地方，历来在表的生成上。靠运行时初始化的，启动的时候得现算一遍，时间就搭进去了。靠外部工具生成代码再 `#include` 进来的，构建的流程又会跟着变复杂。`constexpr` 给了咱们第三个选择：让编译器在编译阶段就把表生成好。

### CRC-32 查找表

CRC 的全称是 Cyclic Redundancy Check，翻成中文的意思是循环冗余校验。您在网络协议、存储系统、通信链路里都能见到它，CRC-32 靠一张 256 项的查找表来加速计算。第一篇里咱们已经把 CRC-32 的表生成过一遍了，这里咱们换个角度，关心两件事：表项的值对不对，以及怎么亲手验证表确实进了只读段。

```cpp
#include <array>
#include <cstdint>

constexpr std::array<std::uint32_t, 256> make_crc32_table()
{
    std::array<std::uint32_t, 256> table{};
    constexpr std::uint32_t kPolynomial = 0xEDB88320u;

    for (std::size_t i = 0; i < 256; ++i) {
        std::uint32_t crc = static_cast<std::uint32_t>(i);
        for (int j = 0; j < 8; ++j) {
            crc = (crc & 1) ? ((crc >> 1) ^ kPolynomial) : (crc >> 1);
        }
        table[i] = crc;
    }
    return table;
}

// 编译期生成完整的 CRC-32 查找表
constexpr auto kCrc32Table = make_crc32_table();

// 编译期校验表的前几项是否正确
static_assert(kCrc32Table[0] == 0x00000000u, "CRC table entry 0 should be 0");
static_assert(kCrc32Table[1] == 0x77073096u, "CRC table entry 1 mismatch");
static_assert(kCrc32Table[255] == 0x2D02EF8Du, "CRC table entry 255 mismatch");

// 运行时 CRC 计算：只需做查表 + XOR
constexpr std::uint32_t crc32(const std::uint8_t* data, std::size_t length)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < length; ++i) {
        std::uint8_t index = static_cast<std::uint8_t>((crc ^ data[i]) & 0xFF);
        crc = (crc >> 8) ^ kCrc32Table[index];
    }
    return crc ^ 0xFFFFFFFFu;
}
```

`kCrc32Table` 进了哪儿，第一篇里咱们已经交过底：编译期生成完毕，直接写进目标文件的只读数据段 `.rodata`。这里补上亲手验证的法子：您拿 `objdump -s -j .rodata` 看一眼生成的二进制，就能确认表的数据确实在只读段里。`static_assert` 把头几项的值跟标准 CRC-32 表对了一遍，生成逻辑对不对？编译期就给了您答案。运行时的 `crc32` 函数只剩下查表和 XOR，真的非常快。

运行期初始化和编译期生成的差别，咱们直接画成了图：

![CRC-32 查找表：运行期初始化与编译期生成的对比](./04-compile-time-practice-table.drawio)

### 正弦函数查表

咱们做信号处理、电机控制、游戏开发的时候，经常要快速拿到三角函数的值。标准库的 `std::sin` 在没有 FPU（Floating Point Unit，浮点运算单元）的平台上可能非常慢，查表是常见的替代方案。sin 的值本身，咱们都可以在编译期算出来：

```cpp
#include <array>
#include <cstddef>

template <std::size_t N>
constexpr std::array<float, N> make_sin_table()
{
    std::array<float, N> table{};
    constexpr double kPi = 3.14159265358979323846;

    for (std::size_t i = 0; i < N; ++i) {
        double angle = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(N);

        // 泰勒展开近似 sin(x) - 使用前5项（最高到 x^9/9!）
        // sin(x) ≈ x - x^3/3! + x^5/5! - x^7/7! + x^9/9!
        double x = angle;
        double term = x;
        double sum = term;
        for (int n = 1; n <= 4; ++n) {  // 4次迭代计算第2-5项
            term *= -x * x / static_cast<double>((2 * n) * (2 * n + 1));
            sum += term;
        }
        table[i] = static_cast<float>(sum);
    }
    return table;
}

// 编译期生成 256 点正弦查表
constexpr auto kSinTable = make_sin_table<256>();

static_assert(kSinTable[0] < 0.001f && kSinTable[0] > -0.001f,
              "sin(0) should be approximately 0");
static_assert(kSinTable[64] > 0.99f && kSinTable[64] < 1.01f,
              "sin(π/2) should be approximately 1");

// 快速 sin 查表（角度范围 [0, 2π) 映射到 [0, 255]）
constexpr float fast_sin_index(std::size_t index)
{
    return kSinTable[index & 0xFF];
}
```

这里的泰勒展开用了 5 项，最高的项到了 x^9/9!。有件事得跟您交底：5 项展开跟第一篇里那版三项展开是一样的，只在 0 附近的区间收敛得像样。角度折进 0 到 π/2 的区间使用的话，实测的误差远小于 0.1%。可代码里的表铺满了整个 0 到 2π，256 项里有 148 项的偏差超过了 0.001，末尾的 idx 255 甚至算出了 11.4，而正弦的真值不会超过 1。两个 `static_assert` 挑的恰好是 0 和 64，都落在收敛最稳的 0 到 π/2 区间里。您真要全表拿来用，第一篇说过的范围缩减免不了：把角度折叠进 0 到 π/2 的区间，再靠对称性补全四个象限的值。您要是需要更高的精度，咱们可以增加展开的项数，或者换切比雪夫多项式的其他逼近方法。只要这些数学能写成一个 `constexpr` 的函数，咱们就能在编译期把表生成出来。

## 第二步——编译期字符串处理

字符串处理在 C++ 里通常是运行时的活儿。可是命令名、协议字段、错误消息 ID 一类的字符串，内容在编译期就已经定下来了。咱们把这些操作提前到编译期去做，运行时的字符串比较和解析开销就能省下一截。

### 编译期字符串哈希

C++ 的 `switch` 语句没法直接拿字符串当条件。经典的变通方案，是用编译期的哈希把字符串映射成整数，再拿得到的整数去做 `switch`。第三篇的场景一里，咱们见过 `consteval` 版的 FNV-1a，那里的哈希计算被强制留在编译期。本篇的入口是运行时传进来的命令字符串，同一份算法在这里标的是 `constexpr`：常量的哈希在编译期算出来，用户输入的哈希在运行时算。咱们看具体的代码：

```cpp
#include <cstdint>
#include <cstddef>

// FNV-1a 哈希：简单、分布均匀、广泛使用
constexpr std::uint32_t fnv1a32(const char* str, std::size_t len)
{
    std::uint32_t hash = 0x811c9dc5u;
    for (std::size_t i = 0; i < len; ++i) {
        hash ^= static_cast<std::uint8_t>(str[i]);
        hash *= 0x01000193u;
    }
    return hash;
}

// 从字符串字面量推导长度
template <std::size_t N>
constexpr std::uint32_t str_hash(const char (&s)[N])
{
    return fnv1a32(s, N - 1);  // N - 1 排除末尾的 '\0'
}

// 编译期生成所有命令的哈希值
constexpr auto kHashInit   = str_hash("INIT");
constexpr auto kHashStart  = str_hash("START");
constexpr auto kHashStop   = str_hash("STOP");
constexpr auto kHashReset  = str_hash("RESET");

// 编译期冲突检测
static_assert(kHashInit != kHashStart, "Hash collision detected");
static_assert(kHashInit != kHashStop, "Hash collision detected");
static_assert(kHashStart != kHashStop, "Hash collision detected");
static_assert(kHashStart != kHashReset, "Hash collision detected");

// 运行时命令分派
#include <cstring>
void dispatch_command(const char* cmd)
{
    std::uint32_t h = fnv1a32(cmd, std::strlen(cmd));
    switch (h) {
        case kHashInit:  /* handle INIT */  break;
        case kHashStart: /* handle START */ break;
        case kHashStop:  /* handle STOP */  break;
        case kHashReset: /* handle RESET */ break;
        default: /* unknown command */ break;
    }
}
```

代码里有一处值得咱们看清楚的地方：运行时那个 `fnv1a32` 调用，算的是运行时传进来的字符串的哈希，`kHashStart` 这些则是编译期就算好的常量。`switch` 拿编译期的常量和运行时的哈希值做比较，匹配逻辑是正确的。当然，哈希冲突在理论上总是存在的。`static_assert` 能覆盖的，是咱们已知命令之间的冲突检测，未知输入之间的冲突，它就覆盖不到了。您的应用要对正确性要求极高，比如安全关键的系统，咱们就可以在哈希匹配之后再做一次 `strcmp` 确认。这么做会多付一点运行时的开销，换来的，是冲突导致的错误行为被完全避免。

## 第三步——编译期状态机

状态机是嵌入式开发里用得最多的设计模式之一。传统写法通常是一个大的 `switch-case` 结构，或者是函数指针的数组，它们缺的是编译期验证：咱们可能漏掉某个状态对某个事件的处理，编译器不会提醒咱们。改用 `constexpr` 来定义状态的转移表，再配上 `static_assert` 做编译期的校验，遗漏和冲突在编译阶段就会替咱们暴露出来。

### 状态机的 constexpr 定义

```cpp
#include <array>
#include <cstdint>
#include <cstddef>

enum class State : std::uint8_t { Idle, Debouncing, Pressed, Count };
enum class Event : std::uint8_t { Press, Release, Timeout, Count };

// 状态转移条目
struct Transition {
    State from;
    Event trigger;
    State to;
};

// 编译期转移表
constexpr std::array<Transition, 5> kDebounceTable = {{
    {State::Idle,       Event::Press,   State::Debouncing},
    {State::Debouncing, Event::Timeout, State::Pressed},
    {State::Debouncing, Event::Release, State::Idle},
    {State::Pressed,    Event::Release, State::Idle},
    {State::Pressed,    Event::Timeout, State::Idle},
}};
```

### 编译期校验转移表

转移表到手了，咱们就能在编译期做各种校验。比如咱们查一查有没有哪个状态一条出转移都没有，也就是所谓的“死状态”，或者有没有重复的 `(from, trigger)` 对。

```cpp
// 检查是否有重复的 (state, event) 组合
template <std::size_t N>
constexpr bool has_duplicate_transitions(const std::array<Transition, N>& table)
{
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            if (table[i].from == table[j].from &&
                table[i].trigger == table[j].trigger) {
                return true;
            }
        }
    }
    return false;
}

// 检查所有状态是否都至少有一个出转移（排除 Count 哨兵值）
template <std::size_t N>
constexpr bool all_states_have_transitions(const std::array<Transition, N>& table)
{
    constexpr std::size_t kStateCount = static_cast<std::size_t>(State::Count);
    bool found[kStateCount] = {};
    for (std::size_t i = 0; i < N; ++i) {
        found[static_cast<std::size_t>(table[i].from)] = true;
    }
    for (std::size_t s = 0; s < kStateCount; ++s) {
        if (!found[s]) return false;
    }
    return true;
}

static_assert(!has_duplicate_transitions(kDebounceTable),
              "Duplicate (state, event) pairs found in transition table");
static_assert(all_states_have_transitions(kDebounceTable),
              "Some states have no outgoing transitions");
```

以后改表的，可能是您的同事，也可能是几个月后的您自己。谁要是改出了重复条目，或者漏掉某个状态的处理，`static_assert` 会替咱们在编译期立刻报错，给出的错误信息也写得明明白白。人眼容易漏的错误，它是抓得住的，而且代码编不过，问题就必须当场修掉了。

### 运行时状态机引擎

转移表是在编译期定义和校验的，可是状态机真正跑起来的时候，终归是运行时的事。咱们把运行时的引擎也写出来：

```cpp
class DebounceFsm {
public:
    constexpr DebounceFsm() : state_(State::Idle) {}

    void handle(Event ev)
    {
        for (const auto& t : kDebounceTable) {
            if (t.from == state_ && t.trigger == ev) {
                state_ = t.to;
                return;
            }
        }
        // 未找到匹配的转移：忽略事件（或者触发断言）
    }

    constexpr State current_state() const { return state_; }

private:
    State state_;
};
```

引擎的实现简单得很，就是把转移表从头到尾地扫一遍，找到了匹配项就转移。只有几个状态和事件的小型状态机，线性查找是完全够用的。状态和事件的数量一多，咱们可以考虑换成拿 `(state, event)` 当索引的二维数组，替掉原来的线性查找。

咱们把 `fsm.handle()` 收到一个事件后的查表过程做成了动画，您可以按步进键单步地看匹配的转移是怎么被找出来的：

<Anim id="fsm-table-dispatch" />

## 第四步——constexpr 与模板的配合

`constexpr` 和模板不是二选一的关系，它们负责的层面不一样：模板做类型层面的编译期分派，`constexpr` 做值层面的编译期计算。咱们把两者结合起来，能做出的编译期抽象就非常强了。

### 编译期策略模式

策略模式（Strategy Pattern）的分派，通常靠的是虚函数或者函数指针，运行的时候才做决定。不过策略要是编译期就能定下来，咱们就可以用模板加 `constexpr` 把分派整个消掉，做成零开销的策略选择。

```cpp
// CRC-32 策略
struct Crc32Strategy {
    static constexpr const char* name = "CRC-32";

    static constexpr std::uint32_t compute(const std::uint8_t* data, std::size_t len)
    {
        constexpr std::uint32_t kPoly = 0xEDB88320u;
        std::uint32_t crc = 0xFFFFFFFFu;
        for (std::size_t i = 0; i < len; ++i) {
            std::uint8_t idx = static_cast<std::uint8_t>((crc ^ data[i]) & 0xFF);
            std::uint32_t entry = static_cast<std::uint32_t>(idx);
            for (int j = 0; j < 8; ++j) {
                entry = (entry & 1) ? ((entry >> 1) ^ kPoly) : (entry >> 1);
            }
            crc = (crc >> 8) ^ entry;
        }
        return crc ^ 0xFFFFFFFFu;
    }
};

// CRC-16-CCITT 策略
struct Crc16CcittStrategy {
    static constexpr const char* name = "CRC-16-CCITT";

    static constexpr std::uint16_t compute(const std::uint8_t* data, std::size_t len)
    {
        constexpr std::uint16_t kPoly = 0x1021u;
        std::uint16_t crc = 0xFFFFu;
        for (std::size_t i = 0; i < len; ++i) {
            crc ^= static_cast<std::uint16_t>(data[i]) << 8;
            for (int j = 0; j < 8; ++j) {
                crc = (crc & 0x8000) ? ((crc << 1) ^ kPoly) : (crc << 1);
            }
        }
        return crc;
    }
};

// 编译期策略选择——零虚函数表、零运行时分派
template <typename Strategy>
constexpr auto checksum(const std::uint8_t* data, std::size_t len)
{
    return Strategy::compute(data, len);
}
```

编译器按模板参数在编译期定下了用哪个策略。现代编译器（GCC/Clang 在 -O2 及以上优化级别）会把对应的计算代码直接内联，虚函数表和运行时分派的开销，是一点都没有的。这话您可以亲自验证：去看编译生成的汇编，对给定的模板参数，只有对应策略的代码被生成，其他策略的代码完全不会出现在最终的二进制文件里。而且每个策略的 `name` 都是编译期常量，咱们可以把它放进 `static_assert` 或者日志系统里用。代码注释里的 CCITT，咱们顺带解码：它是老牌的国际电信标准机构，CRC-16-CCITT 就是它排定的 16 位 CRC 变体。

### 编译期的单点校验

在信号处理和数据校验的场景里，咱们经常要把多个 `constexpr` 函数串起来用。要让串联的每一步都可验，咱们就得把每个环节写成纯函数：无副作用，输入定了输出就定了。本节从最简单的单点校验看起，拿 `static_assert` 在编译期核对一个函数的输出，往后的环节再多，验法也是一样的。

```cpp
constexpr std::uint8_t xor_checksum(const std::uint8_t* data, std::size_t len)
{
    std::uint8_t sum = 0;
    for (std::size_t i = 0; i < len; ++i) { sum ^= data[i]; }
    return sum;
}

// 编译期验证
constexpr std::uint8_t kTestData[] = {0x01, 0x02, 0x03, 0x04};
static_assert(xor_checksum(kTestData, 4) == 0x04, "XOR checksum mismatch");
```

## 第五步——嵌入式实战应用

前面大都是平台无关的写法，接下来咱们落到具体的寄存器和时钟上，看编译期计算能干哪些具体的事。

### 编译期寄存器地址计算

写裸机代码的时候，外设寄存器的地址通常拿基地址加偏移量算出来。传统的做法是宏，宏给不了类型安全。改用 `constexpr` 的话，咱们可以同时要类型安全和零运行时开销：

```cpp
#include <cstdint>

struct PeripheralBase {
    std::uint32_t address;

    constexpr explicit PeripheralBase(std::uint32_t addr) : address(addr) {}

    constexpr std::uint32_t offset(std::uint32_t off) const
    {
        return address + off;
    }
};

// 外设基地址定义
constexpr PeripheralBase kGpioA{0x40010800};
constexpr PeripheralBase kUsart1{0x40013800};
constexpr PeripheralBase kTimer1{0x40012C00};

// 寄存器偏移
struct GpioReg {
    static constexpr std::uint32_t kCrl  = 0x00;
    static constexpr std::uint32_t kCrh  = 0x04;
    static constexpr std::uint32_t kIdr  = 0x08;
    static constexpr std::uint32_t kOdr  = 0x0C;
};

// 编译期地址计算
constexpr std::uint32_t kGpioA_Crl = kGpioA.offset(GpioReg::kCrl);   // 0x40010800
constexpr std::uint32_t kGpioA_Odr = kGpioA.offset(GpioReg::kOdr);   // 0x4001080C

static_assert(kGpioA_Crl == 0x40010800u);
static_assert(kGpioA_Odr == 0x4001080Cu);
```

所有的地址计算都在编译期完成。咱们要是不小心把偏移量写错了，比如加出来的地址越了界，`static_assert` 当场就把错误报了出来。更重要的是，寄存器地址的定义因此变得可读、可审计：某个地址是怎么算出来的，看一眼代码就清楚了，也就不用再去追一层层的宏展开了。

### 编译期配置校验

在嵌入式的项目里，配置参数之间的约束关系往往复杂，而且容易配错。咱们把这些约束用 `constexpr` 加 `static_assert` 表达出来，错误配置在编译期就被拦下了。

```cpp
struct ClockConfig {
    std::uint32_t hse_freq;      // 外部晶振频率
    std::uint32_t pll_mul;       // PLL 倍频系数
    std::uint32_t ahb_div;       // AHB 分频系数
    std::uint32_t apb1_div;      // APB1 分频系数

    constexpr ClockConfig(std::uint32_t hse, std::uint32_t mul,
                          std::uint32_t ahb, std::uint32_t apb1)
        : hse_freq(hse), pll_mul(mul), ahb_div(ahb), apb1_div(apb1) {}

    constexpr std::uint32_t sys_clock() const { return hse_freq * pll_mul; }
    constexpr std::uint32_t ahb_clock() const { return sys_clock() / ahb_div; }
    constexpr std::uint32_t apb1_clock() const { return ahb_clock() / apb1_div; }

    constexpr bool is_valid() const
    {
        // STM32F1 的典型约束
        if (sys_clock() > 72000000u) return false;     // SYSCLK <= 72MHz
        if (apb1_clock() > 36000000u) return false;    // APB1 <= 36MHz
        if (pll_mul < 2 || pll_mul > 16) return false;
        return true;
    }
};

// 8MHz HSE * 9 = 72MHz SYSCLK, /1 = 72MHz AHB, /2 = 36MHz APB1
constexpr ClockConfig kStandardClock{8000000, 9, 1, 2};

static_assert(kStandardClock.is_valid(), "Invalid clock configuration");
static_assert(kStandardClock.sys_clock() == 72000000u);
static_assert(kStandardClock.apb1_clock() == 36000000u);

// 错误配置在编译期被拦截：
// constexpr ClockConfig kBadClock{8000000, 18, 1, 1};
// static_assert(kBadClock.is_valid());  // 编译错误！SYSCLK = 144MHz > 72MHz
```

> 注释里的缩写，咱们顺手解码：HSE 指的是高速外部晶振，PLL 是锁相环的倍频器，AHB 和 APB1 则是芯片内部的两种总线，一个接的是高速设备，一个接的是低速外设。

配置校验放到多人协作的项目里，价值也就显得尤其大了。时钟配置是全局性的参数，把它做成 `constexpr` 的常量、加上编译期校验之后，谁改出一份不合法的配置，构建都会直接失败的。等于全组的每一次构建，都自动替大家把配置检查了一遍。

咱们把标准配置和越界配置的构建对比做成了动画，您可以按步进键看 `static_assert` 在哪一步把非法的倍频拦下来：

<Anim id="clock-config-gate" />

### 编译期波特率计算与误差校验

波特率的计算里有一件事常被忽视：当目标波特率不能整除时钟频率的时候，寄存器里的分频值只能取整，实际波特率就和目标有了偏差。咱们用 `constexpr` 直接把波特率寄存器值和误差百分比算出来，再配上 `static_assert` 的校验，就能确保误差在可接受的范围内。

```cpp
struct BaudRateConfig {
    std::uint32_t clock_freq;
    std::uint32_t target_baud;

    constexpr BaudRateConfig(std::uint32_t clk, std::uint32_t baud)
        : clock_freq(clk), target_baud(baud) {}

    constexpr std::uint32_t brr_value() const
    {
        return clock_freq / target_baud;
    }

    constexpr double error_percent() const
    {
        // 注意：这里假设波特率寄存器值直接作为分频系数
        // 实际的USART配置还需要考虑过采样倍数（8或16）
        std::uint32_t brr = brr_value();
        double actual = static_cast<double>(clock_freq) / static_cast<double>(brr);
        double target = static_cast<double>(target_baud);
        return (actual - target) / target * 100.0;
    }

    constexpr bool is_acceptable() const
    {
        double err = error_percent();
        return err > -3.0 && err < 3.0;  // 波特率误差应在 ±3% 以内
    }
};

constexpr BaudRateConfig kDebugUart{72000000, 115200};
static_assert(kDebugUart.brr_value() == 625, "BRR value should be 625");
static_assert(kDebugUart.is_acceptable(), "Baud rate error too large");
```

## 编译期计算的工程权衡

虽然说编译期计算好用，可它不是万能的，笔者在实际项目里攒过几条经验。头一个代价就是编译时间：大量复杂的 `constexpr` 计算，特别是嵌套很深的模板加 `constexpr` 的组合，会明显地拉长编译时间。项目迭代得勤的时候，咱们可能得把“可选的编译期优化”放到 Release 构建里，Debug 的构建用运行时的实现，好把迭代的速度保住。

真到了排查的时候，您会发现调试器在这里使不上劲：`constexpr` 函数在编译期执行的时候，您没法拿调试器单步跟踪。编译期计算出了问题，编译器给的错误信息可能非常晦涩。所以碰到特别复杂的计算逻辑，笔者的建议是：拿运行时版本开发和测试，等逻辑确认正确了，再把它改写成 `constexpr` 的版本。

表的大小也一样要掂量：编译期生成的表数据通常会被放进 `.rodata`，而到了 MCU（Microcontroller Unit）上，也就是咱们说的单片机，`.rodata` 里放的就是 Flash。Flash 预算紧张的嵌入式项目里，一张 256 项的 `uint32_t` 表占 1KB，可能没什么影响。可 4096 项的 `float` 表要占 16KB，对只有 64KB Flash 的 MCU 来说，就不是小数目了。所以决定把什么放进编译期查表之前，咱们最好把 Flash 预算算一遍。

## 在线运行

讲了这么多，您不妨亲手跑一跑。下面的示例可以在线运行，请您观察 CRC-32 查找表和编译期状态机：

<OnlineCompilerDemo
  title="编译期实战：CRC-32 表与编译期状态机"
  source-path="code/examples/vol2/07_compile_time_practice.cpp"
  description="在线运行并观察编译期生成的 CRC-32 查找表和状态机转移表校验。"
  allow-run
  allow-x86-asm
/>

## 参考资源

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
- [cppreference: std::array](https://en.cppreference.com/w/cpp/container/array)
