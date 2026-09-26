---
chapter: 7
cpp_standard:
- 11
- 14
- 17
description: Semantics, usage, and best practices for C++11-17 standard attributes
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Chapter 1: Deep Dive into RAII: The Cornerstone of Resource Management'
reading_time_minutes: 12
related:
- 'C++20-23 New Attributes: Performance-Oriented Compiler Hints'
tags:
- host
- cpp-modern
- intermediate
title: 'Deep Dive into Standard Attributes: Making the Compiler Your Code Reviewer'
translation:
  source: documents/vol2-modern-features/ch07-attributes/01-standard-attributes.md
  source_hash: c35840c1ce24d0b5a083c1875ff89d126c30468c2584256fcd42e4d323813cfc
  translated_at: '2026-09-25T16:02:56+00:00'
  engine: anthropic
  token_count: 6800
---
# Deep Dive into Standard Attributes: Making the Compiler Your Code Reviewer

When we write code, a few situations keep coming up that make our heads hurt: we call a function that returns an error code, forget to check it, and the compiler waves it through without a peep; a parameter goes unused under one build configuration, and the compiler floods the screen with unused-variable warnings; we want to flag an API as obsolete, but documentation or comments are the only way to remind callers. The standard attribute syntax `[[attribute]]`, introduced in C++11 and extended step by step in later versions, exists to solve exactly these problems—a standardized way to pass extra information to the compiler and let it run static checks for us.

> One-sentence summary: **Attributes are declarative hints to the compiler. They do not change program semantics, but they help the compiler catch errors or generate better code.**

The four attributes this article focuses on fall into two camps: the first two leave the compiler dead silent when you omit them and start warning the moment you write them; the other two, when omitted, pester you with false-positive warnings, and writing them restores the quiet:

