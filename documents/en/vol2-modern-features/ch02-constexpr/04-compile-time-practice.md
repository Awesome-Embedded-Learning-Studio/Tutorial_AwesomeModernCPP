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
- 'Chapter 2: consteval and constinit: New Tools for Compile-Time Guarantees'
reading_time_minutes: 17
related:
- Metaprogramming Essentials (C++20-23)
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
  source_hash: 4b5eef0108ba1728a5faf7445bd855d5ffa5214247b224d94dd78cae830daaaa
  translated_at: '2026-09-27T10:20:11+00:00'
  engine: anthropic
  token_count: 4600
---
# Compile-Time Computation in Practice: From Lookup Tables to Compile-Time Strings

Across the previous three chapters we worked through the basic mechanics of `constexpr`, literal types, and C++20's `consteval`/`constinit` one by one. At this point, in my judgment, the knowledge base is in place — time to put these tools together and have them do something genuinely useful.

No new syntax gets introduced here; we go straight to writing things: starting from compile-time lookup tables, working through compile-time strings and state machines, then templates and design patterns working together, and finally landing in real embedded scenarios to see what these techniques actually buy us.

## Step 1 — Compile-Time Lookup Tables

Lookup tables are one of the oldest and most reliable strategies in performance optimization: trade space for time, precompute the input-to-output mapping of an expensive calculation and store it as an array, and at runtime do nothing but index into the array. The real hassle has always been generating the table. Runtime initialization has to compute it all over again at startup, and that time is spent all over again too. Generating code with an external tool and `#include`-ing it back in makes the build process more complicated. `constexpr` gives us a third option: have the compiler generate the table during compilation.

### The CRC-32 Lookup Table

CRC stands for Cyclic Redundancy Check. You'll meet it in network protocols, storage systems, and communication links; CRC-32 leans on a 256-entry lookup table to speed up its computation. We already generated the CRC-32 table once in the first chapter; here we take a different angle and care about two things: whether the table entries' values are correct, and how to verify with your own hands that the table really landed in the read-only section.

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

// Verify the first few table entries at compile time
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

Where `kCrc32Table` ends up was already settled in the first chapter: generated at compile time and written straight into the object file's read-only data section `.rodata`. Here is how to verify it yourself: run `objdump -s -j .rodata` on the generated binary, and you can confirm the table data really is in the read-only section. The `static_assert`s check the first few entries against the standard CRC-32 table — is the generation logic correct? Compile time hands you the answer. All that's left of the runtime `crc32` function is a table lookup and an XOR; it really is fast.

We drew the difference between runtime initialization and compile-time generation as a diagram:

![CRC-32 lookup table: runtime initialization vs compile-time generation](./04-compile-time-practice-table.drawio)

### A Sine Function Lookup Table

When we work on signal processing, motor control, or game development, we often need trigonometric values fast. The standard library's `std::sin` can be very slow on platforms without an FPU (Floating Point Unit), and a lookup table is the common substitute. The sine values themselves can all be computed at compile time:

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

        // Taylor expansion approximating sin(x) - use the first 5 terms (up to x^9/9!)
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

// Generate a 256-point sine lookup table at compile time
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

The Taylor expansion here uses 5 terms, with the highest term reaching x^9/9!. One thing I have to be upfront about: the 5-term expansion is just like the 3-term version from the first chapter — it only converges decently in the interval around 0. If you fold angles into the 0-to-π/2 interval before use, the measured error is far below 0.1%. But the table in this code covers the entire 0-to-2π range: 148 of the 256 entries deviate by more than 0.001, and the last one, idx 255, even comes out as 11.4 — while the true value of sine never exceeds 1. The two `static_assert`s happen to pick indices 0 and 64, both inside the 0-to-π/2 interval where convergence is most stable. If you actually want to use the whole table, the range reduction from the first chapter is unavoidable: fold the angle into the 0-to-π/2 interval, then use symmetry to fill in the values across the four quadrants. If you need higher precision, we can add terms to the expansion or switch to another approximation method such as Chebyshev polynomials. As long as the math can be written as a `constexpr` function, we can generate the table at compile time.

## Step 2 — Compile-Time String Processing

String processing in C++ is usually a runtime job. But strings like command names, protocol fields, and error message IDs have their content fixed at compile time already. Move those operations up to compile time, and a decent chunk of the runtime cost of string comparison and parsing is saved.

### Compile-Time String Hashing

C++'s `switch` statement can't take a string as its condition directly. The classic workaround is to map strings to integers with a compile-time hash, then `switch` on the resulting integer. In scenario one of the third chapter we saw a `consteval` version of FNV-1a, where the hash computation was forced to stay at compile time. This chapter's entry point is a command string passed in at runtime, so the same algorithm is marked `constexpr` here: hashes of constants are computed at compile time, while the hash of user input is computed at runtime. Let's look at the code:

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

// Deduce the length from a string literal
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

