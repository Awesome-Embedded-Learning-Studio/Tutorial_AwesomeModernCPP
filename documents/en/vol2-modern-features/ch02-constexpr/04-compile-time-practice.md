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
title: 'Practical Compile-Time Computation: From Lookup Tables to Compile-Time Strings'
translation:
  source: documents/vol2-modern-features/ch02-constexpr/04-compile-time-practice.md
  source_hash: effc9a1c155747e6ec1a51a299efa67e671f4baca68661b80c1718062cfb8a3a
  translated_at: '2026-06-16T03:57:06.307758+00:00'
  engine: anthropic
  token_count: 3989
---
# Compile-Time Calculation in Practice: From Lookup Tables to Compile-Time Strings

## Introduction

In the previous three chapters, we discussed the basic mechanisms of `constexpr`, literal types, and C++20's `consteval`/`constinit`. We have built up enough knowledge; now it is time to combine these elements to do something truly useful.

This chapter is entirely driven by practical examples. We will use `constexpr` and related techniques to implement compile-time lookup tables (CRC tables, trigonometric tables), compile-time string processing, compile-time state machines, and several compile-time design patterns. Finally, we will use embedded scenarios to demonstrate the value of these techniques in actual projects.

## Step 1 — Compile-Time Lookup Tables

Lookup tables are one of the oldest and most reliable strategies for performance optimization: trading space for time. We pre-calculate the input-output mapping of complex calculations into an array, so at runtime we only need to perform array indexing. Traditionally, lookup table generation either relied on runtime initialization (wasting startup time) or external tools to generate code that is then `#include`-ed (complexifying the build process). `constexpr` offers a third path: letting the compiler generate this table for you during the compilation phase.

### CRC-32 Lookup Table

CRC checksums are ubiquitous in network protocols, storage systems, and communication links. CRC-32 uses a 256-entry lookup table to accelerate calculation. We use `constexpr` to generate this table, resulting in zero initialization overhead at runtime.

```cpp
// code/examples/vol2/07_compile_time_practice.cpp
#include <array>
#include <cstdint>
#include <iostream>

// Compile-time CRC-32 table generation
constexpr std::array<uint32_t, 256> generate_crc32_table() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
        table[i] = crc;
    }
    return table;
}

// The table is generated at compile time
constexpr auto crc_table = generate_crc32_table();

// Runtime CRC calculation using the table
uint32_t crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc = (crc >> 8) ^ crc_table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}

int main() {
    // Verify key entries match the standard CRC-32 table
    static_assert(crc_table[1] == 0x77073096, "CRC table entry mismatch");
    static_assert(crc_table[2] == 0xEE0E612C, "CRC table entry mismatch");

    const char* test_data = "123456789";
    uint32_t checksum = crc32(reinterpret_cast<const uint8_t*>(test_data), 9);
    std::cout << "CRC-32: " << std::hex << checksum << std::endl;
    // Expected output: CBF43926
    return 0;
}
```

`crc_table` is fully generated at compile time and written to the read-only data section (`.rodata`) of the object file. You can use `objdump` to inspect the generated binary file to verify that the table data indeed resides in the read-only section. `static_assert` verifies that the values of several key entries match the standard CRC-32 table, ensuring the generation logic is bug-free. The runtime `crc32` function only performs simple table lookups and XOR operations, making it very fast.

### Sine Function Lookup Table

In signal processing, motor control, and game development, we often need to quickly obtain trigonometric function values. The standard library's `std::sin` can be very slow on platforms without an FPU, making lookup tables a common alternative.

