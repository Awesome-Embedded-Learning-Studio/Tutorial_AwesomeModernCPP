---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: Say goodbye to implicit integer conversions and build type-safe enumerations with enum class
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Chapter 0: Move Construction and Move Assignment'
reading_time_minutes: 13
related:
- 'Strong Typedefs: Type Safety That Prevents Mix-Ups'
- 'std::variant: A Type-Safe Union'
tags:
- host
- cpp-modern
- intermediate
- enum_class
- 类型安全
title: enum class and Scoped Enums
translation:
  source: documents/vol2-modern-features/ch04-type-safety/01-enum-class.md
  source_hash: 1c4af3b2f3230482c449d35c7d8268526c50e69429d35fabb1c5356a6ef40de5
  translated_at: '2026-09-25T15:29:45+00:00'
  engine: anthropic
  token_count: 4400
---
# enum class and Scoped Enums

Before writing this article, we flipped through some of our old C-style code—screen after screen of `enum Color { Red, Green, Blue };`, with things like `if (color == 1)` everywhere.

If it's a legacy project, fine, there's no helping it—but still writing this way in 2026 is basically digging a pit for your future self. Implicit integer conversion, namespace pollution, and no way to forward-declare: that's the C-style enum's three-axe combo, and any single swing is enough to get you chewed out in code review.

`enum class` (the strongly typed enumeration introduced in C++11) exists to solve exactly these problems. It isn't just syntactic sugar—it's a promise made at the level of type safety. In this chapter we start from the pain points of C-style enums and work out, step by step, exactly which bugs `enum class` fixes and how to use it to write safer code.

## Step 1—The Three Sins of C-Style Enums

Before we get to `enum class`, let's first look at what kinds of blood-pressure-raising problems the old `enum` actually has.

### Sin 1: Implicit Conversion to Integer

Values of an old-style `enum` implicitly convert to `int`. That sounds like "convenience", but what it actually does is encourage code like this:

```cpp
enum Color { Red, Green, Blue };
enum Fruit { Apple, Orange, Banana };

void paint(int c);

paint(Red);       // OK, implicitly converts to int
paint(Orange);    // Also OK! But semantically completely wrong
paint(42);        // Compiles; you only find out at runtime

if (Red == Apple) {
    // It even compiles—and it's true! Because both are 0
}
```

Values from different enum types compare against each other just fine and can be passed to any function accepting `int`—the compiler doesn't care in the slightest whether the values match semantically. Once the codebase gets big, this class of bug is brutally hard to track down, because the compiler never gives you a single warning.

### Sin 2: Namespace Pollution

Every enumerator of an old-style `enum` is dumped directly into the enclosing scope. If two enums both define a popular name like `None` or `Error`, you get a collision:

```cpp
enum Status { None, Ok, Error };
enum Permission { None, Read, Write, Execute };  // Compile error! None redefined

// A common workaround: add prefixes
enum Status { Status_None, Status_Ok, Status_Error };
enum Permission { Perm_None, Perm_Read, Perm_Write, Perm_Execute };
```

Prefixes do fix the problem, but that's replacing a language mechanism with a manual convention—every team may end up with its own prefix style, and the maintenance cost goes through the roof.

### Sin 3: No Forward Declarations

The underlying type of a C-style `enum` is up to the compiler, so before the compiler has seen the enum's definition it cannot know its size. As a result, the `enum` cannot be forward-declared (unless you spell out the underlying type yourself—but then it's no longer "pure C style"), which makes header dependency management distinctly inconvenient.

```cpp
// status.h
enum Status { Ok, Error };  // The full definition must be visible

// device.h
// enum Status;  // Compile error! Cannot forward-declare
class Device {
public:
    Status get_status() const;  // Must include status.h
};
```

Stack those three together and you have pretty much the anti-textbook example of "type safety". C++11's `enum class` delivers a concrete fix for each and every one of them.

## Step 2—The Three Major Improvements of enum class

### Scope Isolation

The enumerators of an `enum class` do not leak into the surrounding scope. They must be accessed as `EnumName::Value`:

```cpp
enum class Color { Red, Green, Blue };
enum class Fruit { Apple, Orange, Banana };

Color c = Color::Red;   // Correct
// Color c = Red;        // Compile error! Red is not in the enclosing scope
// Fruit f = Color::Red; // Compile error! Type mismatch
```

