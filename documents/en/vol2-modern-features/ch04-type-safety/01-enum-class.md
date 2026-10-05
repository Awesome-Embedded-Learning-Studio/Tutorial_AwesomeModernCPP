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
  source_hash: be78df547fee8f906bff3a9dd9f1aa6b187707a97a9a426cbd7754673a5b8057
  translated_at: '2026-09-27T12:17:19+00:00'
  engine: anthropic
  token_count: 4100
---
# enum class and Scoped Enums

Before sitting down to write this chapter, I flipped back through some of my own old C-style code. Screens full of `enum Color { Red, Green, Blue };` paired with `if (color == 1)` scattered everywhere — it made me wince.

Last chapter we spent the whole time with lambdas; this chapter we come back to something smaller that nonetheless shows up everywhere: enumerations. Code like the snippet above, sitting in a legacy project nobody is allowed to touch — fine, we let that slide. But still writing that way in 2026 is honestly indefensible. The old `enum`'s failings, taken together, are three: implicit integer conversion, name pollution, and no forward declarations. Each one is enough to earn you a talking-to in code review.

`enum class`, the strongly typed enumeration introduced in C++11, exists precisely to take those on. Don't dismiss it as mere syntactic sugar: what it gives you is a guarantee at the type-safety level — every misuse the old `enum` waved through becomes a compile error once you switch.

## The Three Old Problems of C-Style enum

Let's lay the old `enum` out and examine exactly where it goes wrong.

### Problem One: Implicit Conversion to Integer

Values of an old-style `enum` convert implicitly to `int`, without you writing a single explicit conversion. At first glance that's convenient. But after enough of that convenience, you end up with code like this on your hands:

```cpp
enum Color { Red, Green, Blue };
enum Fruit { Apple, Orange, Banana };

void paint(int c);

paint(Red);       // OK, implicitly converts to int
paint(Orange);    // Also OK! But the semantics are completely wrong
paint(42);        // Compiles; you only find out at runtime

if (Red == Apple) {
    // This actually compiles, and it's true! Both are 0
}
```

That `paint(Orange)` above compiles just fine, and `Red == Apple` even evaluates to true. As far as the compiler is concerned these values are all integers, so whether it's a comparison or a function argument, it lets them through without discrimination. Bugs like this are extremely hard to track down in a large codebase, and for a call like `paint(Orange)` the compiler never gives us a single warning, start to finish.

> If you actually run this yourself, you'll find that the cross-enum comparison (`Red == Apple`) does get a `-Wenum-compare` warning, which is on by default — but it's only a warning, and compilation still goes through. Passing `Orange` to `paint(int)` is the case that truly gives you no hint at all.

### Problem Two: Enumerators Pollute the Enclosing Scope

The old-style `enum`'s second failing is that it dumps every enumerator straight into the enclosing scope. Define two enums that happen to both use a common name like `None` or `Error`, and the collision arrives immediately:

```cpp
enum Status { None, Ok, Error };
enum Permission { None, Read, Write, Execute };  // Compile error! None redefined

// The common workaround: add prefixes
enum Status { Status_None, Status_Ok, Status_Error };
enum Permission { Perm_None, Perm_Read, Perm_Write, Perm_Execute };
```

You've probably seen the prefix trick; it does work, but it relies on manual convention rather than a language mechanism. Every team's prefix style is different, too, so as the number of teams grows, the maintenance cost climbs right along with it.

### Problem Three: No Forward Declarations

The third failing has its root in declarations. Forward declarations (a declaration that gives just the name, deferring the full definition) are a staple of header management, yet the C-style `enum` can't use them. The reason is that its underlying type (which integer type the enumerators are actually stored as) is decided by the compiler; before the compiler has seen the full definition, it can't settle on the size, so a forward declaration is impossible. Unless you specify the underlying type by hand — at which point it's no longer "pure C style" anyway. Header dependency management ends up awkward as a result.

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

Put the three problems side by side and you're basically looking at a counterexample textbook for type safety. And `enum class` has a targeted fix for every one of them.

## The Three Improvements of enum class

### Scope Isolation

The first improvement is scope isolation: `enum class` enumerators no longer leak into the enclosing scope. Whichever value you want to use, you have to access it through the `EnumName::Value` form:

```cpp
enum class Color { Red, Green, Blue };
enum class Fruit { Apple, Orange, Banana };

Color c = Color::Red;   // Correct
// Color c = Red;        // Compile error! Red is not in the enclosing scope
// Fruit f = Color::Red; // Compile error! Type mismatch
```

From here on, `Color::Red` and `Fruit::Apple` each keep to their own lane; name collisions and mix-ups simply can't happen anymore. And every cross-type misuse, the compiler intercepts for you at compile time.

### No Implicit Conversions

The second improvement takes aim at implicit conversion: an `enum class` no longer converts implicitly to any integer type. To get an integer, you have to write an explicit conversion such as `static_cast`:

```cpp
enum class Color : uint8_t { Red, Green, Blue };

// int x = Color::Red;                          // Compile error!
int x = static_cast<int>(Color::Red);           // OK, explicit conversion

void paint(Color c);
paint(Color::Red);      // OK
// paint(0);             // Compile error!
// paint(static_cast<Color>(0));  // OK but not recommended — bypasses type checking
```

You might feel that writing `static_cast` every time is a hassle. Me, I'm happy to type a few extra characters in exchange for that bit of friction. Wherever a spot genuinely needs to treat an enumerator as an integer, you must write it out explicitly. That means you're making a conscious decision at that location, rather than being waved through by the compiler without noticing.

We've put the earlier counterexamples and the corrected forms side by side in one diagram — on the left, the old `enum` silently waving things through; on the right, `enum class` intercepting at compile time:

![C-style enum implicitly waving conversions through vs. enum class intercepting at compile time](./01-enum-class-compare.drawio)

### Specifying the Underlying Type and Forward Declarations

The third improvement is that the underlying type can now be specified: if you don't write one, the default is still `int`. Once the underlying type is pinned down, the compiler knows the size from the declaration alone, and forward declarations become viable along with it:

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

In the header we need only a single forward declaration; the full definition moves into the `.cpp` file, and just like that the circular dependency between headers is broken. In embedded work, you can also pin the underlying type to `uint8_t` so an enum variable reliably occupies exactly one byte:

```cpp
enum class SensorState : uint8_t {
    kOff = 0,
    kInit = 1,
    kReady = 2,
    kError = 3
};

static_assert(sizeof(SensorState) == 1, "SensorState should be 1 byte");
```

## Bitwise Operations and enum class

Bit flags (a bitmask: treating each binary digit as an independent switch) are extremely common in C-style code, and assembling permissions out of enumerator values is routine business for us:

```cpp
// C style: bitwise operations work natively (because of the implicit conversion to int)
enum Permission { Read = 1, Write = 2, Execute = 4 };
int perms = Read | Write;  // OK
```

`enum class` has banned the implicit conversion, so an expression like `Color::Red | Color::Green` simply doesn't compile. If we want `Permission` to support bitwise operations, we have to overload the operators ourselves:

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

// Helper check: whether any flag bit is set
constexpr bool has_any_flag(Permission flags) noexcept
{
    return to_underlying(flags) != 0;
}

// Helper check: whether a specific flag bit is included
constexpr bool has_flag(Permission flags, Permission flag) noexcept
{
    return to_underlying(flags & flag) != 0;
}
```

Here's what usage looks like:

```cpp
Permission user_perms = Permission::kRead | Permission::kWrite;

if (has_flag(user_perms, Permission::kWrite)) {
    // The user has write permission
}

