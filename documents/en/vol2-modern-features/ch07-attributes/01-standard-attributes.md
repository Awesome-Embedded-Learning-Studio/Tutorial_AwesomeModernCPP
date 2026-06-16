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
reading_time_minutes: 12
related:
- C++20-23 新属性
tags:
- host
- cpp-modern
- intermediate
title: 'Detailed Guide to Standard Attributes: Let the Compiler Be Your Code Reviewer'
translation:
  source: documents/vol2-modern-features/ch07-attributes/01-standard-attributes.md
  source_hash: 68fbfa6d7df82476cb5ac2aa7a2b648c04ae9cfeb8de26dfa13c93f8520e1953
  translated_at: '2026-06-16T03:58:18.964304+00:00'
  engine: anthropic
  token_count: 2428
---
# Standard Attributes Explained: Making the Compiler Your Code Reviewer

When writing code, I often encounter a few frustrating situations: calling a function that returns an error code but forgetting to check it, and the compiler lets it pass without a peep; having a parameter that isn't used in a specific build configuration, causing the compiler to flood the screen with unused variable warnings; or wanting to mark an API as obsolete, relying only on documentation or comments to remind callers. The standard attribute syntax `[[...]]`, introduced in C++11 and gradually expanded in subsequent versions, exists to solve these problems—providing a standardized way to pass extra information to the compiler so it can help us perform static checks.

> In a nutshell: **Attributes are declarative hints to the compiler. They do not change program semantics, but they help the compiler find errors or generate better code.**

------

## Basic Syntax of Attributes

C++ standard attributes use double square brackets `[[...]]`. Multiple attributes can be written together `[[attr1, attr2]]` or separately `[[attr1]] [[attr2]]`; the effect is the same. Attributes can be placed in many positions—function declarations, variable declarations, class declarations, enum declarations, `switch` `case` statements, etc.—depending on the attribute type.

> **Verification**: Compilation tests show that `[[attr1, attr2]]` and `[[attr1]] [[attr2]]` generate identical warnings, and the order of attributes does not affect the result.

Before standard attributes, different compilers had their own syntaxes: GCC/Clang used `__attribute__((...))`, and MSVC used `__declspec(...)`. The advantage of standard attributes is portability—all compliant compilers must support them. However, the standard also reserves a namespace prefix mechanism, such as `[[vendor::attr]]`, allowing compiler extensions to be expressed using a unified syntax.

> **Attributes by Version**: C++11 introduced `[[noreturn]]` and `[[carries_dependency]]`; C++14 introduced `[[deprecated]]`; C++17 introduced `[[fallthrough]]`, `[[maybe_unused]]`, and `[[nodiscard]]`. Different attributes were standardized in different versions, so pay attention to target compiler support when using them.

```cpp
[[nodiscard]] int check_system_status();
[[deprecated("Use new_api() instead")]] void old_api();
```

------

## [[nodiscard]]: Warn if Return Value Is Ignored

This is arguably the most practically useful attribute in systems programming. It tells the compiler: if the caller ignores this function's return value, please issue a warning.

### Basic Usage

```cpp
[[nodiscard]] int read_sensor() {
    // Returns 0 on success, negative error code on failure
    if (sensor_ready()) {
        return sensor_read();
    }
    return -1;
}

void example() {
    read_sensor(); // Warning: ignoring return value of 'read_sensor'
}
```

In systems development, hardware initialization, sensor reads, and communication operations can all fail. Ignoring the return value means you might continue running in an already erroneous state, with unpredictable consequences. `[[nodiscard]]` turns "should check but forgot" into a compiler warning, rather than a runtime bug exposed only after deployment.

### C++20 Enhancement: Custom Messages

C++20 allows adding a custom message to `[[nodiscard]]`, so the compiler displays more specific instructions when issuing the warning:

```cpp
[[nodiscard("Hardware initialization failed, check connections")]]
bool init_hardware() {
    return false; // Simulate failure
}

void boot() {
    init_hardware(); // Warning: ignoring return value of 'init_hardware': Hardware initialization failed, check connections
}
```

If the caller writes `init_hardware()` without checking the return value, the compiler displays your message instead of a generic "ignoring return value" warning.

### Applying to Types

`[[nodiscard]]` can also be placed on a class or enumeration definition. This way, all functions returning that type automatically carry the `nodiscard` semantics:

```cpp
[[nodiscard]] struct Error {
    int code;
};

Error process_data(); // Implicitly [[nodiscard]]
```

### ⚠️ nodiscard Is Not Mandatory