One spot in this code deserves a clear look: the runtime `fnv1a32` call computes the hash of the string passed in at runtime, while `kHashStart` and friends are constants already computed at compile time. The `switch` compares compile-time constants against the runtime hash value, so the matching logic is correct. Of course, hash collisions always exist in theory. What the `static_assert`s cover is collision detection among the commands we know about; collisions involving unknown inputs are beyond their reach. If your application demands extreme correctness — a safety-critical system, say — we can follow the hash match with a `strcmp` confirmation. That costs a little extra runtime overhead; what it buys is the complete elimination of collision-induced misbehavior.

## Step 3 — Compile-Time State Machines

State machines are one of the most used design patterns in embedded development. The traditional implementation is usually one big `switch-case` structure or an array of function pointers; what they lack is compile-time verification: we might leave some event unhandled in some state, and the compiler won't remind us. Define the state transition table with `constexpr` instead, pair it with `static_assert` for compile-time checks, and omissions and conflicts get exposed for us at the compilation stage.

### A constexpr Definition of the State Machine

```cpp
#include <array>
#include <cstdint>
#include <cstddef>

enum class State : std::uint8_t { Idle, Debouncing, Pressed, Count };
enum class Event : std::uint8_t { Press, Release, Timeout, Count };

// A state transition entry
struct Transition {
    State from;
    Event trigger;
    State to;
};

// Compile-time transition table
constexpr std::array<Transition, 5> kDebounceTable = {{
    {State::Idle,       Event::Press,   State::Debouncing},
    {State::Debouncing, Event::Timeout, State::Pressed},
    {State::Debouncing, Event::Release, State::Idle},
    {State::Pressed,    Event::Release, State::Idle},
    {State::Pressed,    Event::Timeout, State::Idle},
}};
```

### Validating the Transition Table at Compile Time

With the transition table in hand, we can run all kinds of checks at compile time. For example, we can look for a state with not a single outgoing transition — a so-called "dead state" — or for duplicate `(from, trigger)` pairs.

```cpp
// Check for duplicate (state, event) combinations
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

// Check that every state has at least one outgoing transition (excluding the Count sentinel)
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

Whoever edits this table later might be your colleague, or you yourself a few months from now. If someone introduces a duplicate entry or drops some state's handling, the `static_assert`s will fail at compile time immediately, with error messages written out plainly. They catch the errors human eyes tend to miss, and since the code no longer compiles, the problem has to be fixed on the spot.

### The Runtime State Machine Engine

The transition table is defined and validated at compile time, but once the state machine actually runs, it is a runtime matter. Let's write the runtime engine too:

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
        // No matching transition found: ignore the event (or fire an assertion)
    }

    constexpr State current_state() const { return state_; }

private:
    State state_;
};
```

The implementation of the engine is dead simple: scan the transition table from start to finish, and transition as soon as a match is found. For a small state machine with only a few states and events, a linear scan is perfectly adequate. Once the number of states and events grows, we can consider replacing the linear scan with a 2D array indexed by `(state, event)`.

We turned the table-lookup that `fsm.handle()` performs after receiving an event into an animation — press the step key to walk through, one step at a time, how the matching transition gets found:

<Anim id="fsm-table-dispatch" />

## Step 4 — constexpr Working Together with Templates

`constexpr` and templates are not an either-or choice; they operate at different layers: templates do compile-time dispatch at the type level, `constexpr` does compile-time computation at the value level. Combine the two, and the compile-time abstractions we can build become remarkably powerful.

### The Compile-Time Strategy Pattern

The Strategy Pattern usually dispatches through virtual functions or function pointers, making the decision at runtime. But if the strategy can be pinned down at compile time, we can use templates plus `constexpr` to eliminate the dispatch entirely and get zero-overhead strategy selection.

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

The compiler fixes which strategy is used at compile time based on the template argument. Modern compilers (GCC/Clang at -O2 and above) inline the corresponding computation code directly; there is no vtable or runtime-dispatch overhead at all. You can verify this claim yourself: look at the generated assembly — for a given template argument, only the corresponding strategy's code is generated, and the other strategies' code never appears in the final binary. Moreover, each strategy's `name` is a compile-time constant, so we can put it to work in `static_assert`s or in a logging system. As for the CCITT in the code comments, let's decode it in passing: it is a long-standing international telecommunications standards body, and CRC-16-CCITT is the 16-bit CRC variant it standardized.

### Single-Point Verification at Compile Time

In signal processing and data-verification scenarios we often chain multiple `constexpr` functions together. To make every step of the chain verifiable, each stage has to be written as a pure function: no side effects, and once the input is fixed the output is fixed. This section starts with the simplest case, single-point verification — using `static_assert` to check one function's output at compile time — and no matter how many stages come later, the verification method stays the same.

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

Most of what came before was platform-independent. Next we get down to concrete registers and clocks, and see what compile-time computation can actually do.

### Compile-Time Register Address Calculation