user_perms |= Permission::kExecute;  // Add execute permission
user_perms &= ~Permission::kWrite;   // Remove write permission
```

Long, yes — all six operators have to be written by our own hand, after all. What we get in exchange is very real: you can no longer mix `Permission` and `Color` values in a bitwise operation. In real projects, people generally collect these operators into a shared header and reuse them via templates or macros.

While writing `to_underlying`, you may already have thought that the standard library would inevitably grow a helper like this. And it has: `std::to_underlying` officially entered the standard library in C++23, so the handwritten version above can be swapped directly for the one in `<utility>`. The standard library currently has no ready-made counterpart for the bitwise operators, so hand-written operator overloads remain the mainstream practice.

> As a side note, the dedicated type wrapper for bit flags, `std::flags`, is still sitting at the proposal stage and hasn't made it in.

## switch Matching and Compiler Warnings

`enum class` and `switch` work together quite smoothly. `enum class` values must all go by their qualified names (the prefixed, fully spelled-out form we've been writing all along, `EnumName::Value`), so the compiler knows the complete set of possible values, and when you leave out a branch, it can warn you:

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

My advice is blunt: **when switching over an `enum class`, don't write a `default` branch**. The reasoning goes like this: with a `default` in place, the compiler assumes you've handled all the "other" cases, and the `-Wswitch` warning is disabled along with it. Leave it out, though, and when a new enumerator is added later, the compiler will flag every `switch` that misses it — the bug gets stopped at compile time.

The corresponding compiler flag is `-Wswitch` on GCC/Clang. GCC only picks it up once you enable `-Wall`; on Clang it is on by default. Stricter still is `-Wswitch-enum` (it warns even with a `default` present). Adding these flags to your project's `CMakeLists.txt` is a solid engineering practice.

## The C++20 using enum

Scope isolation is a genuine good, but when one function uses the same enum over and over, you end up typing `EnumName::` again and again, and yes, it does get wordy to read. C++20 added the `using enum` declaration, which imports every value of an enum into the current scope in one go:

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

The effect of `using enum` extends only to the current block (inside the braces); once you leave the block it's gone, so the enclosing scope stays unpolluted. We can also use it inside a class definition:

```cpp
class Lexer {
public:
    using enum TokenType;  // All enumerators become members of the class

    TokenType next_token();
    bool is_operator(TokenType t);
};
```

There is one spot where this goes wrong easily, and you should go in with eyes open: `using enum` imports every enumerator into the current scope wholesale. If two enums share a same-named value and we `using enum` both of them at the same time, a collision follows. So before using it, you need to know all of the enum's values, and confirm that none of them clash with the names already in the current scope.

## Practical Applications: State Machines and Error Codes

### State Machines

You'll run into state machines over and over in embedded work and protocol parsing — one of the most common patterns there is. Represent the states with an `enum class` and write the transitions with a `switch`, and you get clarity and safety both:

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

The payoff shows up when you add a new state: the day someone adds a `kPaused` to `DeviceState`, the compiler warns at every `switch` that is missing that branch — provided, again, that you didn't write a `default`. Not a single piece of state-transition logic can slip through unnoticed.

### Error Codes

Using `enum class` for error codes is far safer than `#define` or a bare `int`:

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

The benefit is just as direct: callers can no longer casually stuff a `42` in as an error code; the only things usable are values of type `ErrorCode`. The check itself is simple, yet in a large project it genuinely saves you heaps of debugging time.

## C and C++ Interface Interop

Real projects have no shortage of scenarios where you talk to C interfaces: the C++ code you write uses `enum class`, while the C library underneath wants an `int` or a `uint32_t`. When the two sides don't line up, an explicit conversion is in order:

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

If you make these conversions often, `to_underlying` (or C++23's `std::to_underlying`) saves you a few lines of `static_cast`. In my experience, though, the conversions tend to concentrate in the interface layer (an adapter: the layer dedicated to talking to external interfaces); business logic rarely sees more than a couple of them, so the amount of code involved stays small.

## Run It Online

Watching without practicing doesn't count — run the example below yourself, and verify with your own eyes both the problems of the C-style `enum` and the improvements strong typing brings:

<OnlineCompilerDemo
  title="enum class: strongly typed enumerations and type safety"
  source-path="code/examples/vol2/10_enum_class.cpp"
  description="Run it online and observe the implicit-conversion problems of C-style enums and the type-safety improvements of enum class."
  allow-run
/>

## References

- [cppreference: Enumeration declaration](https://en.cppreference.com/w/cpp/language/enum)
- [cppreference: std::to_underlying (C++23)](https://en.cppreference.com/w/cpp/utility/to_underlying)
- [C++20 using enum (P1099R5)](https://en.cppreference.com/w/cpp/language/enum#Using-enum-declaration)
- [C++ Core Guidelines: Enum.2](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#enum2-use-enumerations-to-represent-sets-of-related-named-constants)