![Comparison of the four standard attributes' effects](./01-standard-attributes-effects.drawio)

------

## Basic Syntax of Attributes

C++ standard attributes use double square brackets `[[attr]]`. Multiple attributes can be written together `[[attr1, attr2]]` or separately `[[attr1]] [[attr2]]`—the effect is the same. Attributes can sit in many positions—function declarations, variable declarations, class declarations, enum declarations, the case statements of a switch, and so on—depending on the kind of attribute.

> **Verification**: Compilation tests show that `[[attr1, attr2]]` and `[[attr1]] [[attr2]]` produce exactly the same warnings, and attribute order has no effect either.

Before attributes were standardized, every compiler had its own syntax: GCC/Clang used `__attribute__((xxx))`, MSVC used `__declspec(xxx)`. The advantage of standard attributes is portability—every conforming compiler must support them. The standard also reserves a namespace-prefix mechanism, though, such as `[[gnu::always_inline]]` or `[[clang::fallthrough]]`, so compiler extensions can be expressed in the same unified syntax.

> **Attributes by version**: C++11 introduced `[[noreturn]]` and `[[carries_dependency]]`; C++14 introduced `[[deprecated]]`; C++17 introduced `[[nodiscard]]`, `[[maybe_unused]]`, and `[[fallthrough]]`. Different attributes were standardized in different versions, so pay attention to your target compiler's support when using them.

```cpp
// Single attribute
[[nodiscard]] int check_status();

// Multiple attributes
[[nodiscard, deprecated("Use new_version()")]]
int old_function();

// Compiler extension attributes
[[gnu::always_inline]] inline void hot_path();
[[gnu::format(printf, 1, 2)]] void log_msg(const char* fmt, ...);
```

------

## [[nodiscard]]: Warn When the Return Value Is Ignored

This is arguably the most practically valuable attribute in systems programming. It tells the compiler: if the caller ignores this function's return value, issue a warning.

### Basic Usage

```cpp
[[nodiscard]] ErrorCode initialize_hardware() {
    if (!check_power_supply()) return ErrorCode::PowerFailure;
    if (!setup_clocks())       return ErrorCode::ClockError;
    return ErrorCode::Ok;
}

// Leaving the return value unchecked—the compiler warns
initialize_hardware();

// Correct usage
if (initialize_hardware() != ErrorCode::Ok) {
    handle_error();
}
```

In systems development, hardware initialization, sensor reads, and communication operations can all fail. Ignoring a return value means you may keep running while the system is already in a broken state, and the consequences are hard to predict. `[[nodiscard]]` turns "should have checked but forgot" into a compiler warning instead of a runtime bug that only surfaces after deployment.

### The C++20 Enhancement: Custom Messages

C++20 lets you attach a custom message to `[[nodiscard]]`, so the compiler can show a more specific explanation when it issues the warning:

```cpp
[[nodiscard("Must check: hardware initialization may fail")]]
ErrorCode init_board();
```

If a caller writes `init_board();` without checking the return value, the compiler displays your message instead of a generic "ignoring return value" warning.

### Applying It to Types

`[[nodiscard]]` can also be placed on the definition of a class or enumeration. Every function returning that type then automatically carries nodiscard semantics:

```cpp
[[nodiscard]] enum class ErrorCode {
    Ok,
    InvalidParam,
    Timeout,
    HardwareError
};

// Any function returning ErrorCode automatically triggers the check
ErrorCode read_sensor(uint8_t id);
read_sensor(5);  // warning: return value ignored
```

### nodiscard Is Not Mandatory

Note that `[[nodiscard]]` only produces a warning, not an error. A caller can still bypass it with an explicit cast:

```cpp
(void)init_board();             // explicit cast, silences the warning
static_cast<void>(init_board()); // same effect
```

This means your team's coding standard may need to ban this pattern. `[[nodiscard]]` says "please check," not "must check"—but that is already far better than nothing.

------

## [[maybe_unused]]: Silencing "Unused" Warnings

This attribute tells the compiler: this variable or parameter may go unused, so please don't warn about it.

### Conditional Compilation Scenarios

The most common use is conditional compilation. A parameter is used in one configuration and unused in another:

```cpp
void sensor_task([[maybe_unused]] void* param) {
#ifdef USE_RTOS
    // In RTOS mode, param is used
    auto* config = static_cast<TaskConfig*>(param);
    configure_sensor(config->port);
#else
    // In bare-metal mode, param is not used
    configure_sensor(kDefaultPort);
#endif
}
```

Without `[[maybe_unused]]`, a bare-metal build makes the compiler warn that `param` is unused. The old approaches were to write `(void)param;` in the function body, or to comment out the parameter name: `void* /*param*/`. `[[maybe_unused]]` is more semantic than `(void)` and less error-prone than commenting out parameter names.

### Unused Members in Structured Bindings

When you need only some members of a structured binding, the rest can be marked `[[maybe_unused]]`. The more common practice, though, is to use an underscore `_` as an "I don't care about this" placeholder:

```cpp
std::map<int, std::string> cache;

for (const auto& [key, value] : cache) {
    // If you only care about value, not key
}

// Or use _ as a placeholder (introduced in C++20)
// But note: _ may have special meaning in the global namespace
```

### Comparison with the Traditional Approaches

The old ways of silencing unused warnings each had drawbacks: `(void)param` is a runtime no-op statement sitting in the code, looking like something was forgotten; commenting out the parameter name as `/*param*/` is easy to overlook updating when the parameter type changes; and the compiler-specific attribute `__attribute__((unused))` is not portable. `[[maybe_unused]]` is the standardized, semantically explicit solution.

------

## [[deprecated]]: Marking Obsolete APIs

`[[deprecated]]` lets you mark obsolete functions, classes, or variables through compiler warnings. It has been supported since C++14, and it can carry a custom message explaining what to use instead.

### Basic Usage

```cpp
[[deprecated("Use new_handler() instead")]]
void old_handler();

// Calling old_handler() produces a compile warning carrying your message
old_handler();
// warning: 'old_handler' is deprecated: Use new_handler() instead
```

### Application in Library Version Migration

During a library's version upgrades, `[[deprecated]]` is a very useful tool. You can mark old APIs as deprecated rather than deleting them outright, giving users time to migrate:

```cpp
class SensorManager {
public:
    // Old API—still usable, but marked obsolete
    [[deprecated("Use read_sensor_data() which returns more information")]]
    bool read_sensor(uint8_t id, uint16_t* value);

    // New API
    SensorData read_sensor_data(uint8_t id);
};

// Enumerator values can also be marked deprecated
enum class SensorType {
    Temperature,
    Humidity,
    [[deprecated("Use Pressure instead")]]
    Barometer,   // old name
    Pressure      // new name
};
```

This "deprecate first, delete in the next major version" approach is far friendlier than removing an API outright. Callers see the warning at compile time and know they need to migrate.

### The Scope of deprecated

`[[deprecated]]` can be placed on almost any entity: functions, classes, enumerations, enumerator values, variables, template specializations, namespaces (since C++17), and so on. That means you can deprecate an entire class, not just a single function:

```cpp
[[deprecated("Use NewSensorManager instead")]]
class OldSensorManager { /* ... */ };
```

------

## [[fallthrough]]: Intentional Fallthrough in a switch

In a switch statement, when a case does not end with `break`, execution "falls through" to the next case. The compiler warns about this because it might be a forgotten `break`. But sometimes fallthrough is deliberate—`[[fallthrough]]` is how you tell the compiler "I did that on purpose, stop warning me."

### Basic Usage

```cpp
void handle_event(uint8_t event) {
    switch (event) {
        case 0x01:
            toggle_led(LED1);
            [[fallthrough]];  // explicitly signals intentional fallthrough

        case 0x02:
            toggle_led(LED2);
            break;

        case 0x03:
            toggle_led(LED3);
            break;

        default:
            handle_unknown(event);
            break;
    }
}
```

`[[fallthrough]]` must be placed after the last statement of a case and before the next case label, and it must be followed by a semicolon. Put it anywhere else, and the compiler may ignore it or reject it.

### A Typical Scenario in State Machines

When implementing a state machine, if several states share some processing logic, fallthrough is a very natural choice:

```cpp
enum class State { Idle, Initializing, Running, Paused, Error };

void handle_state(State current, Event ev) {
    switch (current) {
        case State::Idle:
            if (ev == Event::Start) {
                current = State::Initializing;
            }
            [[fallthrough]];  // Idle and Initializing share the initialization logic

        case State::Initializing:
            init_hardware();
            current = init_ok() ? State::Running : State::Error;
            break;

        case State::Running:
            run_task();
            break;

        case State::Paused:
        case State::Error:
            // The two states share processing logic; just fall through
            recover();
            break;
    }
}
```

Notice the last part of the example: there is no `[[fallthrough]]` between `Paused` and `Error`—because there are no statements between them, and the compiler does not warn for empty cases.

------

## [[noreturn]]: Functions That Never Return

`[[noreturn]]` marks functions that never return to their caller. Such functions either call `std::terminate()` or `exit()`, enter an infinite loop, or throw an exception.

```cpp
[[noreturn]] void fatal_error(const char* msg) {
    std::fprintf(stderr, "FATAL: %s\n", msg);
    std::abort();
}

[[noreturn]] void hang_forever() {
    while (true) {
        // Safe shutdown mode in embedded systems
    }
}

void check_critical(bool ok) {
    if (!ok) {
        fatal_error("Critical check failed");
        // The compiler knows this call does not return; the code after it is unreachable
    }
    // The compiler can optimize this branch without considering a return from fatal_error
    proceed();
}
```

`[[noreturn]]`'s value to the compiler lies in optimization: the compiler knows no control flow comes back after `fatal_error()`, so it doesn't need to generate code for a return path. On top of that, the compiler can use it to eliminate "function might not return a value" warnings.

> **Optimization effect**: Assembly tests show that at the `-O2` optimization level, the compiler does optimize away the unreachable code after a call to a `[[noreturn]]` function. That said, modern compilers have strong static analysis; even without the `[[noreturn]]` hint, they can infer that a function never returns in some simple scenarios.

Note: if you add `[[noreturn]]` to a function that actually does return, the behavior is undefined. The compiler may not report an error, but the generated code may be nothing like what you expect.

------

## [[carries_dependency]]

This attribute was introduced in C++11 for dependency-chain propagation in memory ordering related to `std::memory_order_consume`. It is rarely used in practice—mainstream compilers (GCC, Clang) upgrade `memory_order_consume` straight to `memory_order_acquire`, which leaves this attribute nearly decorative. Unless you are writing lock-free data structures and need precise control over dependency-chain propagation, you can safely ignore it.

> **Verification**: Assembly tests confirm that GCC generates identical assembly for `memory_order_consume` and `memory_order_acquire` (both use a `movq` load, with no extra dependency-chain handling), which explains why `[[carries_dependency]]` has practically no effect in practice.

------

## Compiler Extension Attributes

Beyond the standard attributes, mainstream compilers also support compiler-specific attributes through namespace prefixes. These attributes are not standard, but they are useful on particular platforms:

```cpp
// GCC/Clang extensions
[[gnu::always_inline]]           // force inlining
[[gnu::hot]]                     // mark as a hot function
[[gnu::cold]]                    // mark as a cold path
[[gnu::format(printf, 1, 2)]]    // printf format checking
[[clang::fallthrough]]           // Clang-specific fallthrough

// MSVC extensions
[[msvc::forceinline]]            // force inlining
```

Use these attributes with care in cross-platform code. If you must use one, wrapping it behind a macro is the recommended approach:

```cpp
#if defined(__GNUC__)
    #define FORCE_INLINE [[gnu::always_inline]]
#elif defined(_MSC_VER)
    #define FORCE_INLINE [[msvc::forceinline]]
#else
    #define FORCE_INLINE
#endif

FORCE_INLINE void hot_function();
```

------

## Putting Attributes in the Right Place

An attribute placed in different positions has different meanings. Put it in the wrong spot and the compiler may ignore it, or apply it to the wrong target:

```cpp
// Function attribute—before the return type or after the declarator
[[nodiscard]] int func();      // correct
int func [[nodiscard]]();      // also correct (but less common)

// Variable attribute—before the variable name
[[maybe_unused]] int x;

// Class attribute—after the class keyword
class [[deprecated]] OldClass {};

// Enum attribute—after the enum keyword
enum class [[deprecated]] OldEnum {};

// switch case attribute—after the last statement inside the case
switch (x) {
    case 1:
        do_something();
        [[fallthrough]];     // note the semicolon
    case 2:
        do_more();
        break;
}
```

If you are unsure where an attribute belongs, cppreference is the most reliable reference.

------

## Reference Resources

- [cppreference: C++ attributes](https://en.cppreference.com/w/cpp/language/attributes)
- [cppreference: nodiscard](https://en.cppreference.com/w/cpp/language/attributes/nodiscard)
- [cppreference: maybe_unused](https://en.cppreference.com/w/cpp/language/attributes/maybe_unused)
- [cppreference: deprecated](https://en.cppreference.com/w/cpp/language/attributes/deprecated)
