---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Putting constexpr to work on compile-time lookup tables, string processing,
  state machines, and design patterns
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'Chapter 2: constexpr Basics: The Art of Compile-Time Evaluation'
- 'Chapter 2: constexpr Constructors and Literal Types'
reading_time_minutes: 17
related:
- 'Volume IV: Advanced Topics'
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
  source_hash: e740036260f9652d4b55b25d9e7850ed7c7c271150f448d5be8a54ebe93e6d9e
  translated_at: '2026-09-25T14:57:33+00:00'
  engine: anthropic
  token_count: 5500
---
# Compile-Time Computation in Practice: From Lookup Tables to Compile-Time Strings

Over the previous three chapters we covered the basic mechanics of `constexpr`, literal types, and C++20's `consteval`/`constinit`. That's plenty of groundwork — now it's time to put the pieces together and do something genuinely useful.

This chapter is entirely practice-driven. We'll use `constexpr` and related techniques to build compile-time lookup tables (CRC tables, trigonometric tables), compile-time string processing, compile-time state machines, and a few compile-time design patterns. At the end, we'll turn to embedded scenarios to show what these techniques are worth in real projects.

## Step 1 — Compile-Time Lookup Tables

Lookup tables are one of the oldest and most reliable strategies in performance optimization: trade space for time by precomputing the input-to-output mapping of an expensive calculation into an array, so at runtime all you do is index into it. Traditionally, these tables were generated either by runtime initialization (burning startup time) or by an external tool that generates code you then `#include` (complicating the build). `constexpr` offers a third path: have the compiler generate the table during compilation.

### CRC-32 Lookup Table

CRC checks are everywhere — network protocols, storage systems, communication links. CRC-32 uses a 256-entry lookup table to speed up the computation. Generate that table with `constexpr`, and runtime initialization cost drops to zero.

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

// Generate the complete CRC-32 lookup table at compile time
constexpr auto kCrc32Table = make_crc32_table();

// Verify at compile time that the first few entries are correct
static_assert(kCrc32Table[0] == 0x00000000u, "CRC table entry 0 should be 0");
static_assert(kCrc32Table[1] == 0x77073096u, "CRC table entry 1 mismatch");
static_assert(kCrc32Table[255] == 0x2D02EF8Du, "CRC table entry 255 mismatch");

// Runtime CRC computation: just a table lookup + XOR
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

`kCrc32Table` is fully generated at compile time and written into the object file's read-only data section (`.rodata`). You can run `objdump -s -j .rodata` on the produced binary and see the table data sitting in the read-only section. The `static_assert`s confirm that a few key entries match the standard CRC-32 table, so the generation logic can't be silently wrong. The runtime `crc32` function does nothing but table lookups and XORs — it is very fast.

The difference between the runtime-initialization route and the compile-time-generation route can be drawn like this:

![CRC-32 lookup table: runtime initialization vs compile-time generation](./04-compile-time-practice-table.drawio)

### Sine Function Lookup Table

Signal processing, motor control, game development — plenty of domains need trigonometric values fast. The standard library's `std::sin` can be painfully slow on platforms without an FPU, so a lookup table is the usual substitute.

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

        // Taylor expansion approximating sin(x) - the first 5 terms (up to x^9/9!)
        // sin(x) ≈ x - x^3/3! + x^5/5! - x^7/7! + x^9/9!
        double x = angle;
        double term = x;
        double sum = term;
        for (int n = 1; n <= 4; ++n) {  // 4 iterations compute terms 2-5
            term *= -x * x / static_cast<double>((2 * n) * (2 * n + 1));
            sum += term;
        }
        table[i] = static_cast<float>(sum);
    }
    return table;
}

// Generate a 256-point sine table at compile time
constexpr auto kSinTable = make_sin_table<256>();

static_assert(kSinTable[0] < 0.001f && kSinTable[0] > -0.001f,
              "sin(0) should be approximately 0");
static_assert(kSinTable[64] > 0.99f && kSinTable[64] < 1.01f,
              "sin(π/2) should be approximately 1");

