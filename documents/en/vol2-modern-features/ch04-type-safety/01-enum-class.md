---
title: enum class and Strongly Typed Enums
description: Say goodbye to implicit integer conversions, and build type-safe enumerations
  with enum class
chapter: 4
order: 1
tags:
- host
- cpp-modern
- intermediate
- enum_class
- 类型安全
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 14
- 17
- 20
reading_time_minutes: 15
prerequisites:
- 'Chapter 0: 移动构造与移动赋值'
related:
- 强类型 typedef
- std::variant
translation:
  source: documents/vol2-modern-features/ch04-type-safety/01-enum-class.md
  source_hash: 295cdd57ee8f7d69a580809a6d8137ab5c4d8e351079436441f9652b16d65885
  translated_at: '2026-05-26T11:27:16.933494+00:00'
  engine: anthropic
  token_count: 3100
---
# enum class and Strongly-Typed Enumerations

## Introduction

Before writing this article, I flipped through some of my old C-style code — the screen was full of ``enum Color { Red, Green, Blue };``, and things like ``if (color == 1)`` were everywhere.

If it's a legacy project, there's nothing we can do about it. But still writing like this in 2026 is basically digging your own grave. The implicit integer conversion, namespace pollution, and inability to forward-declare of C-style enums — these three strikes are each enough to get you chewed out in a code review.

``enum class`` (the strongly-typed enumeration introduced in C++11) exists to solve these problems. It's not just syntactic sugar — it's a commitment at the level of type safety. In this chapter, we start from the pain points of C-style enums and work our way to understanding exactly what bugs ``enum class`` fixes, and how to use it to write safer code.

## Step 1 — The Three Sins of C-Style Enums

Before diving into ``enum class``, let's look at the blood-pressure-raising problems of the old ``enum``.

### Sin 1: Implicit Conversion to Integers

The values of a legacy ``enum`` can be implicitly converted to ``int``. This might sound "convenient," but it actually encourages you to write code like this:

```cpp
enum Color { Red, Green, Blue };
enum Fruit { Apple, Orange, Banana };

void paint(int c);

paint(Red);       // OK，隐式转成 int
paint(Orange);    // 也 OK！但语义完全错了
paint(42);        // 编译通过，运行时才知道出问题

if (Red == Apple) {
    // 居然编译通过，而且为 true！因为都是 0
}
```

Values from different enumeration types can be compared to each other and passed to any function that accepts ``int`` — the compiler doesn't care at all whether these values are semantically matched. This type of bug is extremely hard to track down in large codebases because the compiler won't give you any warnings.

### Sin 2: Namespace Pollution

All enumerator values of a legacy ``enum`` are exposed directly to the enclosing scope. If you have two enumerations that both define common names like ``None`` or ``Error``, they will clash:

```cpp
enum Status { None, Ok, Error };
enum Permission { None, Read, Write, Execute };  // 编译错误！None 重定义

// 常见的变通方案：加前缀
enum Status { Status_None, Status_Ok, Status_Error };
enum Permission { Perm_None, Perm_Read, Perm_Write, Perm_Execute };
```

Adding prefixes does solve the problem, but this is using manual conventions to compensate for missing language mechanisms — every team might have a different prefix style, driving up the maintenance cost.

### Sin 3: Inability to Forward-Declare