```cpp
// Compile-time sine lookup table generation
constexpr size_t TABLE_SIZE = 360; // 1-degree resolution
constexpr double PI = 3.14159265358979323846;

// Compile-time Taylor series expansion for sin(x)
constexpr double taylor_sin(double x) {
    // Normalize x to [-PI, PI]
    while (x > PI) x -= 2 * PI;
    while (x < -PI) x += 2 * PI;

    double result = x;
    double term = x;
    double x_squared = x * x;

    // sin(x) = x - x^3/3! + x^5/5! - x^7/7! + x^9/9! ...
    for (int n = 1; n <= 5; ++n) {
        term *= -x_squared / ((2 * n) * (2 * n + 1));
        result += term;
    }
    return result;
}

constexpr std::array<double, TABLE_SIZE> generate_sin_table() {
    std::array<double, TABLE_SIZE> table{};
    for (size_t i = 0; i < TABLE_SIZE; ++i) {
        double angle = i * PI / 180.0;
        table[i] = taylor_sin(angle);
    }
    return table;
}

constexpr auto sin_table = generate_sin_table();

double fast_sin(int degrees) {
    // Normalize degrees to [0, 360)
    degrees = degrees % 360;
    if (degrees < 0) degrees += 360;
    return sin_table[degrees];
}
```

Note that the Taylor expansion here uses five terms (up to $x^9/9!$), which provides sufficient precision for most embedded applications (error is typically less than 0.1%). If you need higher precision, you can increase the number of expansion terms or use other approximation methods like Chebyshev polynomials—as long as the math is written in a `constexpr` function, the lookup table can be generated at compile time.

## Step 2 — Compile-Time String Processing

String processing in C++ is usually a runtime task, but in many scenarios, string content is known at compile time—command names, protocol fields, error message IDs, etc. Moving these string operations to compile time reduces the overhead of runtime string comparison and parsing.

### Compile-Time String Hashing

C++ does not allow `switch` statements to use strings directly. A classic workaround is to use compile-time hashing to map strings to integers, and then use integers in the `switch`.

```cpp
// Compile-time string hashing (FNV-1a variant)
constexpr uint32_t hash_string(const char* str, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint8_t>(str[i]);
        hash *= 16777619u;
    }
    return hash;
}

// Helper to get string length at compile time
constexpr size_t str_len(const char* str) {
    size_t len = 0;
    while (str[len] != '\0') len++;
    return len;
}

// Wrapper for string literals
constexpr uint32_t operator""_hash(const char* str, size_t len) {
    return hash_string(str, len);
}

void process_command(const char* cmd_str, size_t len) {
    uint32_t hash = hash_string(cmd_str, len);
    switch (hash) {
        case "START"_hash:
            // Handle START command
            break;
        case "STOP"_hash:
            // Handle STOP command
            break;
        case "STATUS"_hash:
            // Handle STATUS command
            break;
        default:
            // Unknown command
            break;
    }
}
```

One point to note here: the runtime `hash_string` call calculates the hash of the string passed in at runtime, while `"START"_hash` etc. are compile-time constants. The `switch` compares the compile-time constant with the runtime hash value, so the matching logic is correct. Of course, hash collisions are theoretically always possible. `static_assert` can cover collision detection between commands you know, but cannot prevent collisions between unknown inputs. If your application has extremely high requirements for correctness (such as safety-critical systems), you can perform a `strcmp` confirmation after the hash match—this adds a small amount of runtime overhead but completely avoids errors caused by collisions.

## Step 3 — Compile-Time State Machines

State machines are one of the most commonly used design patterns in embedded development. Traditional state machine implementations usually involve a large `switch` structure or an array of function pointers, but they lack compile-time verification—you might miss handling a specific event in a specific state, and the compiler won't tell you.

By defining the state transition table with `constexpr` and using `static_assert` for compile-time validation, we can catch omissions and conflicts during the compilation phase.

### constexpr Definition of the State Machine

```cpp
// State and Event definitions
enum class State { Idle, Running, Paused, Error };
enum class Event { Start, Pause, Resume, Stop, Reset };

struct Transition {
    State current;
    Event event;
    State next;
};

// Compile-time state transition table
constexpr std::array<Transition, 8> state_table = {{
    { State::Idle, Event::Start, State::Running },
    { State::Running, Event::Pause, State::Paused },
    { State::Running, Event::Stop, State::Idle },
    { State::Paused, Event::Resume, State::Running },
    { State::Paused, Event::Stop, State::Idle },
    { State::Error, Event::Reset, State::Idle },
    // ... more transitions
}};
```

