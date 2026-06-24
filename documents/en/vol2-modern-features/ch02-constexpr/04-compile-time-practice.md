---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Comprehensive application of constexpr for compile-time lookup tables,
  string processing, state machines, and design patterns
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'Chapter 2: constexpr 基础'
- 'Chapter 2: constexpr 构造函数与字面类型'
reading_time_minutes: 17
related:
- 卷四：模板元编程
tags:
- host
- cpp-modern
- intermediate
- constexpr
- 编译期计算
- 零开销抽象
title: 'Compile-Time Computation in Practice: From Lookup Tables to Compile-Time Strings'
translation:
  source: documents/vol2-modern-features/ch02-constexpr/04-compile-time-practice.md
  source_hash: 10cd6bf905c14237ce59c0da0f97863d7622c14bf9617bccb4620df8ee52767f
  translated_at: '2026-06-24T01:18:30.349624+00:00'
  engine: anthropic
  token_count: 3989
---
# Compile-Time Calculation in Practice: From Lookup Tables to Compile-Time Strings

## Introduction

In the previous three chapters, we discussed the basic mechanisms of `constexpr`, literal types, and C++20's `consteval`/`constinit`. We have built up enough knowledge, so now it is time to combine these concepts to do something truly useful.

This chapter is entirely driven by practical examples. We will use `constexpr` and related techniques to implement compile-time lookup tables (CRC tables, trigonometric tables), compile-time string processing, compile-time state machines, and some compile-time design patterns. Finally, we will demonstrate the value of these techniques in real-world embedded projects.

## Step 1 — Compile-Time Lookup Tables

Lookup tables are one of the oldest and most reliable strategies for performance optimization: trading space for time. We pre-calculate the input-output mapping of complex calculations and store them as an array, so at runtime we only need to perform an array index. Traditionally, generating lookup tables either relied on runtime initialization (wasting startup time) or involved external tools to generate code that is then `#include`-ed (complicating the build process). `constexpr` offers a third path: letting the compiler generate this table for you during the compilation phase.

### CRC-32 Lookup Table

CRC checksums are ubiquitous in network protocols, storage systems, and communication links. CRC-32 uses a 256-entry lookup table to accelerate calculation. We can use `constexpr` to generate this table, resulting in zero initialization overhead at runtime.

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

`kCrc32Table` is fully generated at compile time and written to the read-only data section (`.rodata`) of the object file. We can verify that the table data indeed resides in the read-only section by inspecting the generated binary file using `objdump -s -j .rodata`. The `static_assert` statements verify that the values of several key entries match the standard CRC-32 table, ensuring the generation logic is bug-free. The runtime `crc32` function performs only simple table lookups and XOR operations, making it extremely fast.

### Sine Function Lookup Table

In fields such as signal processing, motor control, and game development, we frequently need to retrieve trigonometric function values quickly. On platforms without an FPU, the standard library's `std::sin` can be very slow, so lookup tables are a common alternative.

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

Note that the Taylor expansion here uses five terms (up to $x^9/9!$), which provides sufficient precision for most embedded applications (the error is typically less than 0.1%). If you need higher precision, you can increase the number of terms or use other approximation methods like Chebyshev polynomials—as long as we write the math as a `constexpr` function, we can generate the lookup table at compile time.

## Step 2 — Compile-Time String Processing

String processing in C++ is usually a runtime task, but in many scenarios, the string content is already known at compile time—such as command names, protocol fields, or error message IDs. Moving these string operations to compile time reduces the overhead of runtime string comparison and parsing.

### Compile-Time String Hashing

C++ does not allow `switch` statements to use strings directly. A classic workaround is to use a compile-time hash to map strings to integers, and then use the integers in the `switch` statement.

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

One thing to note here: the runtime `fnv1a32` call calculates the hash of a string passed in at runtime, while `kHashStart` and others are compile-time constants. The `switch` statement compares these compile-time constants with the runtime hash value, so the matching logic is correct. Of course, hash collisions are theoretically always possible. While `static_assert` can cover collision detection between known commands, it cannot prevent collisions between unknown inputs. If your application demands high correctness (e.g., in safety-critical systems), you can perform a `strcmp` confirmation after the hash match. This adds a small amount of runtime overhead but completely avoids incorrect behavior caused by collisions.

## Step 3 — Compile-Time State Machine

The state machine is one of the most commonly used design patterns in embedded development. Traditional state machine implementations usually involve a large `switch-case` structure or an array of function pointers, but they lack compile-time verification—you might miss handling a specific event in a specific state, and the compiler won't tell you.

By defining the state transition table using `constexpr` and using `static_assert` for compile-time validation, we can detect omissions and conflicts during the compilation phase.

### Defining State Machines with `constexpr`

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

### Compile-Time Transition Table Validation

With the transition table in place, we can perform various validations at compile time. For example, we can check if there is at least one transition originating from a specific state (ensuring there are no "dead states"), or verify that there are no duplicate `(from, trigger)` pairs.

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

If someone modifies the transition table, introducing duplicate entries or missing a state handler, `static_assert` will immediately raise an error at compile time with a clear message. This "compile-time guarantee" is more reliable than any code review—it catches errors easily missed by the human eye, and forces corrections before the code can even compile.

### Runtime State Machine Engine

While the transition table is defined and validated at compile time, the actual execution of the state machine is, of course, a runtime matter.

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

