---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: Say goodbye to implicit integer conversions, and build type-safe enumerations
  with `enum class`.
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Chapter 0: 移动构造与移动赋值'
reading_time_minutes: 13
related:
- 强类型 typedef
- std::variant
tags:
- host
- cpp-modern
- intermediate
- enum_class
- 类型安全
title: enum class and Scoped Enums
translation:
  source: documents/vol2-modern-features/ch04-type-safety/01-enum-class.md
  source_hash: 853a064143ba3eedf2f9d1773f161cabf00fb0011b4dd880a924e5141e1833b0
  translated_at: '2026-06-16T03:57:17.399366+00:00'
  engine: anthropic
  token_count: 3094
---
# enum class and Scoped Enumerations

## Introduction

Before writing this article, I looked back at some of my old C-style code—screens filled with ``enum Color { Red, Green, Blue };``, and ``if (color == 1)`` appearing everywhere.

If this is a legacy project, there's no choice, but writing like this in 2026 is basically digging a hole for yourself. The implicit integer conversion, namespace pollution, and inability to forward declare C-style enums—these three issues are enough to get you scolded in a code review.

`enum class` (strongly-typed enumeration introduced in C++11) exists to solve these problems. It is not just syntactic sugar—it is a commitment at the level of type safety. In this chapter, starting from the pain points of C-style enums, we will figure out exactly what bugs `enum class` fixes and how to use it to write safer code.

## Step 1 — The Three Cardinal Sins of C-style Enums

Before discussing `enum class`, let's look at the problems with old `enum` that really raise your blood pressure.

### Sin 1: Implicit Conversion to Integer

Values of old-style `enum` can be implicitly converted to `int`. This sounds "convenient," but it actually encourages you to write code like this:

````cpp
enum Color { Red, Green, Blue };
enum Fruit { Apple, Orange, Banana };

void paint(int c);

paint(Red);       // OK，隐式转成 int
paint(Orange);    // 也 OK！但语义完全错了
paint(42);        // 编译通过，运行时才知道出问题

if (Red == Apple) {
    // 居然编译通过，而且为 true！因为都是 0
}
````

Values of different enumeration types can be compared with each other and passed to any function accepting `int`—the compiler doesn't care if these values match semantically. These bugs are extremely hard to track down in large codebases because the compiler won't give you any warnings.

### Sin 2: Namespace Pollution

All enumerators of an old-style `enum` are exposed directly to the outer scope. If you have two enums that both define common names like `OK` or `Error`, a conflict occurs:

````cpp
enum Status { None, Ok, Error };
enum Permission { None, Read, Write, Execute };  // 编译错误！None 重定义

// 常见的变通方案：加前缀
enum Status { Status_None, Status_Ok, Status_Error };
enum Permission { Perm_None, Perm_Read, Perm_Write, Perm_Execute };
````

Adding prefixes can indeed solve the problem, but this replaces language mechanisms with manual conventions—every team might have a different prefix style, driving up maintenance costs significantly.

### Sin 3: Inability to Forward Declare