### Compile-Time Validation of the Transition Table

With the transition table, we can perform various validations at compile time. For example, checking if there is at least one transition starting from a certain state (ensuring no "dead states"), or checking if there are duplicate `(State, Event)` pairs.

```cpp
constexpr bool has_duplicate_transitions() {
    for (size_t i = 0; i < state_table.size(); ++i) {
        for (size_t j = i + 1; j < state_table.size(); ++j) {
            if (state_table[i].current == state_table[j].current &&
                state_table[i].event == state_table[j].event) {
                return true;
            }
        }
    }
    return false;
}

// Compile-time check for duplicate transitions
static_assert(!has_duplicate_transitions(), "Duplicate state transitions detected!");

// Compile-time check to ensure all states are reachable (simplified example)
constexpr bool is_state_reachable(State s) {
    for (const auto& t : state_table) {
        if (t.next == s) return true;
    }
    return false;
}

static_assert(is_state_reachable(State::Running), "State 'Running' is unreachable!");
```

If someone modifies the transition table causing duplicate entries or misses handling for a certain state, `static_assert` will immediately report an error at compile time, providing a clear error message. This kind of "compile-time guarantee" is more reliable than any code review—it can catch errors that are easily missed by the human eye, and forces correction when the code fails to compile.

### Runtime State Machine Engine

The transition table is defined and validated at compile time, but the actual operation of the state machine is naturally a runtime task.

```cpp
class StateMachine {
public:
    StateMachine(State initial) : current_state(initial) {}

    void handle_event(Event event) {
        for (const auto& t : state_table) {
            if (t.current == current_state && t.event == event) {
                current_state = t.next;
                on_state_changed(current_state);
                return;
            }
        }
        // Handle invalid event (e.g., log error, ignore, or enter Error state)
    }

private:
    State current_state;
    void on_state_changed(State new_state) {
        // Callback logic
    }
};
```

The implementation of this state machine engine is very simple—it iterates through the transition table to find a match. For small state machines with only a few states and events, linear search is perfectly adequate. If the number of states and events is large, you can consider using a two-dimensional array (indexed by `State` and `Event`) to replace linear search.

## Step 4 — Combining constexpr with Templates

`constexpr` and templates are not competitors; they are complementary tools. Templates handle compile-time dispatch at the type level, while `constexpr` handles compile-time computation at the value level. Combining them allows for very powerful compile-time abstractions.

### Compile-Time Strategy Pattern

The Strategy Pattern usually uses virtual functions or function pointers for dispatch at runtime. But if the strategy can be determined at compile time, we can use templates + `constexpr` to completely eliminate dispatch overhead, achieving zero-overhead strategy selection.

```cpp
// Strategy interface (concept-based)
struct LowPassStrategy {
    static constexpr double alpha = 0.1;
    constexpr double operator()(double input, double prev) const {
        return prev + alpha * (input - prev);
    }
};

struct HighPassStrategy {
    static constexpr double alpha = 0.9;
    constexpr double operator()((double input, double prev) const {
        return alpha * (prev - (prev + alpha * (input - prev))); // Simplified
    }
};

template<typename Strategy>
class Filter {
public:
    constexpr Filter(double init_val = 0.0) : value(init_val) {}

    constexpr double update(double input) {
        value = strategy_(input, value);
        return value;
    }

    static constexpr double get_alpha() { return Strategy::alpha; }

private:
    double value;
    Strategy strategy_;
};

// Usage
constexpr LowPassFilter low_pass_filter;
constexpr double result = low_pass_filter.update(1.0);
```