// Fast sin lookup (angle range [0, 2π) mapped to [0, 255])
constexpr float fast_sin_index(std::size_t index)
{
    return kSinTable[index & 0xFF];
}
```

Note that the Taylor expansion here uses 5 terms (up to x^9/9!), which is accurate enough for most embedded applications (error is typically below 0.1%). If you need more precision, add more terms to the expansion, or switch to another approximation such as Chebyshev polynomials — as long as the math is written as a `constexpr` function, the table can be generated at compile time.

## Step 2 — Compile-Time String Processing

String handling in C++ is usually a runtime job, but in many scenarios the contents of those strings are already fixed at compile time — command names, protocol fields, error-message IDs, and so on. Moving these string operations ahead to compile time cuts the runtime cost of string comparison and parsing.

### Compile-Time String Hashing

C++ does not allow a `switch` statement to dispatch directly on strings. A classic workaround is to map strings to integers with a compile-time hash, then `switch` on the integers.

```cpp
#include <cstdint>
#include <cstddef>

// FNV-1a hash: simple, evenly distributed, widely used
constexpr std::uint32_t fnv1a32(const char* str, std::size_t len)
{
    std::uint32_t hash = 0x811c9dc5u;
    for (std::size_t i = 0; i < len; ++i) {
        hash ^= static_cast<std::uint8_t>(str[i]);
        hash *= 0x01000193u;
    }
    return hash;
}

// Deduce the length from the string literal
template <std::size_t N>
constexpr std::uint32_t str_hash(const char (&s)[N])
{
    return fnv1a32(s, N - 1);  // N - 1 excludes the trailing '\0'
}

// Generate the hash values of all commands at compile time
constexpr auto kHashInit   = str_hash("INIT");
constexpr auto kHashStart  = str_hash("START");
constexpr auto kHashStop   = str_hash("STOP");
constexpr auto kHashReset  = str_hash("RESET");

// Compile-time collision detection
static_assert(kHashInit != kHashStart, "Hash collision detected");
static_assert(kHashInit != kHashStop, "Hash collision detected");
static_assert(kHashStart != kHashStop, "Hash collision detected");
static_assert(kHashStart != kHashReset, "Hash collision detected");

// Runtime command dispatch
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

One point deserves attention here: the runtime `fnv1a32` call computes the hash of the string passed in at runtime, while `kHashStart` and friends are constants computed at compile time. The `switch` compares the compile-time constants against the runtime hash, so the matching logic is correct. Of course, hash collisions are always theoretically possible; the `static_assert`s cover collision detection among the commands you know about, but they cannot guard against collisions between unknown inputs. If your application demands extremely high correctness (a safety-critical system, for instance), follow up a hash match with a `strcmp` confirmation — it costs a little runtime overhead, but it completely rules out misbehavior caused by collisions.

## Step 3 — Compile-Time State Machines

The state machine is one of the most-used design patterns in embedded development. Traditional implementations are usually one big `switch-case` structure or an array of function pointers, but neither comes with compile-time verification — you can leave out the handler for some event in some state, and the compiler won't tell you.

Define the state-transition table with `constexpr`, validate it with `static_assert`s, and omissions and conflicts get caught at compile time.

### Defining the State Machine with `constexpr`

```cpp
#include <array>
#include <cstdint>
#include <cstddef>

enum class State : std::uint8_t { Idle, Debouncing, Pressed, Count };
enum class Event : std::uint8_t { Press, Release, Timeout, Count };

// A state-transition entry
struct Transition {
    State from;
    Event trigger;
    State to;
};

// The compile-time transition table
constexpr std::array<Transition, 5> kDebounceTable = {{
    {State::Idle,       Event::Press,   State::Debouncing},
    {State::Debouncing, Event::Timeout, State::Pressed},
    {State::Debouncing, Event::Release, State::Idle},
    {State::Pressed,    Event::Release, State::Idle},
    {State::Pressed,    Event::Timeout, State::Idle},
}};
```

### Validating the Transition Table at Compile Time

With the transition table in hand, we can run all sorts of checks at compile time. For example, check that every state has at least one outgoing transition (making sure there are no "dead states"), or check for duplicate `(from, trigger)` pairs.

