---
chapter: 7
cpp_standard:
- 11
- 17
description: A deep dive into std::error_code's error+category two-layer structure,
  why errc is a condition rather than a code, how system_error wraps error codes
  into exceptions, and how to build a custom category from scratch so custom error
  codes blend seamlessly into the standard system
difficulty: intermediate
order: 66
platform: host
prerequisites:
- 'expected: Value or Error, C++23''s New Error Handling Paradigm'
- 'variant: Type-Safe Unions and visit'
reading_time_minutes: 16
related:
- 'expected: Value or Error, C++23''s New Error Handling Paradigm'
- 'filesystem: C++17 Cross-Platform Filesystem Operations'
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'error_code: The Error Code System and Custom Categories'
translation:
  source: documents/vol3-standard-library/error-utils/66-error-code.md
  source_hash: be053c14613a614bd2ce72b824d0fa26c9136c87affa14f34120ebd978d3cda8
  translated_at: '2026-09-26T01:07:07+00:00'
  engine: anthropic
  token_count: 4300
---
# error_code: The Error Code System and Custom Categories

Sooner or later, every C++ programmer runs into the same question: a function failed — how do we tell the caller about it? The language offers three paths: return codes, `std::error_code`, and exceptions. This article does not cover the internals of the exception machinery (that's another volume's business); we focus on the middle path: how the `error_code` / `error_category` system from `<system_error>` is designed, why it is designed that way, and how to slot your own module's error codes seamlessly into it.

Let's get the motivation straight first. Everyone knows the C-era `errno`: call a system interface, and if it fails, read the global `errno` to get an integer, then look up a one-line description with `strerror(errno)`. It works, but the pitfalls are plain — `errno` is global mutable state (thread-local storage rescues half of it, but the semantics stay awkward), the error number is a bare `int` (the compiler cannot tell whether `2` or `99` is an error code or an ordinary return value), and error numbers from different libraries can collide. C++11's `<system_error>` exists to clean up this mess: it does not abandon `errno`'s low-cost "integer error number" model, but wraps two layers of structure around it, **categorizing** and **typing** the error number, and letting custom error codes and system error codes share one interface.

## Trade-offs Among the Three Error Handling Paths

When you write a function that can fail, you will most likely choose among three options:

- **Raw return codes**: the function returns an `int`, where 0 means success and non-zero is the error number. The most primitive option, zero overhead. The problem is that the caller receives an `int`, and the compiler has no idea whether it is a result or an error code — forget to check it and you're flying blind. And since an `int` carries no classification, a `2` from one module and a `2` from another can mean completely different things.
- **Exceptions**: `throw` an object, and control flow implicitly jumps to the nearest `catch`. The upside is that the "success path" code stays very clean, and errors bubble up the call stack automatically. The cost is runtime overhead (even when nothing is thrown, some ABIs pay table overhead, and constructing the exception object / unwinding the stack is not cheap), plus the fact that it "might throw" is hidden inside the signature (C++ has no mandatory `throws` declaration).
- **`std::error_code`**: the function returns a lightweight object (essentially an `int` plus a pointer to a category) carrying "error number + which system this number belongs to". It is explicit — the return type says `error_code`, so the caller knows at a glance that it must be checked. And it is zero-overhead — no exceptions, no stack unwinding, and the object is just 16 bytes, so stuffing it into an `expected<T, error_code>` costs about the same as an `int`.

This table lays out the trade-offs:

| Dimension | Raw Return Code | `error_code` | Exception |
|---|---|---|---|
| Explicitness | Poor (an `int` tells you nothing) | Good (the type is documentation) | Poor (hidden in the signature) |
| Runtime overhead | 0 | Near 0 (16-byte object) | Present (costs even when not thrown; more when thrown) |
| Error classification | None | Yes (category) | Yes (exception type) |
| Cross-module/cross-library | Everyone rolls their own | One standard system | Everyone rolls their own |
| Failure ignorable | Easy to forget checking | Easy to forget checking (`bool` rescues half) | Cannot be ignored (no catch means crash) |