It is important to note that `[[nodiscard]]` produces a warning, not an error. Callers can still bypass it via explicit casting:

```cpp
void example() {
    (void)init_hardware(); // No warning, explicitly discarded
}
```

This means team standards may need to prohibit this pattern. `[[nodiscard]]` is "please check" rather than "must check"—but it is already much better than nothing.

------

## [[maybe_unused]]: Suppress "Unused" Warnings

This attribute tells the compiler: this variable or parameter might not be used, so please do not issue a warning.

### Conditional Compilation Scenarios

The most common use is conditional compilation. A parameter might be used in one configuration but not in another:

```cpp
#ifdef USE_FEATURE_A
    #define FEATURE_ATTR
#else
    #define FEATURE_ATTR [[maybe_unused]]
#endif

void process(int data, FEATURE_ATTR bool flag) {
    // 'flag' is only used when USE_FEATURE_A is defined
#ifdef USE_FEATURE_A
    if (flag) {
        // ...
    }
#endif
}
```

Without `[[maybe_unused]]`, the compiler warns that `flag` is unused when compiling for bare metal. Previous approaches involved writing `(void)flag;` in the function body or commenting out the parameter name `/* flag */`. `[[maybe_unused]]` is more semantic than `(void)flag;` and less error-prone than commenting out parameter names.

### Unused Members in Structured Bindings

When you only need some members of a structured binding, other members can be marked `[[maybe_unused]]`. However, a more common practice is to use an underscore `_` as a placeholder for "I don't care about this":

```cpp
auto [value, _] = get_pair(); // 'value' is used, second is ignored
```

### Comparison with Traditional Methods

Previous methods for suppressing unused warnings had downsides: `(void)var;` is a runtime no-op statement mixed in code that looks like something was missed; commenting out parameter names `/* name */` is easily forgotten when updating parameter types; compiler-specific attributes like `__attribute__((unused))` are not portable. `[[maybe_unused]]` is the standardized, semantically clear solution.

------

## [[deprecated]]: Mark Obsolete APIs

`[[deprecated]]` allows you to mark obsolete functions, classes, or variables via compiler warnings. Supported since C++14, it can include a custom message explaining what to use instead.

### Basic Usage

```cpp
[[deprecated("Replaced by new_api(), which is thread-safe")]]
void old_api();

void user_code() {
    old_api(); // Warning: 'old_api' is deprecated: Replaced by new_api(), which is thread-safe
}
```

### Application in Library Version Migration

During library version upgrades, `[[deprecated]]` is a very useful tool. You can mark old APIs as deprecated instead of deleting them immediately, giving users time to migrate:

```cpp
// v1.0
void connect();

// v2.0
[[deprecated("Use connect_secure() instead")]]
void connect();
void connect_secure();
```

This approach—mark as deprecated first, delete in the next major version—is much friendlier than removing APIs directly. Callers see the warning at compile time and know they need to migrate.

### Scope of deprecated

`[[deprecated]]` can be placed on almost any entity: functions, classes, enums, enum values, variables, template specializations, namespaces (since C++17), etc. This means you can deprecate an entire class, not just individual functions:

```cpp
class [[deprecated("Use StringView instead")]] OldString { /* ... */ };
```

------

## [[fallthrough]]: Intentional Switch Fallthrough

In a `switch` statement, if a `case` does not end with a `break`, execution "falls through" to the next case. Compilers warn about this because it might be a forgotten `break`. But sometimes fallthrough is intentional behavior—`[[fallthrough]]` is used to tell the compiler "I did this on purpose, don't warn me."

### Basic Usage

```cpp
void process_event(int event) {
    switch (event) {
        case 1:
            // Handle event 1
            [[fallthrough]]; // Explicitly indicate fallthrough
        case 2:
            // Handle event 1 and 2
            break;
        default:
            break;
    }
}
```

`[[fallthrough]]` must be placed after the last statement of a `case` and before the next `case` label, and it must be followed by a semicolon. If placed elsewhere, the compiler may ignore it or report an error.

### Typical Scenario in State Machines

In state machine implementations, when multiple states share processing logic, fallthrough is a natural choice:

```cpp
switch (current_state) {
    case STATE_IDLE:
        init_sequence();
        [[fallthrough]];
    case STATE_READY:
        send_ready_signal();
        break;
    case STATE_ERROR:
    case STATE_FATAL: // No warning for empty case
        reset_system();
        break;
}
```

Note the last example: there is no `[[fallthrough]]` between `STATE_ERROR` and `STATE_FATAL`—because there are no statements between them, the compiler does not warn about empty cases.

------

