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
- 'Chapter 1: RAII 深入理解'
reading_time_minutes: 11
related:
- C++20-23 新属性
tags:
- host
- cpp-modern
- intermediate
title: 'Standard Attributes Explained: Making the Compiler Your Code Reviewer'
translation:
  engine: anthropic
  source: documents/vol2-modern-features/ch07-attributes/01-standard-attributes.md
  source_hash: bc7841e2ffaf30c56978133e2dcf75046f0496bf9da4a83697a68399c163787b
  token_count: 2431
  translated_at: '2026-05-26T11:31:29.584312+00:00'
---
# Standard Attributes in Depth: Making the Compiler Your Code Reviewer

When writing code, we often run into a few frustrating situations: calling a function that returns an error code but forgetting to check it, and the compiler silently lets it pass; having a parameter that is unused under a certain build configuration, and the compiler floods the screen with unused variable warnings; wanting to mark an API as obsolete but having to rely solely on documentation or comments to notify callers. The standard attribute syntax `[[...]]`, introduced in C++11 and gradually expanded in subsequent versions, exists to solve these problems—providing a standardized way to pass extra information to the compiler so it can perform static checks for us.

> In a nutshell: **Attributes are declarative hints to the compiler. They do not change program semantics, but they help the compiler catch errors or generate better code.**

------

## Basic Syntax of Attributes

C++ standard attributes use double square brackets `[[...]]`. Multiple attributes can be written together `[[attr1, attr2]]`, or separately `[[attr1]] [[attr2]]`, with the same effect. Attributes can be placed in many positions—function declarations, variable declarations, class declarations, enumeration declarations, `switch` case statements, and more—depending on the attribute type.

> **Verification**: Compilation tests show that `[[nodiscard, maybe_unused]]` and `[[nodiscard]] [[maybe_unused]]` produce identical warnings, and the order of attributes does not affect the result.

Before standard attributes, different compilers had their own syntaxes: GCC/Clang used `__attribute__((...))`, and MSVC used `__declspec(...)`. The advantage of standard attributes is portability—all conforming compilers must support them. However, the standard also reserves a namespace prefix mechanism, such as `[[gcc::...]]` or `[[msvc::...]]`, allowing compiler extensions to be expressed using the same unified syntax.

> **Attributes by version**: C++11 introduced `[[noreturn]]` and `[[carries_dependency]]`, C++14 introduced `[[deprecated]]`, and C++17 introduced `[[nodiscard]]`, `[[maybe_unused]]`, and `[[fallthrough]]`. Different attributes were standardized in different versions, so be mindful of your target compiler's support when using them.

```cpp
// 单个属性
[[nodiscard]] int check_status();

// 多个属性
[[nodiscard, deprecated("Use new_version()")]]
int old_function();

// 编译器扩展属性
[[gnu::always_inline]] inline void hot_path();
[[gnu::format(printf, 1, 2)]] void log_msg(const char* fmt, ...);
```

------

## [[nodiscard]]: Warn When Return Values Are Ignored

This is arguably the most practically valuable attribute in systems programming. It tells the compiler: if the caller ignores this function's return value, please issue a warning.

### Basic Usage

```cpp
[[nodiscard]] ErrorCode initialize_hardware() {
    if (!check_power_supply()) return ErrorCode::PowerFailure;
    if (!setup_clocks())       return ErrorCode::ClockError;
    return ErrorCode::Ok;
}

// 不检查返回值——编译器发出警告
initialize_hardware();

// 正确用法
if (initialize_hardware() != ErrorCode::Ok) {
    handle_error();
}
```

In systems development, hardware initialization, sensor reads, and communication operations can all fail. Ignoring the return value means you might continue running in an already-errored state, with unpredictable consequences. `[[nodiscard]]` turns "should have checked but forgot" into a compiler warning, rather than a runtime bug that only surfaces after deployment.

### C++20 Enhancement: Custom Messages

C++20 allows adding a custom message to `[[nodiscard]]`, so the compiler displays a more specific explanation when issuing the warning:

```cpp
[[nodiscard("Must check: hardware initialization may fail")]]
ErrorCode init_board();
```

If a caller writes `read_sensor()` without checking the return value, the compiler will display your custom message instead of a generic "ignoring return value" warning.

### Applying to Types

`[[nodiscard]]` can also be placed on a class or enumeration definition. This automatically gives all functions returning that type nodiscard semantics:

```cpp
[[nodiscard]] enum class ErrorCode {
    Ok,
    InvalidParam,
    Timeout,
    HardwareError
};

// 任何返回 ErrorCode 的函数都会自动触发检查
ErrorCode read_sensor(uint8_t id);
read_sensor(5);  // 警告：忽略了返回值
```

### ⚠️ nodiscard Is Not Mandatory

It is important to note that `[[nodiscard]]` produces a warning, not an error. Callers can still bypass it with an explicit cast:

```cpp
(void)init_board();             // 显式转换，消除警告
static_cast<void>(init_board()); // 同上
```

This means team coding standards may need to prohibit this pattern. `[[nodiscard]]` means "please check," not "you must check"—but it is still vastly better than having nothing at all.