`error_code`'s sweet spot is the kind of scenario where "failure is the norm, expected, and on the hot path": a file won't open, a network times out, a config entry is invalid. For errors like these, you neither want the overhead of exceptions nor want to give up a unified, comparable, description-queryable error system across modules — which is exactly what `<system_error>` was built to deliver.

## The Composition of `error_code`: value + category, Kept Apart

First, what does an `error_code` look like? It holds exactly two things:

```cpp
// Standard: C++11
// Semantics of std::error_code (simplified)
class error_code {
    int value_;                  // the error number (an integer value)
    const error_category* cat_;  // pointer to a category singleton
};
```

An `int` holds the error number; a pointer points at the `error_category` it belongs to. The key design decision is this **separation**: the error number is a bare value, and a lone `2` means nothing — it could be POSIX `ENOENT` (file does not exist), or "authentication failed" inside your custom module. Only when paired with "which error-number system it belongs to" does that `2` acquire a definite meaning. `error_category` is the carrier of the "error-number system" concept.

`error_category` is an abstract base class; every concrete category is a singleton providing three core capabilities:

- `name()` — what this error-number system is called (for example `"system"`, `"generic"`, `"my-app"`).
- `message(ev)` — translates an error number into human-readable text.
- `default_error_condition(ev)` / `equivalent(...)` — the bridge for cross-category comparison (dedicated discussion below).

The standard library ships two ready-made categories:

- `std::system_category()` — the current platform's system error numbers (the `errno` family: `ENOENT`/`EACCES`/`ETIMEDOUT` ...).
- `std::generic_category()` — the POSIX generic error numbers, stable across platforms (one shared set of `errc` enum values).

Let's run a minimal example that wraps an `errno`-style failure into an `error_code`:

```cpp
// Standard: C++11
#include <system_error>
#include <iostream>
#include <cstring>

int main()
{
    auto* fp = std::fopen("/no/such/file/here", "r");
    if (fp == nullptr) {
        int e = errno;                                   // the raw error number
        std::error_code ec{e, std::system_category()};   // wrapped into an error_code

        std::cout << "value:    " << ec.value() << '\n';
        std::cout << "category: " << ec.category().name() << '\n';
        std::cout << "message:  " << ec.message() << '\n';
    }
    return 0;
}
```

Built and run with `g++ -std=c++20 -O2` (local GCC 16.1.1):

```text
value:    2
category: system
message:  No such file or directory
```

That `2` is the value of `ENOENT`; `system_category` knows that on the current platform, `2` maps to "No such file or directory". Note that we did not write a single line of `strerror` — `message()` did the lookup for us. That is the benefit of the category encapsulating the number-to-description mapping.

`error_code` also has an `operator bool`: it returns `true` when the error number is non-zero (meaning an error occurred). A default-constructed `error_code` has error number 0 and converts to `bool` `false` (no error). So checking for failure is one line, `if (ec)`:

```text
sizeof(error_code)      = 16
default error_code bool = 0
```

16 bytes: an `int` (padded to 8) plus a pointer. Something this light can be passed around between functions or stuffed into an `expected` with overhead you will barely feel.

## `errc`, `make_error_code`, and Cross-Category Comparison

Here comes a counterintuitive point that nearly every beginner steps on: is `std::errc` — the standard library's cross-platform error code enumeration — actually an "error code" or an "error condition"?

The answer is the latter. An enumerator like `errc::no_such_file_or_directory` is **not an `error_code`, but an `error_condition`**. Keep these two concepts apart:

- **`error_code`** — a **concrete, platform-flavored** error number. A `2` under `system_category` is `ENOENT` on Linux; on another platform the value may differ.
- **`error_condition`** — an **abstract, portable** error condition. `no_such_file_or_directory` under `generic_category` means the same thing no matter which platform you are on.