The compiler determines which strategy to use based on template parameters at compile time. Modern compilers (GCC/Clang at `-O2` and above optimization levels) will directly inline the corresponding calculation code, with no virtual function table or runtime dispatch overhead. You can verify this in the generated assembly code—for a given template parameter, only the code for the corresponding strategy is generated; the code for other strategies does not appear in the final binary file. Each strategy's `alpha` is a compile-time constant and can be used in `static_assert` or logging systems.

### Compile-Time Calculation Chain

Chaining multiple `constexpr` functions to form a calculation pipeline, where the output of one stage serves as the input for the next. This approach is very useful in signal processing pipelines and data validation chains. The core idea is to make each stage a pure function (no side effects, deterministic output for deterministic input), and then use `static_assert` to verify the correctness of the entire chain at compile time.

```cpp
// Stage 1: Scaling
constexpr double scale(double x) { return x * 2.0; }

// Stage 2: Offset
constexpr double offset(double x) { return x + 1.0; }

// Stage 3: Clamp
constexpr double clamp(double x) {
    return (x < 0.0) ? 0.0 : (x > 10.0 ? 10.0 : x);
}

// Compile-time pipeline test
constexpr double pipeline(double input) {
    return clamp(offset(scale(input)));
}

// Verify pipeline behavior at compile time
static_assert(pipeline(0.0) == 1.0, "Pipeline logic error");
static_assert(pipeline(5.0) == 11.0, "Pipeline logic error"); // 5*2+1=11 -> clamped to 10.0
static_assert(pipeline(5.0) == 10.0, "Pipeline logic error");
```

## Step 5 — Embedded Practical Applications

The previous sections covered general C++, but this section focuses on specific applications of compile-time calculation in embedded scenarios.

### Compile-Time Register Address Calculation

In bare-metal development, peripheral register addresses are usually calculated by adding an offset to a base address. Traditionally, macros are used for this, but they lack type safety. Using `constexpr` allows for both type safety and zero runtime overhead.

```cpp
// Peripheral base addresses
constexpr uintptr_t GPIOA_BASE = 0x40020000;
constexpr uintptr_t UART_BASE  = 0x40011000;

// Register offsets
constexpr uintptr_t MODER_OFFSET   = 0x00;
constexpr uintptr_t ODR_OFFSET     = 0x14;

// Compile-time address calculation
constexpr uintptr_t GPIOA_MODER = GPIOA_BASE + MODER_OFFSET;
constexpr uintptr_t GPIOA_ODR   = GPIOA_BASE + ODR_OFFSET;

// Type-safe register access
template<typename T>
constexpr volatile T* reg_ptr(uintptr_t address) {
    return reinterpret_cast<volatile T*>(address);
}

int main() {
    // Set GPIOA ODR
    *reg_ptr<uint32_t>(GPIOA_ODR) = 0xFFFF;
}
```

All address calculations are completed at compile time. If you accidentally write an offset incorrectly (e.g., it overflows a certain range), `static_assert` can help you catch it. More importantly, this style makes the definition of register addresses readable and auditable—you no longer need to trace through layers of macro expansions to figure out how a specific address was calculated.

### Compile-Time Configuration Validation

In embedded projects, constraint relationships between configuration parameters are often complex and error-prone. Expressing these constraints with `constexpr` + `static_assert` allows you to intercept incorrect configurations at compile time.

```cpp
// System Clock Configuration
constexpr uint32_t HSI_FREQ = 16000000;   // 16 MHz
constexpr uint32_t PLL_M = 8;
constexpr uint32_t PLL_N = 200;
constexpr uint32_t PLL_P = 2;

// Compile-time calculation of SYSCLK
constexpr uint32_t SYSCLK = (HSI_FREQ / PLL_M) * PLL_N / PLL_P;

// Compile-time validation
static_assert(SYSCLK <= 216000000, "SYSCLK exceeds maximum frequency (216MHz)");
static_assert(PLL_M >= 1 && PLL_M <= 16, "PLL_M out of range");
```

This pattern is particularly valuable in collaborative projects. Clock configuration is a global parameter. Making it a `constexpr` constant and adding compile-time validation acts like a safety net for the entire team.