The underlying type of a C-style ``enum`` is determined by the compiler, so the compiler cannot determine its size before seeing the ``enum`` definition. This means ``enum`` cannot be forward-declared (unless you manually specify the underlying type, but then it's no longer "pure C-style"), which is very inconvenient for header file dependency management.

```cpp
// status.h
enum Status { Ok, Error };  // 必须看到完整定义

// device.h
// enum Status;  // 编译错误！无法前向声明
class Device {
public:
    Status get_status() const;  // 必须包含 status.h
};
```

These three issues combined are basically a textbook example of "type safety anti-patterns." C++11's ``enum class`` provides a clear solution for each and every one of them.

## Step 2 — The Three Major Improvements of enum class

### Scoped Isolation

The enumerator values of an ``enum class`` do not leak into the enclosing scope. They must be accessed using the ``EnumName::Value`` syntax:

```cpp
enum class Color { Red, Green, Blue };
enum class Fruit { Apple, Orange, Banana };

Color c = Color::Red;   // 正确
// Color c = Red;        // 编译错误！Red 不在外部作用域
// Fruit f = Color::Red; // 编译错误！类型不匹配
```

Now ``Color::Red`` and ``Fruit::Apple`` each mind their own business — they can never clash or be mixed up. The compiler can intercept all cross-type misuse at compile time.

### No Implicit Conversion

An ``enum class`` will not implicitly convert to any integer type; you must use ``static_cast`` for explicit conversion:

```cpp
enum class Color : uint8_t { Red, Green, Blue };

// int x = Color::Red;                          // 编译错误！
int x = static_cast<int>(Color::Red);           // OK，显式转换

void paint(Color c);
paint(Color::Red);      // OK
// paint(0);             // 编译错误！
// paint(static_cast<Color>(0));  // OK 但不推荐——绕过类型检查
```

You might think, "Writing ``static_cast`` every time is so annoying." My take is: **the inconvenience is the price of safety**. If a particular place needs to use an enumeration value as an integer, you must write it out explicitly — this means you are making a conscious decision at that point, rather than being silently let through by the compiler.

### Specifying the Underlying Type and Forward Declaration

An ``enum class`` can specify its underlying type, which defaults to ``int``. Once the underlying type is specified, the compiler knows the size of the enumeration at the point of declaration, making forward declarations feasible:

```cpp
// status.h —— 前向声明
enum class Status : uint8_t;

// device.h —— 只需要前向声明
class Device {
public:
    Status get_status() const;
    void set_status(Status s);
};

// status.cpp —— 完整定义
enum class Status : uint8_t { kOk = 0, kError = 1, kBusy = 2 };
```

You only need a forward declaration in the header file, while the full definition goes in the ``.cpp`` file, breaking circular dependencies between headers. Furthermore, in embedded systems, you can specify the underlying type as ``uint8_t`` to ensure the enumeration variable only takes up one byte:

```cpp
enum class SensorState : uint8_t {
    kOff = 0,
    kInit = 1,
    kReady = 2,
    kError = 3
};

static_assert(sizeof(SensorState) == 1, "SensorState should be 1 byte");
```

## Step 3 — Bitwise Operations and enum class

In C-style code, using enumeration values as bitmasks is a very common operation:

```cpp
// C 风格：天然支持位运算（因为隐式转换成 int）
enum Permission { Read = 1, Write = 2, Execute = 4 };
int perms = Read | Write;  // OK
```

But ``enum class`` prohibits implicit conversion, so writing something like ``Color::Red | Color::Green`` directly results in a compilation error. To support bitwise operations, we need to manually overload the operators:

```cpp
#include <type_traits>

enum class Permission : uint32_t {
    kNone    = 0,
    kRead    = 1 << 0,
    kWrite   = 1 << 1,
    kExecute = 1 << 2
};

// 辅助函数：枚举值到底层类型的转换
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

// 辅助判断：是否有任何标志位被设置
constexpr bool has_any_flag(Permission flags) noexcept
{
    return to_underlying(flags) != 0;
}

// 辅助判断：是否包含特定标志位
constexpr bool has_flag(Permission flags, Permission flag) noexcept
{
    return to_underlying(flags & flag) != 0;
}
```

Using it feels very natural:

```cpp
Permission user_perms = Permission::kRead | Permission::kWrite;

if (has_flag(user_perms, Permission::kWrite)) {
    // 用户有写权限
}

user_perms |= Permission::kExecute;  // 添加执行权限
user_perms &= ~Permission::kWrite;   // 移除写权限
```

Although this code looks a bit long (after all, you have to hand-write six operators), it guarantees type safety: you cannot mix values from ``Permission`` and ``Color`` in bitwise operations. In real projects, these operators are usually extracted into a common header file and reused via templates or macros.

Speaking of which, it's worth mentioning the progress in C++23. ``std::to_underlying`` has been officially incorporated into the C++23 standard library, and the ``to_underlying`` helper function above can be directly replaced with ``std::to_underlying`` from ``<utility>``. As for ``std::flags``, a type wrapper specifically designed for bitmasks, it is currently still in the proposal stage (P1872) and has not yet entered the standard. Until then, manually overloading operators remains the most mainstream approach.

## Step 4 — switch Matching and Compiler Warnings

``enum class`` and ``switch`` statements are a match made in heaven. Because the values of an ``enum class`` must be accessed via a qualified name, the compiler knows all possible values and can warn you when a branch is missing:

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
    // 如果缺少 kError 分支，-Wswitch 会发出警告
    }
    return "unknown";
}
```

I strongly recommend: **when using an ``enum class`` in a ``switch``, do not write a ``default`` branch**. The reason is that if you write a ``default``, the compiler assumes you have handled all "other" cases, and the ``-Wswitch`` warning becomes ineffective. If you don't write a ``default``, when new enumeration values are added later, the compiler will warn at every ``switch`` that misses them, helping you nip bugs in the bud at compile time.

The corresponding compiler flags are GCC/Clang's ``-Wswitch`` (enabled by default) or ``-Wswitch-enum`` (stricter, warns even if a ``default`` is present). Adding these flags in your project's CMakeLists.txt is a good engineering practice.

## Step 5 — C++20 using enum

While the scoped isolation of ``enum class`` is a good thing, sometimes in a function that frequently uses a certain enumeration, repeatedly writing ``EnumName::`` is indeed a bit verbose. C++20 introduced the ``using enum`` declaration, which brings all values of a given enumeration into the current scope at once:

```cpp
enum class TokenType {
    kNumber, kString, kIdentifier,
    kPlus, kMinus, kStar, kSlash,
    kLeftParen, kRightParen, kEof
};