`errc` is an enumeration of `error_condition`s; the standard library marks it as a condition enum via `is_error_condition_enum<std::errc>`. We can verify this directly with the type traits:

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

The consequence is blunt: `std::error_code ec = std::errc::timed_out;` **does not compile**. `errc` is not a code enum, so the path enabling implicit construction of an `error_code` does not exist for it. To turn an `errc` into an `error_code`, you must call `std::make_error_code` explicitly:

```cpp
// Standard: C++11
auto ec = std::make_error_code(std::errc::timed_out);  // built under generic_category
std::cout << "value=" << ec.value()
          << " cat=" << ec.category().name() << '\n';
// value=110 cat=generic   (110 is POSIX's ETIMEDOUT)
```

So if `errc` is a condition, where does it earn its keep? The answer is **comparison**. `error_code` overloads `==` so it can compare directly against an `errc`:

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

Note the difference between these two results — it is the entire reason `default_error_condition` exists:

- `sys_ec == generic_ec` asks "are these two `error_code`s completely identical" (equal value **and** same category). One is `system`, the other `generic` — different categories, hence `0` (not equal).
- `sys_ec == errc::...` takes a different route: the `errc` is first implicitly constructed as an `error_condition`, and during the comparison, `system_category`'s `default_error_condition(2)` **maps** the system error number to the corresponding `generic` condition — which happens to be exactly `no_such_file_or_directory`, so they compare equal.

In other words, `default_error_condition` is the category declaring for itself: "this concrete error number of mine is equivalent to that generic error condition". It builds a bridge between "platform-dependent error codes" and "portable error conditions": an `ENOENT` under `system_category` that you obtained on Linux compares equal to someone else's `no_such_file_or_directory` under `generic_category` — because semantically they are the same thing. That is why, in practice, error checks are almost always written as `ec == std::errc::xxx` rather than comparing against another `error_code`: the former crosses categories and platforms, while the latter is welded to a single category.

::: warning errc Is Not error_code
`errc` is an `error_condition` enum, not an `error_code` enum. `std::error_code ec = std::errc::x;` will not compile — you need `std::make_error_code(std::errc::x)`. But `ec == std::errc::x` compiles and works correctly — the `==` path goes through `default_error_condition` equivalence and does not require `errc` to be a code enum. This distinction is one of the cores of `<system_error>`'s design; mix it up and you either get a compile failure or write comparisons that look right but never match.
:::

## `system_error`: Wrapping an error_code in an Exception

`error_code` is the explicit return path, but sometimes you still want the exception's "automatically bubble up" semantics — say, deep in a call stack, where a low-level `error_code` failure would otherwise have to be relayed upward layer by layer and you would rather just throw. `<system_error>` provides a ready-made exception class, `std::system_error`, which wraps an `error_code` internally:

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

`what()` concatenates the context string we passed with `error_code::message()`, which makes troubleshooting at a glance much easier. After the `catch`, `e.code()` pulls the `error_code` back out so you can continue with the usual `value()/category()/== errc` logic. This is the junction between `error_code` and exceptions: down in the low layers you return failures the cheap way with `error_code`, and at a boundary where you decide "this error is worth interrupting control flow", you promote it with `throw system_error{ec, "..."}`; conversely, catching a `system_error` hands you back the `error_code`. The two paths interoperate — it is not either/or.

The heaviest user of this trick in the standard library is `<filesystem>` (C++17). Every fallible `std::filesystem` function has two overloads: one that throws `filesystem_error` (which derives from `system_error`), and one that takes a `std::error_code&` output parameter and does not throw:

```cpp
// Standard: C++17
#include <filesystem>
#include <system_error>
#include <iostream>

namespace fs = std::filesystem;

int main()
{
    std::error_code ec;
    fs::file_size("/no/such/path", ec);   // non-throwing overload: the error lands in ec
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

The caller picks: want an interrupting exception? Call the version that takes no `ec`. Want to handle it yourself without exceptions breaking your control flow? Pass `ec`. This "dual API" pattern is everywhere in `filesystem` and `asio` — in essence it treats `error_code` as an "optional substitute for exceptions" and hands the decision to the caller. The custom categories we are about to build exist precisely so that your own modules can join this "error codes can live anywhere" system.

## Custom Categories: Building an Error Code System from Scratch

This is the heart of the article. The most elegant part of the standard library's `error_code` design is that it is not reserved for system errors — any module can register its own `error_category`, define its own set of error numbers, and then carry them in the unified `error_code` type and manipulate them through the same `message()`/`== errc` interface. Your module's errors and POSIX errors are equals at the type level.

Let's build one from scratch right now. Suppose we are writing a network login module with these failures: network down, authentication failed, timeout, malformed payload. The goal: `login()` returns `std::error_code`; the caller can check with `ec == MyErrc::kAuthFailed`, fetch a Chinese-language description via `ec.message()`, and even have `kTimeout` compare equal to the standard `errc::timed_out` automatically.

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

Note that the values start at 0 and 0 is reserved for "success" — aligned with the `error_code::operator bool` convention (non-zero means error).

### Step 2: Specialize `is_error_code_enum`, Enabling Implicit Conversion

An enum alone is not enough — the standard library does not know it is an "error code enum". You must specialize `std::is_error_code_enum` and mark it `true`; only then will `error_code` enable that implicit-construction-from-enum path:

```cpp
// Standard: C++11
namespace std {
template <>
struct is_error_code_enum<MyErrc> : true_type {};
}  // namespace std
```

This step is the "switch" connecting a custom enum to the `error_code` system. Earlier we verified that for `std::errc` this switch reads `0` (it is a condition enum), which is why `errc` cannot implicitly construct an `error_code`; our `MyErrc` specialization is `true`, so `MyErrc` can.

### Step 3: Write the Custom Category Singleton

A category is a class deriving from `std::error_category` that implements those virtual functions. It must be a singleton — `error_code` internally stores a pointer to the category, and checking whether two `error_code`s share a category is a pointer comparison, so each category gets exactly one instance per process:

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

    // Map custom error numbers to generic error_conditions for cross-category equivalence
    std::error_condition default_error_condition(int ev) const noexcept override {
        switch (static_cast<MyErrc>(ev)) {
            case MyErrc::kTimeout:
                return std::make_error_condition(std::errc::timed_out);
            default:
                return {ev, *this};   // everything else stays in this category
        }
    }
};

// Singleton factory: the single process-wide instance
const std::error_category& my_category() {
    static MyCategory instance;
    return instance;
}
```

`message()` translates our own error numbers into human-readable text — now we do not have to write `ec.message()` ourselves. `default_error_condition()` is optional but pivotal: we have declared "my `kTimeout` is equivalent to the standard `errc::timed_out`". The consequence: a caller holding an `error_code` of `MyErrc::kTimeout` can directly check `if (ec == std::errc::timed_out)` — across categories, across "my module vs the standard library", the semantics line up.

### Step 4: Write the `make_error_code` Overload

The switch is on and the category is written; the last step is providing a `make_error_code(MyErrc)` function that tells the standard library "how to build an `error_code` from a `MyErrc`". It gets found via ADL (argument-dependent lookup), so it goes either into the namespace containing `MyErrc` or into namespace `std` (the former is the official recommendation):

```cpp
// Standard: C++11
std::error_code make_error_code(MyErrc e) {
    return {static_cast<int>(e), my_category()};
}
```

With those four steps in place, the custom error code system works. Now a function that uses it:

```cpp
// Standard: C++11
std::error_code login(bool network_ok, bool password_ok) {
    if (!network_ok) return MyErrc::kNetworkDown;   // implicit conversion to error_code
    if (!password_ok) return MyErrc::kAuthFailed;
    return MyErrc::kSuccess;
}
```