Now `Color::Red` and `Fruit::Apple` each mind their own business—name collisions and cross-type mix-ups are permanently off the table. The compiler intercepts every cross-type misuse for you at compile time.

### No Implicit Conversions

An `enum class` does not implicitly convert to any integer type; you must convert explicitly with `static_cast`:

```cpp
enum class Color : uint8_t { Red, Green, Blue };

// int x = Color::Red;                          // Compile error!
int x = static_cast<int>(Color::Red);           // OK, explicit conversion

void paint(Color c);
paint(Color::Red);      // OK
// paint(0);             // Compile error!
// paint(static_cast<Color>(0));  // OK but not recommended—bypasses type checking
```

You may feel that writing `static_cast` every single time is a chore. Our view: **the chore is precisely the price of safety**. If some location genuinely needs to treat an enumerator as an integer, you must write that out explicitly—which means you are making a conscious decision at that spot, rather than being waved through by the compiler without ever noticing.

Let's put the earlier counterexample and the corrected version side by side in one diagram: on the left, the old `enum` silently waving things through; on the right, `enum class` intercepting at compile time:

![C-style enum letting implicit conversions through vs. enum class intercepting them at compile time](./01-enum-class-compare.drawio)

### Specifying the Underlying Type and Forward Declarations

An `enum class` can specify its underlying type, which defaults to `int`. Once the underlying type is pinned down, the compiler knows the enum's size at the point of declaration, so forward declaration becomes viable:

```cpp
// status.h — forward declaration
enum class Status : uint8_t;

// device.h — only the forward declaration is needed
class Device {
public:
    Status get_status() const;
    void set_status(Status s);
};

// status.cpp — full definition
enum class Status : uint8_t { kOk = 0, kError = 1, kBusy = 2 };
```

Headers need only the forward declaration; the full definition lives in a `.cpp` file, which breaks circular dependencies between headers. And in embedded work you can pin the underlying type to `uint8_t`, guaranteeing that an enum variable occupies exactly one byte:

```cpp
enum class SensorState : uint8_t {
    kOff = 0,
    kInit = 1,
    kReady = 2,
    kError = 3
};

static_assert(sizeof(SensorState) == 1, "SensorState should be 1 byte");
```

## Step 3—Bitwise Operations and enum class

In C-style code, using enumerators as bit flags (a bitmask) is a very common practice:

```cpp
// C style: bitwise operations work natively (implicit conversion to int)
enum Permission { Read = 1, Write = 2, Execute = 4 };
int perms = Read | Write;  // OK
```

But `enum class` forbids implicit conversion, so writing `Color::Red | Color::Green` is a straight compile error. To support bitwise operations, we need to overload the operators by hand:

```cpp
#include <type_traits>

enum class Permission : uint32_t {
    kNone    = 0,
    kRead    = 1 << 0,
    kWrite   = 1 << 1,
    kExecute = 1 << 2
};

// Helper: convert an enumerator to its underlying type
template <typename E>
constexpr auto to_underlying(E e) noexcept
{
    return static_cast<std::underlying_type_t<E>>(e);
}

constexpr Permission operator|(Permission a, Permission b) noexcept
{
    return static_cast<Permission>(to_underlying(a) | to_underlying(b));
}

constexpr Permission operator&(Permission a, Permission b) noexcept
{
    return static_cast<Permission>(to_underlying(a) & to_underlying(b));
}

constexpr Permission operator^(Permission a, Permission b) noexcept
{
    return static_cast<Permission>(to_underlying(a) ^ to_underlying(b));
}

constexpr Permission operator~(Permission a) noexcept
{
    return static_cast<Permission>(~to_underlying(a));
}

constexpr Permission& operator|=(Permission& a, Permission b) noexcept
{
    a = a | b;
    return a;
}

constexpr Permission& operator&=(Permission& a, Permission b) noexcept
{
    a = a & b;
    return a;
}

// Helper check: whether any flag bits are set
constexpr bool has_any_flag(Permission flags) noexcept
{
    return to_underlying(flags) != 0;
}

// Helper check: whether a specific flag bit is present
constexpr bool has_flag(Permission flags, Permission flag) noexcept
{
    return to_underlying(flags & flag) != 0;
}
```

Using it all feels perfectly natural:

```cpp
Permission user_perms = Permission::kRead | Permission::kWrite;

if (has_flag(user_perms, Permission::kWrite)) {
    // The user has write permission
}

user_perms |= Permission::kExecute;  // Add execute permission
user_perms &= ~Permission::kWrite;   // Remove write permission
```

Granted, this code looks a bit long (six hand-written operators, after all), but it guarantees type safety: you can never mix `Permission` and `Color` values in a bitwise operation. In real projects, these operators usually get factored out into a shared header and reused through templates or macros.

Speaking of which, the C++23 progress here is worth a mention. `std::to_underlying` has officially been adopted into the standard library in C++23, so the `to_underlying` helper above can be swapped directly for `std::to_underlying` from `<utility>`. As for a dedicated bitmask-oriented type wrapper like `std::flags`, it is still at the proposal stage (P1872) and has not entered the standard. Until then, hand-written operator overloads remain the mainstream practice.

## Step 4—switch Matching and Compiler Warnings

`enum class` and the `switch` statement are a natural pair. Because `enum class` values must be accessed via their qualified names, the compiler knows all the possible values and can warn you when a branch goes missing:

```cpp
enum class NetworkState : uint8_t {
    kDisconnected,
    kConnecting,
    kConnected,
    kError
};

std::string_view to_string(NetworkState state)
{
    switch (state) {
    case NetworkState::kDisconnected: return "disconnected";
    case NetworkState::kConnecting:   return "connecting";
    case NetworkState::kConnected:    return "connected";
    // If the kError branch is missing, -Wswitch emits a warning
    }
    return "unknown";
}
```

Our strong recommendation: **when switching over an `enum class`, do not write a `default` branch**. The reason: once you write `default`, the compiler assumes you have handled every "other" case, and the `-Wswitch` warning is neutralized. Leave out the `default`, and when a new enumerator is added later, the compiler will flag every `switch` that misses it, strangling those bugs at compile time.

The corresponding compiler options are GCC/Clang's `-Wswitch` (on by default) or `-Wswitch-enum` (stricter—it warns even when a `default` exists). Adding these options to your project's CMakeLists.txt is a sound engineering practice.

## Step 5—C++20 using enum

The scope isolation of `enum class` is a good thing, but in a function that leans heavily on one enum, repeatedly typing `EnumName::` does get a bit wordy. C++20 introduced the `using enum` declaration, which imports all of an enum's values into the current scope in one go:

```cpp
enum class TokenType {
    kNumber, kString, kIdentifier,
    kPlus, kMinus, kStar, kSlash,
    kLeftParen, kRightParen, kEof
};

std::string_view token_to_string(TokenType type)
{
    // Bring all enumerators into the function scope
    using enum TokenType;

    switch (type) {
    case kNumber:     return "number";
    case kString:     return "string";
    case kIdentifier: return "identifier";
    case kPlus:       return "+";
    case kMinus:      return "-";
    case kStar:       return "*";
    case kSlash:      return "/";
    case kLeftParen:  return "(";
    case kRightParen: return ")";
    case kEof:        return "eof";
    }
    return "unknown";
}
```

The scope of `using enum` is limited to the current block (inside the braces), so it does not pollute the outer scope. It works inside class definitions too:

```cpp
class Lexer {
public:
    using enum TokenType;  // All enumerators become members of the class

    TokenType next_token();
    bool is_operator(TokenType t);
};
```

Here is the pitfall: `using enum` pulls every enumerator into the current scope. If two enums have same-named values, bringing both in via `using enum` at the same time produces a conflict. So before using it, make sure you know all of the enum's values and that they will not collide with names already present in the current scope.

## Practical Applications—State Machines and Error Codes

### State Machines

The state machine is one of the most common patterns in embedded systems and protocol parsing. Representing states with an `enum class` and driving the transitions with a `switch` is both clear and safe:

```cpp
#include <cstdio>

enum class DeviceState : uint8_t {
    kIdle,
    kInitializing,
    kRunning,
    kSuspending,
    kError
};

class DeviceController {
public:
    void on_event(const char* event)
    {
        switch (state_) {
        case DeviceState::kIdle:
            if (is_start(event)) {
                state_ = DeviceState::kInitializing;
                std::printf("State: Idle -> Initializing\n");
                do_init();
            }
            break;
        case DeviceState::kInitializing:
            if (is_init_done(event)) {
                state_ = DeviceState::kRunning;
                std::printf("State: Initializing -> Running\n");
            } else if (is_error(event)) {
                state_ = DeviceState::kError;
                std::printf("State: Initializing -> Error\n");
            }
            break;
        case DeviceState::kRunning:
            if (is_stop(event)) {
                state_ = DeviceState::kSuspending;
                std::printf("State: Running -> Suspending\n");
            } else if (is_error(event)) {
                state_ = DeviceState::kError;
                std::printf("State: Running -> Error\n");
            }
            break;
        case DeviceState::kSuspending:
            if (is_suspend_done(event)) {
                state_ = DeviceState::kIdle;
                std::printf("State: Suspending -> Idle\n");
            }
            break;
        case DeviceState::kError:
            if (is_reset(event)) {
                state_ = DeviceState::kIdle;
                std::printf("State: Error -> Idle\n");
            }
            break;
        }
    }

    DeviceState get_state() const noexcept { return state_; }

private:
    DeviceState state_ = DeviceState::kIdle;

    void do_init() { /* ... */ }

    static bool is_start(const char* e)      { return e[0] == 'S'; }
    static bool is_init_done(const char* e)  { return e[0] == 'D'; }
    static bool is_stop(const char* e)       { return e[0] == 'T'; }
    static bool is_suspend_done(const char* e) { return e[0] == 's'; }
    static bool is_error(const char* e)      { return e[0] == 'E'; }
    static bool is_reset(const char* e)      { return e[0] == 'R'; }
};
```

The payoff of this code: if you later add a new state to `DeviceState` (say, `kPaused`), the compiler warns at every `switch` that lacks the branch for it (provided you didn't write a `default`), so no state-transition logic gets missed.

### Error Codes

Using `enum class` for error codes is far safer than `#define` or a raw `int`:

```cpp
#include <string_view>

enum class ErrorCode : int {
    kOk = 0,
    kInvalidArgument = 1,
    kNotFound = 2,
    kPermissionDenied = 3,
    kTimeout = 4,
    kInternalError = 5
};

struct Result {
    ErrorCode code;
    std::string_view message;

    bool is_ok() const noexcept { return code == ErrorCode::kOk; }
};

Result open_file(const char* path)
{
    if (!path || path[0] == '\0') {
        return {ErrorCode::kInvalidArgument, "path is empty"};
    }
    // ... the actual file-opening logic
    return {ErrorCode::kOk, "success"};
}
```

The benefit here: callers cannot just pass in a `42` as an error code—they must use a value of type `ErrorCode`. This compile-time check is simple, but in a large project it saves you a great deal of debugging time.

## C and C++ Interface Interoperability

In real projects, `enum class` sometimes runs into scenarios where it must talk to a C interface. The underlying C library may demand an `int` or `uint32_t`, while your C++ code uses an `enum class`. That's when an explicit conversion is needed:

```cpp
extern "C" void hal_set_mode(uint8_t mode);

enum class HalMode : uint8_t {
    kSleep = 0,
    kNormal = 1,
    kBoost = 2
};

void set_device_mode(HalMode mode)
{
    // enum class -> underlying type -> C interface
    hal_set_mode(static_cast<uint8_t>(mode));
}
```

If you make this conversion frequently, the `to_underlying` helper (or C++23's `std::to_underlying`) can save you a few lines of `static_cast`. In our experience, though, these conversions usually concentrate in the interface layer (the adapter layer) rather than scattering through the business logic, so the amount of code stays small.

## Run It Online

Run the enum class example online and compare the problems of C-style enums against the strongly typed improvements:

<OnlineCompilerDemo
  title="enum class: Strongly Typed Enums and Type Safety"
  source-path="code/examples/vol2/10_enum_class.cpp"
  description="Run it online and observe the implicit-conversion problems of C-style enums and the type-safety improvements of enum class."
  allow-run
/>

## References

- [cppreference: Enumeration declaration](https://en.cppreference.com/w/cpp/language/enum)
- [cppreference: std::to_underlying (C++23)](https://en.cppreference.com/w/cpp/utility/to_underlying)
- [C++20 using enum (P1099R5)](https://en.cppreference.com/w/cpp/language/enum#Using-enum-declaration)
- [C++ Core Guidelines: Enum.2](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#enum2-use-enumerations-to-represent-sets-of-related-named-constants)