In bare-metal code, peripheral register addresses are usually computed as a base address plus an offset. The traditional approach is macros, and macros can't give you type safety. Switch to `constexpr`, and we can have type safety and zero runtime overhead at the same time:

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

// Peripheral base address definitions
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

All the address arithmetic completes at compile time. If we accidentally write a wrong offset — the computed address goes out of range, say — the `static_assert` reports the error on the spot. More importantly, register address definitions become readable and auditable: how a given address came to be is clear at a glance, with no need to chase through layers of macro expansion.

### Compile-Time Configuration Validation

In embedded projects, the constraints among configuration parameters are often intricate, and easy to get wrong. Express those constraints with `constexpr` plus `static_assert`, and bad configurations get stopped at compile time.

```cpp
struct ClockConfig {
    std::uint32_t hse_freq;      // External crystal frequency
    std::uint32_t pll_mul;       // PLL multiplication factor
    std::uint32_t ahb_div;       // AHB divider
    std::uint32_t apb1_div;      // APB1 divider

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

// A bad configuration is caught at compile time:
// constexpr ClockConfig kBadClock{8000000, 18, 1, 1};
// static_assert(kBadClock.is_valid());  // Compile error! SYSCLK = 144MHz > 72MHz
```

> Let's decode the abbreviations in the comments while we're at it: HSE is the high-speed external crystal, PLL is the phase-locked-loop frequency multiplier, and AHB and APB1 are the chip's two internal buses — one hosting high-speed devices, the other low-speed peripherals.

Put configuration validation in a multi-developer project and its value shows even more. Clock configuration is a global parameter: once it becomes a `constexpr` constant with compile-time validation, anyone who commits an illegal configuration fails the build outright. It amounts to every single build the team runs automatically re-checking the configuration for everyone.

We turned the build comparison between the standard configuration and an out-of-range one into an animation — press the step key to see at which point the `static_assert` blocks the illegal multiplier:

<Anim id="clock-config-gate" />

### Compile-Time Baud Rate Calculation and Error Checking

One thing in baud rate calculation often gets overlooked: when the target baud rate doesn't evenly divide the clock frequency, the divider value in the register has to round, and the actual baud rate drifts away from the target. Compute the baud rate register value and the error percentage directly with `constexpr`, add `static_assert` checks, and we can guarantee the error stays within an acceptable range.

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
        // Note: this assumes the baud rate register value is used directly as the divider
        // A real USART configuration must also account for oversampling (8 or 16)
        std::uint32_t brr = brr_value();
        double actual = static_cast<double>(clock_freq) / static_cast<double>(brr);
        double target = static_cast<double>(target_baud);
        return (actual - target) / target * 100.0;
    }

    constexpr bool is_acceptable() const
    {
        double err = error_percent();
        return err > -3.0 && err < 3.0;  // Baud rate error should stay within ±3%
    }
};

constexpr BaudRateConfig kDebugUart{72000000, 115200};
static_assert(kDebugUart.brr_value() == 625, "BRR value should be 625");
static_assert(kDebugUart.is_acceptable(), "Baud rate error too large");
```

## The Engineering Trade-offs of Compile-Time Computation

Compile-time computation is handy, but it is no panacea; I've collected a few lessons from real projects. The first cost is compile time: heavy, complex `constexpr` computations — especially deeply nested template-plus-`constexpr` combinations — can stretch compile times noticeably. On a project that iterates fast, we may need to push the "optional compile-time optimizations" into Release builds and let Debug builds use the runtime implementation, to keep iteration speed alive.

When it comes to debugging, you'll find the debugger has little leverage here: while a `constexpr` function executes at compile time, you can't single-step through it with a debugger. And when compile-time computation goes wrong, the compiler's error messages can be thoroughly cryptic. So for particularly intricate computational logic, my advice is: develop and test the runtime version first, and only rewrite it as the `constexpr` version once the logic is confirmed correct.

Table size needs weighing too: compile-time-generated table data usually lands in `.rodata`, and on an MCU (Microcontroller Unit), `.rodata` is stored in Flash. In an embedded project with a tight Flash budget, a 256-entry `uint32_t` table taking 1KB may be no big deal. But a 4096-entry `float` table takes 16KB, and for an MCU with only 64KB of Flash that is no small sum. So before deciding what goes into a compile-time lookup table, we'd better run the Flash budget first.

## Run It Online

That's a lot of talk — go run it yourself. The example below runs online; watch the CRC-32 lookup table and the compile-time state machine:

<OnlineCompilerDemo
  title="Compile-Time Practice: CRC-32 Table and a Compile-Time State Machine"
  source-path="code/examples/vol2/07_compile_time_practice.cpp"
  description="Run it online and watch the compile-time-generated CRC-32 lookup table and the state machine transition table checks."
  allow-run
  allow-x86-asm
/>

## References

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
- [cppreference: std::array](https://en.cppreference.com/w/cpp/container/array)