Look at the line `return MyErrc::kNetworkDown;` — because the `is_error_code_enum` specialization from step 2 is `true`, `error_code`'s enabling constructor is activated, and the compiler automatically calls step 4's `make_error_code` to convert it into an `error_code`. **The implicit conversion works here**, in contrast with `errc`'s "cannot implicitly construct an error_code" — this is exactly where the "code enum vs condition enum" distinction lands.

Let's run the full thing:

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

It compiles under `g++ -std=c++20 -O2 -Wall -Wextra` and prints:

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

And just like that, an error code system entirely our own meshes seamlessly with the standard library's. Let's recap what each of the four steps does:

1. **Define the enum** — pin down the error numbers, reserving 0 for success.
2. **Specialize `is_error_code_enum`** — flip the switch on, telling the standard library "this enum is an error_code enum".
3. **Write the category singleton** — provide `name`/`message`/`default_error_condition`, encapsulating both the "meaning" of the error numbers and their "cross-system equivalence relations".
4. **Write the `make_error_code` overload** — tell the standard library how to build an error_code from the enum; it is found via ADL.

::: warning A Category Must Be a Singleton
When `error_code::operator==` checks "same category?", it compares pointers. If your category has more than one instance, two `error_code`s that logically belong to "the same category" will compare unequal. So a category is always returned from a function-local `static` variable, guaranteeing a single instance per process. Skip this step and you get the eerie bug where "it is clearly the same error, but the comparison says otherwise".
:::

## Pairing with `expected`: The Modern Use of the Error Code System

At this point you might wonder: `login()` returns an `error_code` directly, so how does it hand back a result like "the token from a successful login" when it succeeds? An `error_code` carries only errors, not values. This is precisely where `std::expected<T, E>` shines — set `E` to `error_code`, and one type expresses both "the value on success" and "the error code on failure":

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
    return id * 2;   // success: implicitly constructs the expected
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

The sweet part of the `E = std::error_code` route: your error type is a standard 16-byte lightweight object (we verified `sizeof(error_code)=16` earlier), barely more than an `int`, so it does not bloat the `expected`; meanwhile `r.error().message()` directly yields a readable description, and `r.error() == std::errc::timed_out` checks across systems. For a custom module, just set `E` to the `error_code` from your own category — the system built by the four steps above drops into `expected` unchanged.

One more small trap to flag: `std::unexpected(std::errc::x)` **does not work**. Since `errc` is a condition enum, `unexpected(errc)` deduces the error slot's type as `errc` rather than `error_code`, which does not line up with `expected<T, error_code>` — it will not compile. Wrapping an `errc` must go explicitly through `std::make_error_code`. A custom `MyErrc`, on the other hand, can be used directly as `return std::unexpected(MyErrc::kBoom);` — it converts implicitly to `error_code` (verified earlier), and the conversion happens during `unexpected`'s construction. Code enums and condition enums part ways once more here.

We already took `expected`'s full machinery apart (construction, monadic chaining, the performance comparison against exceptions) in article 64; here we only care about how it meshes with `error_code` on this end. One-sentence summary: **`expected<T, error_code>` welds the modern typed "value or error" error handling onto `<system_error>`'s standardized, categorizable, cross-system error codes** — one of the cleanest combinations in C++ error handling today.

## Real Pitfalls People Actually Hit

Let's collect the spots where this journey tends to flip over — all verified by the tests above:

::: warning Don't Treat errc as error_code
`std::errc` is an `error_condition` enum (`is_error_condition_enum=1`, `is_error_code_enum=0`), so `std::error_code ec = std::errc::x;` will not compile. To build an `error_code`, use `std::make_error_code(std::errc::x)`, which yields a code under `generic_category`. But a comparison like `ec == std::errc::x` is fine — it goes through `default_error_condition` equivalence and does not require `errc` to be a code enum.
:::

