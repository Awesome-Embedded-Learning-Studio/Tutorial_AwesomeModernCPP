---
chapter: 7
cpp_standard:
- 11
- 17
description: Deep dive into the `std::error_code` dual-layer structure of error and
  category, why `errc` is a condition rather than a code, how `system_error` wraps
  error codes into exceptions, and building a custom category from scratch to seamlessly
  integrate custom error codes into the standard system.
difficulty: intermediate
order: 66
platform: host
prerequisites:
- expected：把错误提升为类型
- variant：类型安全的联合体
reading_time_minutes: 16
related:
- expected：把错误提升为类型
- filesystem 与路径操作
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'error_code: Error Code System and Custom category'
translation:
  source: documents/vol3-standard-library/error-utils/66-error-code.md
  source_hash: be053c14613a614bd2ce72b824d0fa26c9136c87affa14f34120ebd978d3cda8
  translated_at: '2026-06-24T00:41:07.596631+00:00'
  engine: anthropic
  token_count: 4877
---
# error_code: The Error Code System and Custom Categories

Eventually, every C++ developer faces a fundamental question: when a function fails, how do we communicate that failure to the caller? The language offers three paths: return codes, `std::error_code`, and exceptions. We won't cover the internal mechanics of exceptions (that's a topic for another time); instead, we focus on the middle path: how the `<system_error>` `error_code` / `error_category` system is designed, why it is designed this way, and how to seamlessly integrate your own module's error codes into this system.

Let's clarify the motivation first. Most are familiar with the C-era `errno`: you call a system interface, and if it fails, you read a global `errno` to get an integer, then use `strerror(errno)` to look up a string description. It works, but the pitfalls are obvious—`errno` is global mutable state (thread-local storage mitigates half the problem, but the semantics remain awkward), error numbers are raw `int`s (the compiler can't distinguish between `2` and `99` as an error code versus a normal return value), and error numbers from different libraries can collide. C++11's `<system_error>` exists to clean up this mess: it doesn't discard the low-cost "integer error number" model of `errno`, but instead wraps it in two layers of structure to **categorize** and **type** error numbers, allowing custom error codes and system error codes to share the same interface.

## Trade-offs Between the Three Error Handling Paths

When writing a function that can fail, you will likely choose among three options:

- **Raw return codes**: The function returns `int`, where `0` is success and non-zero is an error number. This is the most primitive and zero-overhead approach. The problem is that the caller receives an `int`, and the compiler cannot distinguish whether it is a result or an error code; if you forget to check it, you're flying blind. Furthermore, `int` lacks classification; a `2` from one module and a `2` from another might mean completely different things.
- **Exceptions**: `throw` an object, and control flow implicitly jumps to the nearest `catch`. The advantage is that the "success path" code remains very clean, and errors automatically propagate up the call stack. The cost is runtime overhead (even if not thrown, some ABIs have table overhead, and constructing the exception object/stack unwinding isn't cheap), and it hides the fact that "it might throw" inside the signature (C++ lacks a mandatory `throws` declaration).
- **`std::error_code`**: The function returns a lightweight object (essentially an `int` plus a pointer to a category), containing "error number + which system this number belongs to". It is explicit—the return type says `error_code`, so the caller knows immediately to check it. It is also zero-overhead—no exceptions, no stack unwinding, and the object is just 16 bytes, which is about the same as stuffing an `int` into an `expected<T, error_code>`.

This table lays out the trade-offs between the three:

| Dimension | Raw Return Code | `error_code` | Exception |
|---|---|---|---|
| Explicitness | Poor (`int` reveals nothing) | Good (type is documentation) | Poor (hidden in signature) |
| Runtime Overhead | 0 | Near 0 (16-byte object) | Yes (even if not thrown, higher if thrown) |
| Error Classification | None | Yes (category) | Yes (exception type) |
| Cross-module/Library | Each does its own thing | Standard unified system | Each does its own thing |
| Failure Ignorable | Easy to forget to check | Easy to forget to check (`bool` helps half-way) | Cannot ignore (crash if not caught) |

The sweet spot for `error_code` is scenarios where "failure is normal, expected, and on the hot path": files won't open, networks time out, configuration items are invalid. For these errors, you don't want the overhead of exceptions, but you do want a unified, comparable, queryable error system across modules—this is exactly what `<system_error>` was built to do.

## The Composition of `error_code`: value + category, separated

Let's first look at what `error_code` looks like. Inside, it contains just two things:

```cpp
// Standard: C++11
// std::error_code 的语义(简化)
class error_code {
    int value_;                  // 错误号(整数值)
    const error_category* cat_;  // 指向某个 category 单例的指针
};
```

One `int` holds the error number, and a pointer points to the `error_category` it belongs to. The key design lies in this **separation**: the error number is just a raw value, but a standalone "2" is meaningless—it could be the POSIX `ENOENT` (No such file or directory), or it could be "Authentication Failed" in your custom module. Only when paired with "which error numbering system it belongs to" does this "2" have a definite meaning. `error_category` is the carrier for the concept of an "error numbering system."

`error_category` is an abstract base class. Each concrete category is a singleton that provides three core capabilities:

- `name()` — The name of this error numbering system (e.g., `"system"`, `"generic"`, `"my-app"`).
- `message(ev)` — Translates the error number into human-readable text.
- `default_error_condition(ev)` / `equivalent(...)` — The bridge for cross-category comparisons (discussed specifically below).

The standard library comes with two built-in categories:

- `std::system_category()` — Corresponds to the current platform's system error numbers (the `errno` set, like `ENOENT`/`EACCES`/`ETIMEDOUT`...).
- `std::generic_category()` — Corresponds to POSIX generic error numbers, stable across platforms (the same set of `errc` enum values).

Let's run a minimal example to wrap an `errno`-style failure into an `error_code`:

```cpp
// Standard: C++11
#include <system_error>
#include <iostream>
#include <cstring>

int main()
{
    auto* fp = std::fopen("/no/such/file/here", "r");
    if (fp == nullptr) {
        int e = errno;                                   // 原始错误号
        std::error_code ec{e, std::system_category()};   // 包成 error_code

        std::cout << "value:    " << ec.value() << '\n';
        std::cout << "category: " << ec.category().name() << '\n';
        std::cout << "message:  " << ec.message() << '\n';
    }
    return 0;
}
```

`g++ -std=c++20 -O2` (native GCC 16.1.1) results:

```text
value:    2
category: system
message:  No such file or directory
```

That `2` is the value of `ENOENT`, and `system_category` knows that on the current platform, `2` corresponds to "No such file or directory". Note that we didn't write a single line of `strerror`; `message()` looked it up for us—this is the benefit of the category encapsulating the mapping from "number to description".

`error_code` also has an `operator bool`: it returns `true` if the error number is non-zero (indicating an error). A default-constructed `error_code` has an error number of `0` and a `bool` value of `false` (no error). Therefore, checking for errors is just one line: `if (ec)`:

```text
sizeof(error_code)      = 16
default error_code bool = 0
```

Sixteen bytes—that is just an `int` (padded to eight) plus a pointer. Something this lightweight, when passed between functions or wrapped in an `expected`, incurs almost no overhead.

## `errc`, `make_error_code`, and Cross-Category Comparison

Here is a counterintuitive point that almost every beginner stumbles over: Is the standard library's cross-platform error code enumeration, `std::errc`, actually an "error code" or an "error condition"?

The answer is the latter. An enumeration value like `errc::no_such_file_or_directory` **is not an `error_code`, but an `error_condition`**. We must clearly distinguish these two concepts:

- **`error_code`** — A **concrete, platform-specific** error number. For example, `2` under `system_category` is `ENOENT` on Linux, but the value might differ on other platforms.
- **`error_condition`** — An **abstract, portable** error condition. For example, `no_such_file_or_directory` under `generic_category` retains the same meaning regardless of the platform.

`errc` is an enumeration for `error_condition`. The standard library marks it as a condition enum using `is_error_condition_enum<std::errc>`. We can verify this directly using a type trait:

```cpp
// Standard: C++11
std::cout << "is_error_code_enum<errc>      = "
          << std::is_error_code_enum<std::errc>::value << '\n';
std::cout << "is_error_condition_enum<errc> = "
          << std::is_error_condition_enum<std::errc>::value << '\n';
```

```text
is_error_code_enum<errc>      = 0
is_error_condition_enum<errc> = 1
```

The consequence is straightforward: `std::error_code ec = std::errc::timed_out;` **fails to compile**. `errc` is not a `code` enumeration, so the path enabling the implicit construction of `error_code` is not available. To convert `errc` into `error_code`, we must explicitly call `std::make_error_code`:

```cpp
// Standard: C++11
auto ec = std::make_error_code(std::errc::timed_out);  // 造出来是 generic_category
std::cout << "value=" << ec.value()
          << " cat=" << ec.category().name() << '\n';
// value=110 cat=generic   (110 就是 POSIX 的 ETIMEDOUT)
```

So, where can we use `errc` given that it is a `condition`? The answer is **comparison**. `error_code` overloads `==`, so we can compare it directly with an `errc`:

```cpp
// Standard: C++11
int e = ENOENT;   // 2
std::error_code sys_ec{e, std::system_category()};
auto generic_ec = std::make_error_code(std::errc::no_such_file_or_directory);

std::cout << "sys_ec == generic_ec (code==code) ? "
          << (sys_ec == generic_ec) << '\n';
std::cout << "sys_ec == errc::no_such_file_or_directory (code==errc) ? "
          << (sys_ec == std::errc::no_such_file_or_directory) << '\n';
```

```text
sys_ec == generic_ec (code==code) ? 0
sys_ec == errc::no_such_file_or_directory (code==errc) ? 1
```

Notice the difference between these two results; this is the entire reason `default_error_condition` exists:

- `sys_ec == generic_ec` compares whether "two `error_code` objects are exactly equal" (equal value **and** equal category). Since one belongs to `system` and the other to `generic`, the categories differ, resulting in `0` (not equal).
- `sys_ec == errc::...` takes a different path: `errc` is implicitly constructed into an `error_condition` first. During comparison, `system_category`'s `default_error_condition(2)` **maps** this system error number to the corresponding `generic` condition—which happens to be `no_such_file_or_directory`—so they are equal.

In other words, `default_error_condition` is the category's way of declaring "this specific error number is equivalent to which generic error condition." It builds a bridge between "platform-specific error codes" and "portable error conditions": if you get `ENOENT` from `system_category` on Linux and compare it with someone else's `no_such_file_or_directory` from `generic_category`, they are equal—because they are semantically the same thing. This is why, in practice, error checking almost always uses the `ec == std::errc::xxx` style, rather than comparing against another `error_code`: the former works across categories and platforms, while the latter is strictly bound to the same category.

::: warning errc is not error_code
`errc` is an `error_condition` enum, not an `error_code` enum. `std::error_code ec = std::errc::x;` fails to compile; you must use `std::make_error_code(std::errc::x)`. However, `ec == std::errc::x` compiles and works correctly—the `==` path uses `default_error_condition` equivalence and does not require `errc` to be a code enum. This distinction is central to the design of `<system_error>`. If you mix them up, you'll either get a compilation failure or write a comparison that "looks correct but never matches."
:::

## `system_error`: Wrapping error_code in an Exception

`error_code` provides an explicit return path, but sometimes we still want the "automatic bubbling up" semantics of an exception. For example, deep within a call stack, if a low-level `error_code` fails, we might not want to manually propagate it up layer by layer, preferring to throw immediately. `<system_error>` provides a ready-made exception class, `std::system_error`, which wraps an `error_code` internally:

```cpp
// Standard: C++11
#include <system_error>
#include <iostream>
#include <cstring>

int main()
{
    try {
        errno = ENOENT;
        throw std::system_error(
            std::error_code{ENOENT, std::system_category()},
            "打开配置文件失败");
    } catch (const std::system_error& e) {
        std::cout << "what():     " << e.what() << '\n';
        std::cout << "code value: " << e.code().value() << '\n';
        std::cout << "code msg:   " << e.code().message() << '\n';
        std::cout << "category:   " << e.code().category().name() << '\n';
    }
    return 0;
}
```

```text
what():     打开配置文件失败: No such file or directory
code value: 2
code msg:   No such file or directory
category:   system
```

`what()` concatenates the context string we provide with `error_code::message()`, making the issue immediately clear during debugging. Once caught, we can extract the internal `error_code` using `e.code()` and continue with the standard logic of `value()/category()/== errc`. This serves as the bridge between `error_code` and exceptions: we can use the low-overhead `error_code` for returns at the low level, and at the boundary, decide that "this error is worth interrupting the control flow" by throwing `system_error{ec, "..."}` to promote it to an exception. Conversely, catching `system_error` allows us to retrieve the original `error_code`. The two paths are interoperable, not mutually exclusive.

The Standard Library uses this pattern most extensively in `<filesystem>` (C++17). Every function in `std::filesystem` that might fail has two overloads: one that throws a `filesystem_error` (which inherits from `system_error`), and another that accepts a `std::error_code&` output parameter and does not throw:

```cpp
// Standard: C++17
#include <filesystem>
#include <system_error>
#include <iostream>

namespace fs = std::filesystem;

int main()
{
    std::error_code ec;
    fs::file_size("/no/such/path", ec);   // 不抛重载:错误塞进 ec
    if (ec) {
        std::cout << "file_size 失败: " << ec.message()
                  << " (cat=" << ec.category().name() << ")\n";
    }
    return 0;
}
```

```text
file_size 失败: No such file or directory (cat=system)
```

The caller gets to choose: if we want exceptions to interrupt the flow, we call the version without `ec`; if we want to handle it ourselves and avoid exceptions breaking the control flow, we pass `ec`. This "dual API" pattern can be seen everywhere in `filesystem` and `asio`. Essentially, it treats `error_code` as an "optional alternative to exceptions," leaving the decision to the caller. The custom `category` we discuss below is designed to integrate our own modules into this system where "error codes can be everywhere."

## Custom category: Building an Error Code System from Scratch

This is the core of this article. The cleverest part of the standard library's `error_code` system design is that it isn't just for system errors—any module can register its own `error_category`, define its own set of error numbers, and then use the unified type `error_code` to carry them. We can operate on them using the same `message()`/`== errc` interfaces. Your module's errors are on equal footing with POSIX errors in terms of type.

Let's build one from scratch. Suppose we are writing a network login module with the following possible errors: network disconnection, authentication failure, timeout, and packet format error. Our goal is to make `login()` return `std::error_code`, allowing the caller to check with `ec == MyErrc::kAuthFailed`, retrieve a Chinese description with `ec.message()`, and even make `kTimeout` automatically equivalent to the standard `errc::timed_out`.

### Step 1: Define the Error Code Enum

```cpp
// Standard: C++11
#include <system_error>
#include <string>
#include <iostream>

enum class MyErrc {
    kSuccess     = 0,
    kNetworkDown = 10,
    kAuthFailed  = 11,
    kTimeout     = 12,
    kBadPayload  = 13,
};
```

Note that values start at zero, and zero is reserved for "success"—this aligns with the `error_code::operator bool` convention (where non-zero indicates an error).

### Step 2: Specialize `is_error_code_enum` to enable implicit conversion

Having just an enum isn't enough; the standard library doesn't know it's an "error code enum". We must specialize `std::is_error_code_enum` to mark it as `true`, so that `error_code` enables the implicit construction path from the enum:

```cpp
// Standard: C++11
namespace std {
template <>
struct is_error_code_enum<MyErrc> : true_type {};
}  // namespace std
```

This step acts as the "switch" connecting our custom enum to the `error_code` system. Previously, we verified that the switch for `std::errc` is `0` (because it is a condition enum), so `errc` cannot implicitly construct an `error_code`. Since we specialized our `MyErrc` to `true`, `MyErrc` can.

### Step 3: Write a Custom Category Singleton

A category is a class inheriting from `std::error_category` that implements those virtual functions. It must be a singleton—because `error_code` stores a pointer to the category internally, and comparing two `error_code` objects for the same category compares the pointers. Therefore, there must be only one instance of each category in the entire process:

```cpp
// Standard: C++11
class MyCategory : public std::error_category {
public:
    const char* name() const noexcept override {
        return "my-app";
    }

    std::string message(int ev) const override {
        switch (static_cast<MyErrc>(ev)) {
            case MyErrc::kSuccess:     return "成功";
            case MyErrc::kNetworkDown: return "网络不可达";
            case MyErrc::kAuthFailed:  return "鉴权失败";
            case MyErrc::kTimeout:     return "操作超时";
            case MyErrc::kBadPayload:  return "报文格式错误";
            default:                   return "未知错误";
        }
    }

    // 把自定义错误号映射到通用 error_condition,实现跨 category 等价
    std::error_condition default_error_condition(int ev) const noexcept override {
        switch (static_cast<MyErrc>(ev)) {
            case MyErrc::kTimeout:
                return std::make_error_condition(std::errc::timed_out);
            default:
                return {ev, *this};   // 其他用本 category 自身
        }
    }
};

// 单例工厂:全进程唯一实例
const std::error_category& my_category() {
    static MyCategory instance;
    return instance;
}
```

`message()` translates the error number into human-readable text—saving us from writing `ec.message()` ourselves. The `default_error_condition()` step is optional but critical: we declare that "my `kTimeout` is equivalent to the standard `errc::timed_out`". The consequence is that when the caller receives an `error_code` containing `MyErrc::kTimeout`, they can check it directly using `if (ec == std::errc::timed_out)`. This bridges the semantic gap across categories and between "my module" and the standard library.

### Step 4: Write the `make_error_code` overload

With the switch enabled and the category written, the final step is to provide a `make_error_code(MyErrc)` function to tell the standard library "how to construct an `error_code` when encountering `MyErrc`". This function is found via ADL (Argument-Dependent Lookup), so it should either be placed in the namespace where `MyErrc` resides or in the `std` namespace (the former is officially recommended):

```cpp
// Standard: C++11
std::error_code make_error_code(MyErrc e) {
    return {static_cast<int>(e), my_category()};
}
```

With these four steps in place, our custom error code system is complete. Now let's write a function that uses it:

```cpp
// Standard: C++11
std::error_code login(bool network_ok, bool password_ok) {
    if (!network_ok) return MyErrc::kNetworkDown;   // 隐式转 error_code
    if (!password_ok) return MyErrc::kAuthFailed;
    return MyErrc::kSuccess;
}
```

Note the line `return MyErrc::kNetworkDown;`. Since the `is_error_code_enum` specialization from step two evaluates to `true`, the `error_code`'s enabling constructor is activated. The compiler automatically calls `make_error_code` from step four to convert it into an `error_code`. **Implicit conversion works here**, which contrasts with `errc`'s inability to implicitly construct an `error_code`. This is the practical difference between a "code enum" and a "condition enum".

Let's run the complete example:

```cpp
// Standard: C++11
int main()
{
    auto ec1 = login(false, true);
    std::cout << "login(false,true):\n";
    std::cout << "  value    = " << ec1.value() << '\n';
    std::cout << "  category = " << ec1.category().name() << '\n';
    std::cout << "  message  = " << ec1.message() << '\n';
    std::cout << "  bool(ec) = " << static_cast<bool>(ec1) << " (非0=有错)\n";

    auto ec2 = login(true, false);
    std::cout << "\nlogin(true,false): " << ec2.message()
              << " (bool=" << static_cast<bool>(ec2) << ")\n";

    auto ec3 = login(true, true);
    std::cout << "login(true,true):  " << ec3.message()
              << " (bool=" << static_cast<bool>(ec3) << ")\n";

    std::cout << "\n--- 跨 category 等价性 ---\n";
    std::error_code tc{static_cast<int>(MyErrc::kTimeout), my_category()};
    std::cout << "kTimeout message: " << tc.message() << '\n';
    std::cout << "tc == errc::timed_out ? " << (tc == std::errc::timed_out)
              << "  (1=default_error_condition 映射后相等)\n";
    return 0;
}
```

Compiled with `g++ -std=c++20 -O2 -Wall -Wextra`, and the output is:

```text
login(false,true):
  value    = 10
  category = my-app
  message  = 网络不可达
  bool(ec) = 1 (非0=有错)

login(true,false): 鉴权失败 (bool=1)
login(true,true):  成功 (bool=0)

--- 跨 category 等价性 ---
kTimeout message: 操作超时
tc == errc::timed_out ? 1  (1=default_error_condition 映射后相等)
```

With this, we have a completely custom error code system that integrates seamlessly with the standard library. Let's review what each of these four steps actually does:

1. **Define the enum** — Determine the error values, reserving `0` for success.
2. **Specialize `is_error_code_enum`** — Flip the switch to tell the standard library, "This enum is an error code enum."
3. **Write the category singleton** — Provide `name`, `message`, and `default_error_condition` to encapsulate the "meaning" of the error values and their "cross-system equivalence."
4. **Write the `make_error_code` overload** — Tell the standard library how to construct an `error_code` from the enum, found via ADL.

::: warning The category must be a singleton
When `error_code::operator==` checks for "same category," it compares pointers. If your category has more than one instance, two `error_code` objects that are logically the "same category" will compare as unequal. Therefore, always return the category using a `static` local variable inside a function to guarantee a unique instance for the entire process. Missing this step leads to the bizarre bug where "it's clearly the same error, but the comparison says otherwise."
:::

## Working with `expected`: The Modern Approach to Error Code Systems

At this point, you might be wondering: if `login()` returns `error_code` directly, how do we pass back the "successful login token" or other results when it succeeds? `error_code` only holds errors, not values. This is exactly where `std::expected<T, E>` comes into play—by setting `E` to `error_code`, a single type can express both "the successful value" and "the failure error code":

```cpp
// Standard: C++23
#include <expected>
#include <system_error>
#include <iostream>

std::expected<int, std::error_code> read_sensor(int id)
{
    if (id < 0) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    if (id > 100) {
        return std::unexpected(std::make_error_code(std::errc::result_out_of_range));
    }
    return id * 2;   // 成功:隐式构 expected
}

int main()
{
    if (auto r = read_sensor(5); r) {
        std::cout << "read_sensor(5) = " << *r << '\n';
    }
    if (auto r = read_sensor(-1); !r) {
        std::cout << "read_sensor(-1) 失败: " << r.error().message()
                  << " (cat=" << r.error().category().name() << ")\n";
    }
    if (auto r = read_sensor(200); !r) {
        std::cout << "read_sensor(200) 失败: " << r.error().message() << '\n';
    }
    return 0;
}
```

```text
read_sensor(5) = 10
read_sensor(-1) 失败: Invalid argument (cat=generic)
read_sensor(200) 失败: Numerical result out of range
```

The sweet spot of using `E = std::error_code` is this: your error type is a standard, lightweight object of 16 bytes (we verified earlier that `sizeof(error_code) == 16`), which is about the same size as stuffing in an `int`, so it won't bloat `expected`. At the same time, `r.error().message()` gives you a readable description directly, and `r.error() == std::errc::timed_out` allows for cross-system comparison. For custom modules, just set `E` to an `error_code` with your own category—the four-step framework we built above plugs straight into `expected` without modification.

Here's a small pitfall to watch out for: `std::unexpected(std::errc::x)` **does not work**. Since `errc` is a condition enum, `unexpected(errc)` will deduce the error slot's type as `errc` instead of `error_code`, which doesn't match `expected<T, error_code>` and fails to compile. You must explicitly use `std::make_error_code` to wrap `errc`. Custom `MyErrc` enums, however, can use `return std::unexpected(MyErrc::kBoom);` directly—because they implicitly convert to `error_code` (as verified earlier), and the conversion happens during `unexpected`'s construction. The distinction between code enums and condition enums shows up here yet again.

We broke down the full mechanism of `expected` (construction, monadic chaining, and performance comparison against exceptions) in [Post 64](64-expected.md), so here we focus only on how it interfaces with `error_code`. To summarize in one sentence: **`expected<T, error_code>` welds together modern "value-or-error" typed error handling with the standardized, categorized, cross-system error codes of `<system_error>`**—this is one of the cleanest combinations for error handling in modern C++.

## Common Pitfalls

Let's round up the places where things easily go wrong based on our journey above; all of these have been verified through testing:

::: warning Don't confuse errc with error_code
`std::errc` is an `error_condition` enum (`is_error_condition_enum == 1`, `is_error_code_enum == 0`), so `std::error_code ec = std::errc::x;` fails to compile. To construct an `error_code`, use `std::make_error_code(std::errc::x)`, which results in a code under `generic_category`. However, comparisons like `ec == std::errc::x` work fine—they rely on `default_error_condition` equivalence and don't require `errc` to be a code enum.
:::

::: warning code==code compares value+category, not semantics
`error_code{ENOENT, system_category()} == make_error_code(errc::no_such_file_or_directory)` results in `false`—one belongs to `system`, the other to `generic`; different categories mean immediate inequality. Semantically they are the same, so to compare them, use the condition path: `ec == errc::no_such_file_or_directory`. Don't expect direct `==` between two `error_code` objects to perform a semantic comparison.
:::

::: warning Categories must be singletons, or comparisons break
`error_code` compares "same category" by comparing pointers. If your category class isn't implemented as a singleton (e.g., returning a temporary object each time), two `error_code` objects that should be equal will compare as unequal. Always use a function-local `static` variable to return the category reference.
:::

::: warning unexpected(errc) fails to compile
When stuffing errors into `std::expected<T, std::error_code>`, `std::unexpected(std::errc::x)` deduces an `unexpected<errc>`, which doesn't match the type `expected<T, error_code>`. You need `std::unexpected(std::make_error_code(std::errc::x))`. Custom code enums (like `MyErrc`) can omit `make_error_code`—they implicitly convert to `error_code`.
:::

## Summary

The `<system_error>` framework essentially wraps the low-cost "integer error number" model of `errno` in a "categorization + typing" structure. This allows it to be zero-overhead, consistent across modules, and seamlessly integrated with custom error codes. Let's recap the key conclusions:

- **Three tiers of error handling**: Bare return codes (most primitive, uncategorized), `error_code` (explicit, zero-overhead, categorized, standard unified), and exceptions (implicit control flow, overhead, automatic bubbling). The sweet spot for `error_code` is "where failure is normal, in hot paths, and needs cross-module consistency."
- **`error_code` = value + category**: The error number is a raw value, while the category is a singleton providing `name`/`message`/`default_error_condition`. Separating the two ensures a single `int` has a definite meaning only when paired with "which system it belongs to." The standard provides `system_category` (platform errno) and `generic_category` (POSIX generic).
- **`errc` is a condition, not a code**: `std::errc` is an `error_condition` enum and cannot implicitly construct an `error_code` (requires `make_error_code`), but `ec == errc::x` works—via `default_error_condition` equivalence, which acts as the bridge for cross-category and cross-platform comparison.
- **`system_error` exception wraps `error_code`**: Use `error_code` for low-cost returns at the bottom layer, and promote to an exception at the boundary with `throw system_error{ec, "..."}`; the "throwing/non-throwing dual API" of `filesystem` is the standard practice of this pattern.
- **Four steps to custom categories**: 1. Define enum (reserve 0 for success); 2. Specialize `is_error_code_enum<E>` to `true` (enable implicit conversion); 3. Write category singleton (`name`/`message`/optional `default_error_condition`); 4. Write `make_error_code(E)` overload (found via ADL). Once complete, custom error codes are on equal footing with POSIX errors.
- **Pair with `expected<T, error_code>`**: Welds typed "value-or-error" handling with standardized error codes; the 16-byte `error_code` as `E` has near-zero overhead. Note that `unexpected(errc)` fails to compile while `unexpected(MyErrc)` works—another distinction between code enums and condition enums.

In the next post, we'll switch perspectives and look at another error handling paradigm outside of `<system_error>`. If you're interested, you can revisit [Post 64](64-expected.md) for the full mechanism of `expected`, or check out the `<filesystem>` post to see how this "dual API" pattern lands in a real standard library component.

## References

- [cppreference: std::error_code](https://en.cppreference.com/w/cpp/error/error_code) — value + category structure, `operator bool`, comparison semantics
- [cppreference: std::error_category](https://en.cppreference.com/w/cpp/error/error_category) — abstract base class with `name`/`message`/`default_error_condition`
- [cppreference: std::errc](https://en.cppreference.com/w/cpp/error/errc) — POSIX generic error condition enum (note it is an `error_condition` enum)
- [cppreference: std::system_error](https://en.cppreference.com/w/cpp/error/system_error) — exception class wrapping `error_code`, `filesystem_error` inherits from it
- [cppreference: std::is_error_code_enum](https://en.cppreference.com/w/cpp/error/error_code/is_error_code_enum) — trait to enable implicit enum to `error_code` construction (step 2 for custom categories)