The implementation of this state machine engine is very simple—we iterate through the transition table to find a match. For small state machines with only a few states and events, a linear search is perfectly sufficient. If the number of states and events is large, we can consider replacing the linear search with a two-dimensional array (indexed by `(state, event)`).

## Step 4 — Combining `constexpr` with Templates

`constexpr` and templates are not competitors; they are complementary tools. Templates handle compile-time dispatch at the type level, while `constexpr` handles compile-time computation at the value level. By combining them, we can achieve very powerful compile-time abstractions.

### Compile-Time Strategy Pattern

The Strategy Pattern typically uses virtual functions or function pointers to dispatch at runtime. However, if the strategy is known at compile time, we can use templates combined with `constexpr` to eliminate the dispatch entirely, achieving zero-overhead strategy selection.

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

The compiler determines which strategy to use based on template parameters at compile time. Modern compilers (GCC/Clang at `-O2` and higher optimization levels) will directly inline the corresponding calculation code, resulting in no virtual function table or runtime dispatch overhead. You can verify this in the generated assembly code—for given template parameters, only the code for the corresponding strategy is generated, while the code for other strategies is completely absent from the final binary. The `name` of each strategy is a compile-time constant and can be used in `static_assert` or logging systems.

### Compile-Time Calculation Chains

We can chain multiple `constexpr` functions to form a calculation chain, where the output of each stage serves as the input for the next. This approach is particularly useful in signal processing pipelines and data validation chains. The core idea is to ensure that each stage is a pure function (no side effects, deterministic output for a given input), and then use `static_assert` to verify the correctness of the entire chain at compile time.

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

## Step 5 — Practical Embedded Applications

While previous sections covered general C++, this section focuses on specific applications of compile-time computation within embedded scenarios.

### Compile-Time Register Address Calculation

In bare-metal development, peripheral register addresses are typically calculated using a base address plus an offset. Traditionally, this is done using macros, which lack type safety. By using `constexpr`, we can achieve both type safety and zero runtime overhead.

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

All address calculations are performed at compile time. If you accidentally write an incorrect offset (for example, one that overflows a specific range), `static_assert` can help catch it. More importantly, this approach makes register address definitions readable and auditable—you no longer need to trace through layers of macro expansions to figure out how an address was calculated.

### Compile-Time Configuration Validation

In embedded projects, the constraint relationships between configuration parameters are often complex and error-prone. By expressing these constraints using `constexpr` + `static_assert`, we can intercept incorrect configurations at compile time.

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

This pattern is particularly valuable in collaborative projects. Clock configuration is a global parameter; making it a `constexpr` constant with compile-time validation acts as a safety net for the entire team.

### Compile-Time Baud Rate Calculation and Error Validation

A common pitfall in baud rate calculation is that the target baud rate might not divide the clock frequency evenly, causing a discrepancy between the actual and target baud rates. We can use `constexpr` to directly calculate the baud rate register value and the percentage error, combined with `static_assert` to ensure the error remains within an acceptable range.

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

## Engineering Trade-offs of Compile-Time Computation

While compile-time computation is powerful, it is not a silver bullet. Here are a few lessons learned from real-world projects.

Compilation time is a critical factor. Extensive and complex `constexpr` calculations (especially combinations of deeply nested templates and `constexpr`) can significantly increase build times. In projects with frequent iteration cycles, we might need to restrict "optional compile-time optimizations" to Release builds, while using runtime implementations in Debug builds to speed up the iteration process.

Debugging complexity is another concern. When `constexpr` functions execute during compilation, we cannot single-step through them with a debugger. If something goes wrong with a compile-time calculation, the compiler's error messages can be quite cryptic. For particularly complex logic, the recommendation is to develop and test using a runtime version first to verify correctness, and then refactor it to a `constexpr` version.

The trade-off between lookup table size and Flash budget cannot be ignored. Data generated at compile time is typically placed in `.rodata` (Flash). In embedded projects with tight Flash budgets, a 256-entry `uint32_t` table consuming 1KB might be negligible; however, a 4096-entry `float` table consuming 16KB is significant for an MCU with only 64KB of Flash. Before deciding what to offload to compile-time lookup tables, calculate your Flash budget carefully.

## Run Online

Run the compile-time practical examples online to observe the CRC-32 lookup table and compile-time state machine:

<OnlineCompilerDemo
  title="Compile-Time Practice: CRC-32 Table and Compile-Time State Machine"
  source-path="code/examples/vol2/07_compile_time_practice.cpp"
  description="Run online to observe the compile-time generated CRC-32 lookup table and state machine transition table verification."
  allow-run
  allow-x86-asm
/>

## Summary

In this chapter, we applied all the compile-time computation techniques we learned earlier from a practical perspective. Lookup table generation (CRC, trigonometric functions, polynomials) demonstrated the power of `constexpr` in data preprocessing; string hashing and compile-time state machines showed its value in code structure design; and embedded register address calculation and configuration verification highlighted its safety assurance capabilities in actual engineering.

The core philosophy is: **if a calculation can be completed at compile time and its result remains constant at runtime, we should consider moving it to compile time**. This isn't about showing off, but about making runtime code simpler, faster, and safer. The compiler is your colleague; let it do the heavy lifting so your MCU can do less.

## References

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
- [cppreference: std::array](https://en.cppreference.com/w/cpp/container/array)