::: warning code==code Compares value+category, Not Semantics
`error_code{ENOENT, system_category()} == make_error_code(errc::no_such_file_or_directory)` evaluates to `false` — one is `system`, the other `generic`, different categories, so they are judged unequal straight away. Semantically they are the same thing; to compare them that way you need the condition path, `ec == errc::no_such_file_or_directory`. Do not `==` two `error_code`s directly and expect semantic comparison.
:::

::: warning A Category Must Be a Singleton, or Comparisons Break
When `error_code` compares "same category", it compares pointers. If the category class is not made a singleton (say it returns a temporary each time), two `error_code`s that should be equal will compare unequal. Always return the category reference from a function-local `static` variable.
:::

::: warning unexpected(errc) Will Not Compile
To stuff an error into `std::expected<T, std::error_code>`, `std::unexpected(std::errc::x)` deduces an `unexpected<errc>`, which does not match `expected<T, error_code>`. Use `std::unexpected(std::make_error_code(std::errc::x))`. Custom code enums (like `MyErrc`) can skip `make_error_code` — they convert implicitly to `error_code`.
:::

## Summary

At its core, the `<system_error>` system takes `errno`'s low-cost "integer error number" model and wraps a "categorize + type" structure around it, so that it stays zero-overhead, unifies across modules, and lets custom error codes blend in seamlessly. The key takeaways:

- **Three error handling paths**: raw return codes (most primitive, no classification), `error_code` (explicit, zero-overhead, classified, standardized), exceptions (implicit control flow, overhead, automatic bubbling). `error_code`'s sweet spot is "failure is the norm, on the hot path, and you want cross-module unification".
- **`error_code` = value + category**: the error number is a bare value; the category is a singleton providing `name`/`message`/`default_error_condition`. The separation is what lets an `int` gain a definite meaning once paired with "which system it belongs to". The standard ships `system_category` (platform errno) and `generic_category` (POSIX generic).
- **`errc` is a condition, not a code**: `std::errc` is an `error_condition` enum and cannot implicitly construct an `error_code` (use `make_error_code`), but `ec == errc::x` works — via `default_error_condition` equivalence, which is the bridge for cross-category, cross-platform comparison.
- **`system_error` wraps an `error_code` in an exception**: return cheaply with `error_code` in the low layers, promote at boundaries with `throw system_error{ec, "..."}`; `filesystem`'s "throwing / non-throwing dual API" is the canonical practice of this pattern.
- **Custom categories in four steps**: ① define the enum (0 reserved for success); ② specialize `is_error_code_enum<E>` to `true` (flip the implicit-conversion switch on); ③ write the category singleton (`name`/`message`/optional `default_error_condition`); ④ write the `make_error_code(E)` overload (found via ADL). Once complete, custom error codes and POSIX errors are equals at the type level.
- **Pair with `expected<T, error_code>`**: welds typed "value or error" handling onto standardized error codes; a 16-byte `error_code` as `E` is nearly free. Remember `unexpected(errc)` will not compile while `unexpected(MyErrc)` will — the code enum vs condition enum distinction again.

In the next article we switch angles and look at another error handling paradigm beyond `<system_error>` — if you are interested, circle back to article 64 for `expected`'s full machinery, or hop over to the `<filesystem>` article to see how this "dual API" pattern lands in a real standard library component.

## References

- [cppreference: std::error_code](https://en.cppreference.com/w/cpp/error/error_code) — the value + category structure, `operator bool`, comparison semantics
- [cppreference: std::error_category](https://en.cppreference.com/w/cpp/error/error_category) — the abstract base class and `name`/`message`/`default_error_condition`
- [cppreference: std::errc](https://en.cppreference.com/w/cpp/error/errc) — the POSIX generic error condition enum (note that it is an `error_condition` enum)
- [cppreference: std::system_error](https://en.cppreference.com/w/cpp/error/system_error) — the exception class wrapping an `error_code`; `filesystem_error` derives from it
- [cppreference: std::is_error_code_enum](https://en.cppreference.com/w/cpp/error/error_code/is_error_code_enum) — the trait enabling implicit enum-to-`error_code` construction (step 2 of a custom category)