------

## [[maybe_unused]]: Suppressing "Unused" Warnings

This attribute tells the compiler: this variable or parameter might not be used, so please do not issue a warning.

### Conditional Compilation Scenarios

The most common use case is conditional compilation. A parameter might be used under one configuration but not another:

```cpp
void sensor_task([[maybe_unused]] void* param) {
#ifdef USE_RTOS
    // RTOS 模式下使用 param
    auto* config = static_cast<TaskConfig*>(param);
    configure_sensor(config->port);
#else
    // 裸机模式下不用 param
    configure_sensor(kDefaultPort);
#endif
}
```

Without `[[maybe_unused]]`, the compiler will warn that `timeout_ms` is unused during a bare-metal build. The old workaround was to write `(void)timeout_ms;` inside the function body, or to comment out the parameter name `/*timeout_ms*/`. `[[maybe_unused]]` is more semantic than `(void)`, and less error-prone than commenting out parameter names.

### Unused Members in Structured Bindings

When you only need some members of a structured binding, the other members can be marked `[[maybe_unused]]`. However, a more common approach is to use an underscore `_` as a "don't care" placeholder:

```cpp
std::map<int, std::string> cache;

for (const auto& [key, value] : cache) {
    // 如果你只关心 value，不关心 key
}

// 或者用 _ 占位（C++20 引入）
// 但注意 _ 在全局命名空间可能有特殊含义
```

### Comparison with Traditional Methods

Previous approaches to suppressing unused warnings each had drawbacks: `(void)x;` is a runtime no-op statement mixed into your code that looks like something was left out; commenting out the parameter name `int foo(int /*x*/)` makes it easy to forget to update the comment when changing the parameter type; compiler-specific attributes like `__attribute__((unused))` are not portable. `[[maybe_unused]]` is a standardized, semantically clear solution.

------

## [[deprecated]]: Marking Obsolete APIs

`[[deprecated]]` lets you mark obsolete functions, classes, or variables via compiler warnings. It has been supported since C++14 and can include a custom message explaining what to use instead.

### Basic Usage

```cpp
[[deprecated("Use new_handler() instead")]]
void old_handler();

// 调用 old_handler() 会产生编译警告，附带你写的消息
old_handler();
// warning: 'old_handler' is deprecated: Use new_handler() instead
```

### Use in Library Version Migration

During library version upgrades, `[[deprecated]]` is an extremely useful tool. You can mark old APIs as deprecated instead of deleting them outright, giving users time to migrate:

```cpp
class SensorManager {
public:
    // 旧 API——仍然可用，但标记为过时
    [[deprecated("Use read_sensor_data() which returns more information")]]
    bool read_sensor(uint8_t id, uint16_t* value);

    // 新 API
    SensorData read_sensor_data(uint8_t id);
};

// 枚举值也可以标记为 deprecated
enum class SensorType {
    Temperature,
    Humidity,
    [[deprecated("Use Pressure instead")]]
    Barometer,   // 旧名称
    Pressure      // 新名称
};
```

This approach of "mark as deprecated first, then remove in the next major version" is much friendlier than deleting an API outright. Callers see the warning at compile time and know they need to migrate.

### Scope of deprecated

`[[deprecated]]` can be placed on almost any entity—functions, classes, enumerations, enumeration values, variables, template specializations, and even namespaces (since C++17). This means you can deprecate an entire class, not just individual functions:

```cpp
[[deprecated("Use NewSensorManager instead")]]
class OldSensorManager { /* ... */ };
```

------

## [[fallthrough]]: Intentional switch Fallthrough

In a `switch` statement, if a `case` does not end with `break`, execution "falls through" to the next `case`. Compilers warn about this because it might mean you forgot to write `break`. But sometimes fallthrough is intentional—`[[fallthrough]]` tells the compiler "I did this on purpose, don't warn."

### Basic Usage

```cpp
void handle_event(uint8_t event) {
    switch (event) {
        case 0x01:
            toggle_led(LED1);
            [[fallthrough]];  // 明确表示有意贯穿

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

`[[fallthrough]]` must be placed after the last statement of a `case` and before the next `case` label, and it must be followed by a semicolon. If you place it elsewhere, the compiler may ignore it or report an error.

### Typical Scenario in State Machines

When implementing a state machine where multiple states share certain processing logic, fallthrough is a natural choice:

```cpp
enum class State { Idle, Initializing, Running, Paused, Error };

void handle_state(State current, Event ev) {
    switch (current) {
        case State::Idle:
            if (ev == Event::Start) {
                current = State::Initializing;
            }
            [[fallthrough]];  // Idle 和 Initializing 共享初始化逻辑

        case State::Initializing:
            init_hardware();
            current = init_ok() ? State::Running : State::Error;
            break;

        case State::Running:
            run_task();
            break;

        case State::Paused:
        case State::Error:
            // 两个状态共享处理逻辑，直接贯穿
            recover();
            break;
    }
}
```

Note the last example: there is no `[[fallthrough]]` between `STATE_C` and `STATE_D`—because there are no statements between them, the compiler will not warn about an empty `case`.

------

## [[noreturn]]: Functions That Never Return

`[[noreturn]]` marks functions that never return to the caller. Such functions either call `std::exit()`, `std::abort()`, enter an infinite loop, or throw an exception.

```cpp
[[noreturn]] void fatal_error(const char* msg) {
    std::fprintf(stderr, "FATAL: %s\n", msg);
    std::abort();
}