### Compile-Time Baud Rate Calculation and Error Validation

A common pitfall in baud rate calculation is that the target baud rate does not divide the clock frequency evenly, causing a deviation between the actual baud rate and the target. Using `constexpr` allows us to directly calculate the baud rate register value and the error percentage,配合 `static_assert` to ensure the error is within an acceptable range.

```cpp
constexpr uint32_t UART_CLOCK   = 108000000; // 108 MHz
constexpr uint32_t TARGET_BAUD  = 115200;

// USARTDIV = UART_CLOCK / (16 * Baud)
constexpr double USARTDIV = static_cast<double>(UART_CLOCK) / (16.0 * TARGET_BAUD);

// Calculate integer and fractional parts for the register
constexpr uint32_t DIV_MANTISSA = static_cast<uint32_t>(USARTDIV);
constexpr uint32_t DIV_FRACTION = static_cast<uint32_t>((USARTDIV - DIV_MANTISSA) * 16.0 + 0.5);

// Calculate actual baud rate
constexpr uint32_t ACTUAL_BAUD = UART_CLOCK / (16 * (DIV_MANTISSA + DIV_FRACTION / 16.0));

// Calculate error percentage
constexpr double ERROR_PERCENT = (static_cast<double>(ACTUAL_BAUD) - TARGET_BAUD) / TARGET_BAUD * 100.0;

static_assert(ERROR_PERCENT > -2.0 && ERROR_PERCENT < 2.0, "Baud rate error exceeds 2%");
```

## Engineering Trade-Offs of Compile-Time Calculation

While compile-time calculation is powerful, it is not a silver bullet. Here are a few lessons learned from actual projects.

Compilation time is a factor to watch. Large amounts of complex `constexpr` calculations (especially deeply nested templates + `constexpr` combinations) can significantly increase compilation time. In projects with frequent development iterations, you may need to keep "optional compile-time optimizations" in the Release build, while the Debug build uses runtime implementations to speed up iteration.

Debugging difficulty also needs consideration. When `constexpr` functions execute at compile time, you cannot single-step through them with a debugger. If something goes wrong with the compile-time calculation, the compiler's error messages can be very cryptic. For particularly complex calculation logic, my suggestion is to develop and test with a runtime version first, confirm the logic is correct, and then rewrite it as a `constexpr` version.

The trade-off between lookup table size and Flash budget cannot be ignored. Table data generated at compile time is usually placed in `.rodata` (Flash). In embedded projects with tight Flash budgets, a 256-entry `uint32_t` table takes 1KB, which might be negligible; but a 4096-entry `uint32_t` table takes 16KB, which is not a small amount for an MCU with 64KB of Flash. Before deciding what to put into a compile-time lookup table, calculate the Flash budget first.

## Run Online

Run the compile-time practical examples online to observe the CRC-32 lookup table and compile-time state machine:

<OnlineCompilerDemo
  title="Compile-Time Practice: CRC-32 Table and State Machine"
  source-path="code/examples/vol2/07_compile_time_practice.cpp"
  description="Run online and observe the compile-time generated CRC-32 lookup table and state machine transition table validation."
  allow-run
  allow-x86-asm
/>

## Summary

In this chapter, we applied all the compile-time calculation techniques learned previously from a practical perspective. Lookup table generation (CRC, trigonometric functions, polynomials) demonstrates the power of `constexpr` in data preprocessing; string hashing and compile-time state machines show its value in code structure design; and embedded register address calculation and configuration validation highlight its safety assurance capabilities in actual engineering.

The core idea is: **If a calculation can be completed at compile time and its result does not change at runtime, you should consider moving it to compile time.** This is not about showing off, but about making runtime code simpler, faster, and safer. The compiler is your colleague; let it do more work, and your MCU can do less.

## Reference Resources

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
- [cppreference: std::array](https://en.cppreference.com/w/cpp/container/array)