```cpp
// Check for duplicate (state, event) pairs
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

// Check that every state has at least one outgoing transition (the Count sentinel is excluded)
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

If someone edits the transition table and introduces a duplicate entry or leaves a state unhandled, the `static_assert`s fail immediately at compile time with a clear message. This kind of "compile-time guarantee" beats any code review — it catches the mistakes human eyes tend to miss, and the code is forcibly fixed because it won't compile otherwise.

### The Runtime State Machine Engine

The transition table is defined and validated at compile time, but actually running the state machine is of course a runtime affair.

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
        // No matching transition: ignore the event (or fire an assertion)
    }

    constexpr State current_state() const { return state_; }

private:
    State state_;
};
```

This state machine engine is deliberately simple — scan the transition table for a match. For a small state machine with only a few states and events, a linear scan is perfectly adequate. When the numbers grow, consider replacing the scan with a two-dimensional array indexed by `(state, event)`.

## Step 4 — `constexpr` and Templates Working Together

`constexpr` and templates are not competitors; they are complementary tools. Templates handle compile-time dispatch at the type level, while `constexpr` handles compile-time computation at the value level. Combine the two and you can build remarkably powerful compile-time abstractions.

### Compile-Time Strategy Pattern

The Strategy Pattern usually dispatches at runtime via virtual functions or function pointers. But if the strategy is already determined at compile time, we can use templates + `constexpr` to eliminate the dispatch entirely and get strategy selection with zero overhead.

```cpp
// CRC-32 strategy
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

// CRC-16-CCITT strategy
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

// Compile-time strategy selection — zero vtables, zero runtime dispatch
template <typename Strategy>
constexpr auto checksum(const std::uint8_t* data, std::size_t len)
{
    return Strategy::compute(data, len);
}
```

The compiler decides which strategy to use from the template argument at compile time, and modern compilers (GCC/Clang at -O2 and above) simply inline the corresponding computation — no virtual table, no runtime dispatch overhead. You can verify this in the generated assembly: for a given template argument, only that strategy's code is emitted, and the other strategies never appear in the final binary at all. Each strategy's `name` is a compile-time constant, usable in `static_assert`s or in a logging system.

### Compile-Time Computation Chains

Chain several `constexpr` functions together into a computation pipeline where each stage's output feeds the next stage's input. This pattern is very useful in signal-processing pipelines and data-validation chains. The core idea is to make every stage a pure function (no side effects; a fixed input always yields a fixed output), then use `static_assert`s to verify the correctness of the whole chain at compile time.

```cpp
constexpr std::uint8_t xor_checksum(const std::uint8_t* data, std::size_t len)
{
    std::uint8_t sum = 0;
    for (std::size_t i = 0; i < len; ++i) { sum ^= data[i]; }
    return sum;
}

// Compile-time verification
constexpr std::uint8_t kTestData[] = {0x01, 0x02, 0x03, 0x04};
static_assert(xor_checksum(kTestData, 4) == 0x04, "XOR checksum mismatch");
```

## Step 5 — Practical Embedded Applications

Everything above is generic C++; this section is specifically about concrete applications of compile-time computation in embedded scenarios.

### Compile-Time Register Address Calculation

In bare-metal development, peripheral register addresses are usually computed as a base address plus an offset. Traditionally that's done with macros, which give up type safety. With `constexpr` you can have both type safety and zero runtime overhead.

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

// Peripheral base addresses
constexpr PeripheralBase kGpioA{0x40010800};
constexpr PeripheralBase kUsart1{0x40013800};
constexpr PeripheralBase kTimer1{0x40012C00};

// Register offsets
struct GpioReg {
    static constexpr std::uint32_t kCrl  = 0x00;
    static constexpr std::uint32_t kCrh  = 0x04;
    static constexpr std::uint32_t kIdr  = 0x08;
    static constexpr std::uint32_t kOdr  = 0x0C;
};

// Compile-time address calculation
constexpr std::uint32_t kGpioA_Crl = kGpioA.offset(GpioReg::kCrl);   // 0x40010800
constexpr std::uint32_t kGpioA_Odr = kGpioA.offset(GpioReg::kOdr);   // 0x4001080C