[[noreturn]] void hang_forever() {
    while (true) {
        // 嵌入式中的安全停机模式
    }
}

void check_critical(bool ok) {
    if (!ok) {
        fatal_error("Critical check failed");
        // 编译器知道这里不会返回，后续代码不可达
    }
    // 编译器可以优化此分支，不需要考虑 fatal_error 返回的情况
    proceed();
}
```

The value of `[[noreturn]]` to the compiler lies in optimization: the compiler knows that no control flow will come back after `fatal_error()`, so it does not need to generate code for the return path. Furthermore, the compiler can use this to suppress "function might not return a value" warnings.

> **Optimization effect**: Assembly tests show that at the `-O2` optimization level, the compiler does indeed optimize away unreachable code after a `[[noreturn]]` function call. However, modern compilers have strong static analysis capabilities, and even without the `[[noreturn]]` hint, they can infer in some simple scenarios that a function will not return.

⚠️ Note: if you add `[[noreturn]]` to a function that actually does return, the behavior is undefined behavior (UB). The compiler might not report an error, but the generated code may behave completely unexpectedly.

------

## [[carries_dependency]]

This attribute was introduced in C++11 for propagating memory order dependency chains related to `std::memory_order_consume`. It is extremely rarely used in practice—because mainstream compilers (GCC, Clang) promote `memory_order_consume` directly to `memory_order_acquire`, making this attribute practically useless. Unless you are writing lock-free data structures and need precise control over dependency chain propagation, you can safely ignore it.

> **Verification**: Assembly tests confirm that GCC indeed generates identical assembly code for `memory_order_consume` and `memory_order_acquire` (both use `ldar` loads, with no additional dependency chain handling), which explains why `[[carries_dependency]]` has virtually no effect in practice.

------

## Compiler Extension Attributes

Beyond standard attributes, mainstream compilers support compiler-specific attributes via namespace prefixes. Although these are not standard, they can be very useful on specific platforms:

```cpp
// GCC/Clang 扩展
[[gnu::always_inline]]           // 强制内联
[[gnu::hot]]                     // 标记为热点函数
[[gnu::cold]]                    // 标记为冷路径
[[gnu::format(printf, 1, 2)]]    // printf 格式检查
[[clang::fallthrough]]           // Clang 专用的 fallthrough

// MSVC 扩展
[[msvc::forceinline]]            // 强制内联
```

These attributes should be used cautiously in cross-platform code. If you must use them, we recommend wrapping them uniformly with macro definitions:

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

## Correct Placement of Attributes

Placing attributes in different positions has different meanings. Putting an attribute in the wrong position might cause the compiler to ignore it, or it might apply to the wrong target:

```cpp
// 函数属性——放在返回类型之前或声明符之后
[[nodiscard]] int func();      // 正确
int func [[nodiscard]]();      // 也正确（但不太常见）

// 变量属性——放在变量名之前
[[maybe_unused]] int x;

// 类属性——放在 class 关键字之后
class [[deprecated]] OldClass {};

// 枚举属性——放在 enum 关键字之后
enum class [[deprecated]] OldEnum {};

// switch case 属性——放在 case 内最后一条语句之后
switch (x) {
    case 1:
        do_something();
        [[fallthrough]];     // 注意分号
    case 2:
        do_more();
        break;
}
```

If you are unsure where an attribute should be placed, cppreference is the most reliable reference.

------

## Summary

The standard attributes from C++11 through C++17 provide practical static checking tools for daily development. `[[nodiscard]]` enforces return value checks, `[[maybe_unused]]` suppresses unused warnings, `[[deprecated]]` marks obsolete APIs, `[[fallthrough]]` marks intentional fallthrough, and `[[noreturn]]` marks non-returning functions. Each attribute solves a specific engineering problem—not as a flashy trick, but as a way to make the compiler help with code review.

In team development, we recommend establishing unified standards for using these attributes: which functions must have `[[nodiscard]]` (such as all functions returning error codes), which scenarios are suitable for `[[deprecated]]` (such as during API version migration), and when to use compiler extension attributes. Unified standards are more effective than scattered individual habits.

In the next chapter, we will look at attributes added in C++20 and C++23—`[[likely]]/[[unlikely]]`, `[[no_unique_address]]`, `[[optimize]]`, and more—which lean more toward performance optimization, representing the "make the compiler generate better code" direction.

## References

- [cppreference: C++ attributes](https://en.cppreference.com/w/cpp/language/attributes)
- [cppreference: nodiscard](https://en.cppreference.com/w/cpp/language/attributes/nodiscard)
- [cppreference: maybe_unused](https://en.cppreference.com/w/cpp/language/attributes/maybe_unused)
- [cppreference: deprecated](https://en.cppreference.com/w/cpp/language/attributes/deprecated)