The underlying type of a C-style `enum` is decided by the compiler, so the compiler cannot determine its size before seeing the `enum` definition. This prevents `enum` from being forward declared (unless you manually specify the underlying type, but then it's not "pure C-style"), which is very inconvenient for header file dependency management.

````cpp
// status.h
enum Status { Ok, Error };  // 必须看到完整定义

// device.h
// enum Status;  // 编译错误！无法前向声明
class Device {
public:
    Status get_status() const;  // 必须包含 status.h
};
````

These three points together are basically a textbook example of "lack of type safety." C++11's `enum class` provides a clear solution for each one.

## Step 2 — The Three Major Improvements of enum class

### Scope Isolation

Enumerators of `enum class` do not leak into the outer scope. They must be accessed via `Enum::Value`:

````cpp
enum class Color { Red, Green, Blue };
enum class Fruit { Apple, Orange, Banana };

Color c = Color::Red;   // 正确
// Color c = Red;        // 编译错误！Red 不在外部作用域
// Fruit f = Color::Red; // 编译错误！类型不匹配
````

Now `Color::Red` and `TrafficLight::Red` each manage their own scope; they can never clash or be mixed up. The compiler can intercept all cross-type misuse at compile time.

### No Implicit Conversion

`enum class` does not implicitly convert to any integer type; you must use `static_cast` for explicit conversion:

````cpp
enum class Color : uint8_t { Red, Green, Blue };

// int x = Color::Red;                          // 编译错误！
int x = static_cast<int>(Color::Red);           // OK，显式转换

void paint(Color c);
paint(Color::Red);      // OK
// paint(0);             // 编译错误！
// paint(static_cast<Color>(0));  // OK 但不推荐——绕过类型检查
````

You might think "writing `static_cast` every time is so troublesome." My view is: **The trouble is the price of safety**. If a place needs to use an enumeration value as an integer, you must write it out explicitly—this means you are making a conscious decision at that location, rather than being unintentionally let through by the compiler.

### Specifying Underlying Type and Forward Declaration

`enum class` can specify the underlying type, defaulting to `int`. Once the underlying type is specified, the compiler knows the size of the enumeration at declaration time, making forward declarations feasible:

````cpp
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
````

In a header file, you only need a forward declaration; the full definition can be placed in the `.cpp` file, breaking circular dependencies between headers. Furthermore, in embedded systems, you can specify the underlying type as `uint8_t`, ensuring enumeration variables only occupy one byte:

````cpp
enum class SensorState : uint8_t {
    kOff = 0,
    kInit = 1,
    kReady = 2,
    kError = 3
};

static_assert(sizeof(SensorState) == 1, "SensorState should be 1 byte");
````

## Step 3 — Bitwise Operations and enum class

In C-style code, using enumeration values as bit flags (bitmasks) is a very common operation:

````cpp
// C 风格：天然支持位运算（因为隐式转换成 int）
enum Permission { Read = 1, Write = 2, Execute = 4 };
int perms = Read | Write;  // OK
````

However, `enum class` prohibits implicit conversion, so `flags | Flags::Read` results in a compilation error. To support bitwise operations, we need to manually overload operators:

````cpp
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
````

Using it feels very natural:

````cpp
Permission user_perms = Permission::kRead | Permission::kWrite;

if (has_flag(user_perms, Permission::kWrite)) {
    // 用户有写权限
}

user_perms |= Permission::kExecute;  // 添加执行权限
user_perms &= ~Permission::kWrite;   // 移除写权限
````

Although this code looks a bit long (after all, you have to hand-write six operators), it guarantees type safety: you cannot mix values from `Flags` and `Permissions` for bitwise operations. In actual projects, these operators are usually extracted into a common header file, reused using templates or macros.

Speaking of which, it's worth mentioning the progress in C++23. `std::to_underlying` has been officially included in the standard library in C++23, so the `to_underlying` helper function above can be replaced directly with `std::to_underlying`. As for `std::bitmask` (a type wrapper specifically designed for bitmasks), it is still in the proposal stage (P1872) and has not yet entered the standard. Until then, manually overloading operators remains the most mainstream approach.

## Step 4 — Switch Matching and Compiler Warnings

`enum class` and `switch` statements are a match made in heaven. Because `enum class` values must be accessed via qualified names, the compiler knows all possible values and can warn you when a branch is missing:

````cpp
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
````

I strongly suggest: **When using `enum class` with `switch`, do not write a `default` branch**. The reason is: if you write `default`, the compiler assumes you have handled all "other" cases, and the `-Wswitch` warning becomes ineffective. If you don't write `default`, when you add new enumeration values later, the compiler will issue warnings at all `switch` statements that missed them, helping you nip bugs in the bud at compile time.

The corresponding compiler options are GCC/Clang's `-Wswitch` (enabled by default) or `-Wswitch-enum` (stricter, warns even if there is a `default`). Adding these options to your project's CMakeLists.txt is good engineering practice.

## Step 5 — C++20 using enum

While the scope isolation of `enum class` is a good thing, sometimes in a function that frequently uses a certain enumeration, repeatedly writing `Enum::Value` is indeed a bit verbose. C++20 introduced the `using enum` declaration, which introduces all values of an enumeration into the current scope at once:

````cpp
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
````

The scope of `using enum` is limited to the current block (inside curly braces), so it won't pollute the outer scope. It can also be used in class definitions:

````cpp
class Lexer {
public:
    using enum TokenType;  // 所有枚举值成为类的成员

    TokenType next_token();
    bool is_operator(TokenType t);
};
````

⚠️ There is a pitfall here: `using enum` introduces all enumeration values into the current scope. If two enumerations have values with the same name, using `using enum` for both simultaneously will cause a conflict. So when using it, ensure you are clear about all values of that enumeration and that they won't conflict with names in the current scope.

## Practical Application — State Machines and Error Codes

### State Machine

State machines are one of the most common patterns in embedded systems and protocol parsing. Using `enum class` to represent states, combined with `switch` to implement state transitions, is both clear and safe:

````cpp
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
````

The benefit of this code is: if you later add a new state to `State` (e.g., `Suspending`), the compiler will warn at every `switch` missing this branch (provided you didn't write `default`), ensuring you don't miss any state transition logic.

### Error Codes

Using `enum class` for error codes is much safer than using `std::error_code` or naked `int`:

````cpp
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
````

The benefit here is: the caller cannot casually pass an `int` as an error code—it must use a value of type `ErrorCode`. Although this compile-time check is simple, it saves you a lot of debugging time in large projects.

## C and C++ Interface Interoperability

In actual projects, `enum class` sometimes encounters scenarios interacting with C interfaces. The underlying C library might require passing `int` or `uint32_t`, while your C++ code uses `enum class`. Explicit conversion is needed at this point:

````cpp
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
````

If you need to do this conversion frequently, the `to_underlying` helper function (or C++23's `std::to_underlying`) can save you a few lines of `static_cast`. However, in my experience, this conversion is usually concentrated at the interface layer (adapter layer) and not scattered in business logic, so the code volume isn't large.

## Run Online

Run the enum class example online to compare the issues of C-style enums with strongly-typed improvements:

<OnlineCompilerDemo
  title="enum class: Strongly-Typed Enumerations and Type Safety"
  source-path="code/examples/vol2/10_enum_class.cpp"
  description="Run online and observe the implicit conversion issues of C-style enums and the type safety improvements of enum class."
  allow-run
/>

## Summary

`enum class` has existed since C++11 and is today an indispensable basic tool in modern C++. Through three core improvements—scope isolation, prohibition of implicit conversion, and specifiable underlying types—it completely fixes the type safety issues of C-style `enum`.

Bitwise operations require manually overloading operators, but this is precisely the embodiment of type safety: the compiler won't mix values of two different enumerations for bitwise operations without your knowledge. The combination of `enum class` and `switch` allows the compiler to check exhaustiveness, and with the `-Wswitch` option, no branches are missed when adding new enumeration values. C++20's `using enum` provides a convenient shorthand for frequent enumeration usage while maintaining type safety.

The next topic we will discuss, "strong typedef," solves the same class of problems as `enum class`—except it faces not "finite enumeration values" but "values with the same underlying type but different semantics."

## References

- [cppreference: Enumeration declaration](https://en.cppreference.com/w/cpp/language/enum)
- [cppreference: std::to_underlying (C++23)](https://en.cppreference.com/w/cpp/utility/to_underlying)
- [C++20 using enum (P1099R5)](https://en.cppreference.com/w/cpp/language/enum#Using-enum-declaration)
- [C++ Core Guidelines: Enum.2](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#enum2-use-enumerations-to-represent-sets-of-related-named-constants)