## [[noreturn]]: Function Does Not Return

`[[noreturn]]` marks functions that never return to the caller. Such functions either call `std::exit()`, `std::abort()`, enter an infinite loop, or throw an exception.

```cpp
[[noreturn]] void fatal_error(const char* msg) {
    printf("Fatal: %s\n", msg);
    std::abort();
}

void process() {
    if (error_condition) {
        fatal_error("System failure"); // Compiler knows execution won't continue
    }
    // Code here is valid, compiler assumes it's reachable only if error_condition is false
}
```

The value of `[[noreturn]]` to the compiler lies in optimization: the compiler knows that control flow will not return after the call, so it does not need to generate code for the return path. Furthermore, the compiler can suppress "function might not return a value" warnings based on this.

> **Optimization Effect**: Assembly tests show that at `-O2` optimization level, the compiler does optimize away unreachable code after a `[[noreturn]]` function call. However, modern compilers have strong static analysis capabilities; even without the `[[noreturn]]` hint, they can infer that a function won't return in some simple scenarios.

⚠️ **Note**: If you add `[[noreturn]]` to a function that actually returns, the behavior is undefined. The compiler may not report an error, but the generated code may be completely unexpected.

------

## [[carries_dependency]]

This attribute was introduced in C++11 for memory order dependency chain propagation related to `std::memory_order::consume`. It is rarely used in practical development—because mainstream compilers (GCC, Clang) promote `std::memory_order::consume` directly to `std::memory_order::acquire`, making this attribute almost useless. Unless you are writing lock-free data structures and need precise control over dependency chain propagation, you can safely ignore it.

> **Verification**: Assembly tests confirm that GCC generates identical assembly code for `std::memory_order::consume` and `std::memory_order::acquire` (both using `ldar` on ARM64, with no extra dependency chain handling), explaining why `[[carries_dependency]]` has virtually no effect in practice.

------

## Compiler Extension Attributes

Beyond standard attributes, mainstream compilers support compiler-specific attributes via namespace prefixes. While not standard, these are useful on specific platforms:

```cpp
// GCC/Clang: Pack struct members
struct [[gnu::packed]] Packet {
    uint8_t header;
    uint32_t data; // No padding inserted
};

// MSVC: Align to 32-byte boundary for SIMD
struct [[msvc::align(32)]] Vec4 {
    float data[4];
};
```

Use these attributes cautiously in cross-platform code. If necessary, it is recommended to wrap them via macro definitions:

```cpp
#if defined(__GNUC__)
    #define PACKED __attribute__((packed))
#elif defined(_MSC_VER)
    #define PACKED __pragma(pack(push, 1))
#else
    #define PACKED
#endif

struct PACKED Header { /* ... */ };
```

------

## Correct Placement of Attributes

Placing attributes in different positions has different meanings. Placing them incorrectly might cause the compiler to ignore them or apply them to the wrong target:

```cpp
// Attribute applies to the function
[[nodiscard]] int* get_ptr();

// Attribute applies to the type pointed to, not the function
int* [[nodiscard]] get_ptr_wrong(); // Likely not what you intended
```

If you are unsure where an attribute should go, cppreference is the most reliable reference.

------

## Summary

Standard attributes from C++11 to C++17 provide practical static checking tools for daily development. `[[nodiscard]]` enforces return value checking, `[[maybe_unused]]` eliminates unused warnings, `[[deprecated]]` marks obsolete APIs, `[[fallthrough]]` marks intentional fallthrough, and `[[noreturn]]` marks non-returning functions. Each attribute solves a specific engineering problem—not for showing off, but for letting the compiler help you review code.

In team development, it is recommended to establish unified standards for using these attributes: which functions must have `[[nodiscard]]` (e.g., all functions returning error codes), which scenarios suit `[[deprecated]]` (e.g., during API version migration), and when to use compiler extension attributes. Unified standards are more effective than scattered individual habits.

The next chapter will look at attributes added in C++20 and C++23—`[[likely]]`/`[[unlikely]]`, `[[no_unique_address]]`, `[[assume]]`, etc.—which lean more towards performance optimization, representing the "make the compiler generate better code" direction.

## Reference Resources

- [cppreference: C++ attributes](https://en.cppreference.com/w/cpp/language/attributes)
- [cppreference: nodiscard](https://en.cppreference.com/w/cpp/language/attributes/nodiscard)
- [cppreference: maybe_unused](https://en.cppreference.com/w/cpp/language/attributes/maybe_unused)
- [cppreference: deprecated](https://en.cppreference.com/w/cpp/language/attributes/deprecated)