static_assert(kGpioA_Crl == 0x40010800u);
static_assert(kGpioA_Odr == 0x4001080Cu);
```

All the address arithmetic happens at compile time. If you get an offset wrong by accident (say, it slips out of some range), a `static_assert` can catch it. More importantly, this style makes register address definitions readable and auditable — you no longer have to chase layer after layer of macro expansion to work out where some address came from.

### Compile-Time Configuration Validation

In embedded projects, the constraints among configuration parameters are often complex and easy to get wrong. Express those constraints with `constexpr` + `static_assert`, and invalid configurations get intercepted at compile time.

```cpp
struct ClockConfig {
    std::uint32_t hse_freq;      // External crystal frequency
    std::uint32_t pll_mul;       // PLL multiplication factor
    std::uint32_t ahb_div;       // AHB division factor
    std::uint32_t apb1_div;      // APB1 division factor

    constexpr ClockConfig(std::uint32_t hse, std::uint32_t mul,
                          std::uint32_t ahb, std::uint32_t apb1)
        : hse_freq(hse), pll_mul(mul), ahb_div(ahb), apb1_div(apb1) {}

    constexpr std::uint32_t sys_clock() const { return hse_freq * pll_mul; }
    constexpr std::uint32_t ahb_clock() const { return sys_clock() / ahb_div; }
    constexpr std::uint32_t apb1_clock() const { return ahb_clock() / apb1_div; }

    constexpr bool is_valid() const
    {
        // Typical STM32F1 constraints
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

// A bad configuration is intercepted at compile time:
// constexpr ClockConfig kBadClock{8000000, 18, 1, 1};
// static_assert(kBadClock.is_valid());  // Compile error! SYSCLK = 144MHz > 72MHz
```

This pattern is especially valuable in multi-developer projects. Clock configuration is a global parameter; making it a `constexpr` constant with compile-time validation is like installing a safety net for the entire team.

### Compile-Time Baud Rate Calculation and Error Checking

A common trap in baud rate calculation: the target baud rate doesn't evenly divide the clock frequency, so the actual baud rate drifts from the target. With `constexpr` you can compute the baud-rate register value and the error percentage directly, and pair them with `static_assert`s to keep the error within an acceptable range.

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
        // Note: this assumes the baud-rate register value acts directly as the divider
        // Real USART configuration must also account for oversampling (8 or 16)
        std::uint32_t brr = brr_value();
        double actual = static_cast<double>(clock_freq) / static_cast<double>(brr);
        double target = static_cast<double>(target_baud);
        return (actual - target) / target * 100.0;
    }

    constexpr bool is_acceptable() const
    {
        double err = error_percent();
        return err > -3.0 && err < 3.0;  // Baud-rate error should stay within ±3%
    }
};

constexpr BaudRateConfig kDebugUart{72000000, 115200};
static_assert(kDebugUart.brr_value() == 625, "BRR value should be 625");
static_assert(kDebugUart.is_acceptable(), "Baud rate error too large");
```

## Engineering Trade-offs of Compile-Time Computation

Powerful as compile-time computation is, it is not a silver bullet. Here are a few lessons we have learned from real projects.

Compile time is one factor to watch. Heavy, complex `constexpr` computation (especially deeply nested template + `constexpr` combinations) can noticeably increase build times. On a rapidly iterating project, it may make sense to keep "optional compile-time optimizations" for Release builds while Debug builds use runtime implementations to speed up the iteration loop.

Debugging difficulty deserves consideration too. While a `constexpr` function executes at compile time, you cannot single-step through it with a debugger. If the compile-time computation goes wrong, the compiler's error messages can be deeply opaque. For particularly complex logic, our advice is to develop and test the runtime version first, and only rewrite it as the `constexpr` version once the logic is confirmed correct.

The trade-off between table size and the Flash budget must not be overlooked either. Compile-time-generated table data typically lands in `.rodata` (Flash). On an embedded project with a tight Flash budget, a 256-entry `uint32_t` table taking 1KB may not matter; but a 4096-entry `float` table taking 16KB is no small sum for an MCU with 64KB of Flash. Before deciding what to move into a compile-time table, do the Flash budget math first.

## Run It Online

Run the compile-time practice examples online and observe the CRC-32 lookup table and the compile-time state machine:

<OnlineCompilerDemo
  title="Compile-Time Practice: CRC-32 Table and a Compile-Time State Machine"
  source-path="code/examples/vol2/07_compile_time_practice.cpp"
  description="Run online and observe the compile-time-generated CRC-32 lookup table and the state-machine transition-table checks."
  allow-run
  allow-x86-asm
/>

## References

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
- [cppreference: std::array](https://en.cppreference.com/w/cpp/container/array)