std::string_view token_to_string(TokenType type)
{
    // 把所有枚举值引入函数作用域
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

The scope of ``using enum`` is limited to the current block (inside the curly braces), so it won't pollute the outer scope. It can also be used inside a class definition:

```cpp
class Lexer {
public:
    using enum TokenType;  // 所有枚举值成为类的成员

    TokenType next_token();
    bool is_operator(TokenType t);
};
```

⚠️ There's a pitfall here: ``using enum`` brings all enumeration values into the current scope. If two enumerations have identically named values, using ``using enum`` for both at the same time will cause a conflict. So when using it, make sure you know all the values of that enumeration and that they won't clash with names in the current scope.

## Practical Applications — State Machines and Error Codes

### State Machines

State machines are one of the most common patterns in embedded systems and protocol parsing. Using an ``enum class`` to represent states, combined with a ``switch`` to implement state transitions, is both clear and safe:

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

The benefit of this code is that if you later add a new state to ``DeviceState`` (such as ``kPaused``), the compiler will warn at every ``switch`` missing this branch (provided you didn't write a ``default``), ensuring you don't miss any state transition logic.

### Error Codes

Using an ``enum class`` for error codes is much safer than using ``#define`` or a bare ``int``:

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
    // ... 实际的文件打开逻辑
    return {ErrorCode::kOk, "success"};
}
```

The benefit of doing this is that the caller cannot casually pass in a ``42`` as an error code — it must use a value of type ``ErrorCode``. Although this compile-time check is simple, it can save you a tremendous amount of debugging time in large projects.

## C and C++ Interface Interoperability

In real projects, ``enum class`` sometimes encounters scenarios where it needs to interact with C interfaces. The underlying C library might require passing a ``int`` or ``uint32_t``, while your C++ code uses an ``enum class``. In this case, explicit conversion is needed:

```cpp
extern "C" void hal_set_mode(uint8_t mode);

enum class HalMode : uint8_t {
    kSleep = 0,
    kNormal = 1,
    kBoost = 2
};

void set_device_mode(HalMode mode)
{
    // enum class -> 底层类型 -> C 接口
    hal_set_mode(static_cast<uint8_t>(mode));
}
```

If you need to do this conversion frequently, the ``to_underlying`` helper function (or C++23's ``std::to_underlying``) can save you from writing a few extra lines of ``static_cast``. However, in my experience, this kind of conversion is usually concentrated at the interface layer (adapter layer) and doesn't scatter throughout the business logic, so the amount of code isn't that large.

## Run Online

Run the enum class example online to compare the problems of C-style enums with the strongly-typed improvements:

<OnlineCompilerDemo
  title="enum class: Strongly-Typed Enumerations and Type Safety"
  source-path="code/examples/vol2/10_enum_class.cpp"
  description="Run online and observe the implicit conversion problems of C-style enums and the type safety improvements of enum class."
  allow-run
/>

## Summary

``enum class`` has been around since C++11, and today it is an indispensable foundational tool in modern C++. Through three core improvements — scoped isolation, prohibition of implicit conversion, and specifiable underlying types — it thoroughly fixes the type safety issues of C-style ``enum``.

Bitwise operations require hand-written operator overloads, but this is precisely the embodiment of type safety: the compiler won't mix values from two different enumerations in bitwise operations behind your back. The combination of ``switch`` and ``enum class`` lets the compiler check for exhaustiveness on your behalf, and paired with the ``-Wswitch`` flag, no branch will be missed when new enumeration values are added. C++20's ``using enum`` then provides a convenient shorthand for scenarios that frequently use enumerations, all while maintaining type safety.

The "strongly-typed typedef" we will explore in the next article solves the same class of problems as ``enum class`` — except it is aimed not at "a finite set of enumeration values," but at "values with the same underlying type but different semantics."

## References

- [cppreference: Enumeration declaration](https://en.cppreference.com/w/cpp/language/enum)
- [cppreference: std::to_underlying (C++23)](https://en.cppreference.com/w/cpp/utility/to_underlying)
- [C++20 using enum (P1099R5)](https://en.cppreference.com/w/cpp/language/enum#Using-enum-declaration)
- [C++ Core Guidelines: Enum.2](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#enum2-use-enumerations-to-represent-sets-of-related-named-constants)
