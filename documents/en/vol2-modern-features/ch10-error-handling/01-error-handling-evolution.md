---
chapter: 10
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: 'Error codes, exceptions, optional, expected: the evolution and selection
  of error handling strategies'
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Chapter 4: std::optional: Elegantly Expressing ''A Value May Be Absent'''
- 'Chapter 4: std::variant: A Type-Safe Union'
reading_time_minutes: 13
related:
- optional for Error Handling
- 'std::expected<T, E>: Type-Safe Error Propagation'
tags:
- host
- cpp-modern
- intermediate
- 类型安全
title: 'Evolution of Error Handling: From Error Codes to Type Safety'
translation:
  source: documents/vol2-modern-features/ch10-error-handling/01-error-handling-evolution.md
  source_hash: 6ad0139fc45f10a6aa948947d2c6ceade231f57c39a5588d246f362379676315
  translated_at: '2026-09-25T16:47:46+00:00'
  engine: anthropic
  token_count: 4800
---
# Evolution of Error Handling: From Error Codes to Type Safety

After all these years of writing C++, one lesson has sunk in deepest for us: **error handling is always the hardest part of a project to get right**. Not because it is complicated — precisely because it looks too simple. Plenty of people assume `if (ret != 0)` or `try { ... } catch (...)` is enough, but once the project reaches the maintenance stage, you discover unchecked errors everywhere, silently swallowed exceptions, and function calls that fail for reasons nobody can tell.

In this chapter we walk through the evolution of error handling in C++: from C-style error codes to C++ exceptions, then to C++17's `optional` / `variant`, and finally to C++23's `expected`. Only after we understand what problem each approach solves — and what new problems it introduces — can we make a reasonable choice when facing a concrete scenario.

------

## The Starting Point: C-Style Error Codes

If you have written C, or maintained a large C legacy project, this code will look all too familiar:

```cpp
// Classic C style: an integer return value signals success/failure
#define ERR_FILE_NOT_FOUND  (-1)
#define ERR_PERMISSION      (-2)
#define ERR_INVALID_FORMAT  (-3)

int read_config(const char* path, Config* out) {
    FILE* f = fopen(path, "r");
    if (!f) return ERR_FILE_NOT_FOUND;

    char buffer[4096];
    size_t n = fread(buffer, 1, sizeof(buffer), f);
    fclose(f);

    if (n == 0) return ERR_INVALID_FORMAT;

    // Parsing logic...
    return 0;  // success
}

// Caller
Config cfg;
int ret = read_config("app.cfg", &cfg);
if (ret != 0) {
    // Is ret -1, -2, or -3?
    // You have to dig through the macro definitions in the header
    printf("Error: %d\n", ret);
}
```

The problem with this style is not whether it "works", but whether code written this way can **run reliably**.

The first problem is **ignorability**. An error code is a plain `int`; the caller can simply skip checking the return value, and the compiler will not emit a single warning. We have seen far too much code like this: a function returns an error code, the caller ignores it and presses on, and eventually the program crashes in some bizarre way — with the error site and the crash site possibly a dozen function calls apart.

The second problem is **scarce information**. What does a bare `-1` tell you? File not found? Insufficient permission? Disk full? You have to go read the documentation or the macro definitions in the header, and pray that this function's docs are the latest version. Worse, different modules may use the same integer to mean different things: `-1` is "file not found" in module A and quite possibly "timeout" in module B.

The third problem is **dependence on global state**. The C standard library's classic `errno` mechanism is a textbook example — it is a global variable, and if you forget to save `errno` between two function calls, its value gets overwritten. In a multithreaded environment this is an outright disaster; modern implementations use thread-local storage, but the mental burden remains considerable.

The fourth problem is **resource-leak risk**. The `read_config` above performs only one step, so the placement of `fclose` is still reasonably clear. But if you have five steps that can each fail, every one of them must correctly clean up the resources allocated earlier before exiting — this is where the `goto cleanup` pattern comes from. It works, but the code reads like spaghetti.

------

## Stage Two: The C++ Exception Mechanism

C++ introduced the exception mechanism to tackle the core pain point of error codes — separating error handling from the control flow, so that the code on the "normal path" is not interrupted by error checks:

```cpp
#include <stdexcept>
#include <fstream>
#include <string>

Config read_config(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        throw std::runtime_error("Cannot open: " + path);
    }

    std::string content;
    std::getline(f, content, '\0');

    if (content.empty()) {
        throw std::runtime_error("Empty config file");
    }

    return parse_config(content);  // parse_config may throw as well
}

// Caller
void init_system() {
    try {
        auto cfg = read_config("app.cfg");
        apply_config(cfg);
    } catch (const std::runtime_error& e) {
        std::cerr << "Config error: " << e.what() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Unknown error: " << e.what() << "\n";
    }
}
```

Exceptions solve a lot of problems: the code on the normal path becomes clean, errors cannot be silently ignored (an uncaught exception terminates the program), and RAII combined with stack unwinding cleans up resources automatically. In application-level development, exceptions are a genuinely pleasant tool.

But exceptions have their own problems, and some of them are fatal in specific scenarios.

The first is **unpredictable performance**. On the "happy path" (that is, when no exception is thrown), the cost of exceptions is nearly zero — that is the design goal of zero-overhead abstraction. But once an exception is thrown, the cost of stack unwinding is huge, involving stack-frame traversal, destructor calls, exception-object copies, and so on. For scenarios where errors happen only occasionally this is not a problem; but if your network service handles 100,000 requests per second and 5% of them fail, using exceptions for these "anticipated failures" no longer fits.

The second is **opaque control flow**. Look at the `init_system` code above: can you tell at a glance which exceptions `read_config` and `apply_config` might each throw? Most likely not, unless you carefully read the documentation or the function implementations. C++ exceptions are "invisible" — the function signature does not declare what it may throw (the `throw()` specification was removed in C++17; `noexcept`, as a specifier, promises that nothing will be thrown, but there is no way to annotate which exception types might be thrown).

Third, and most critical — **embedded environments usually disable exceptions**. The exception mechanism requires runtime support (stack-unwinding information, RTTI, and so on), all of which increases binary size. On many embedded platforms, `-fno-exceptions` is the default option, which means you simply cannot use `throw` / `catch`. Code generated with exception support by the GNU ARM toolchain is 50 KB to 200 KB larger than code without it, depending on the case; on an MCU with only 64 KB of Flash, that overhead is fatal.

Finally, there is the complexity of **exception safety**. Writing exception-safe code requires a deep understanding of concepts such as RAII, the strong exception guarantee, and the basic exception guarantee. If a constructor throws, the object may be left half-constructed; if a `push_back` throws, the container may be left half-modified. This is not the exception mechanism's fault, but it certainly adds to the mental burden.

------

## Stage Three: Error Codes + Enums, Improved

Since exceptions are unavailable in some scenarios, we return to the error-code approach — but this time we let the C++ type system make up for its shortcomings:

```cpp
#include <string>
#include <string_view>

enum class ConfigError {
    kSuccess,
    kFileNotFound,
    kPermissionDenied,
    kInvalidFormat,
    kParseError,
};

struct ConfigResult {
    ConfigError error;
    std::string message;  // additional error description

    constexpr bool ok() const noexcept {
        return error == ConfigError::kSuccess;
    }
};

ConfigResult read_config(std::string_view path, Config& out) {
    auto f = open_file(path);
    if (!f) {
        return {ConfigError::kFileNotFound,
                std::string("Cannot open: ") + std::string(path)};
    }

    auto content = read_content(f);
    if (content.empty()) {
        return {ConfigError::kInvalidFormat, "Empty file"};
    }

    auto parsed = parse_config(content);
    if (!parsed) {
        return {ConfigError::kParseError, "Malformed config"};
    }

    out = std::move(*parsed);
    return {ConfigError::kSuccess, {}};
}
```

Using `enum class` instead of macros or bare `int` to represent error codes is already significant progress — type safety, namespace isolation, IDE-completion friendliness. With the additional information carried by the `std::string`, the caller can finally find out what exactly went wrong.

But the core problem persists: **the compiler will not force you to check the return value**. `ConfigResult` is still an ordinary struct; if you do not call `.ok()`, the program will keep running all the same, using an uninitialized `Config` object for subsequent operations. Besides, the `std::string` inside `ConfigResult` means a heap allocation, which may not be what you want in an embedded environment.

------

## Stage Four: Type-Safe Error Types

C++17 introduced `std::optional` and `std::variant`, and C++23 introduced `std::expected`; they reconsider error handling at the type-system level. The core idea: **make the "may fail" information part of the type, and let the compiler check it for you, rather than relying on programmer discipline**.

### std::optional: Success or No Value

```cpp
#include <optional>
#include <string>
#include <unordered_map>

std::optional<User> find_user(int id) {
    static const std::unordered_map<int, User> kUsers = {
        {1, User{"Alice", 30}},
        {2, User{"Bob", 25}},
    };

    auto it = kUsers.find(id);
    if (it != kUsers.end()) {
        return it->second;
    }
    return std::nullopt;
}

// Caller — must check whether a value is present
auto user = find_user(42);
if (user) {
    std::cout << user->name << "\n";
} else {
    std::cout << "User not found\n";
}
```

`optional` suits the simple scenario of "on success, a value; on failure, no value". Its advantage is explicit semantics — one look at `std::optional<User>` tells you "there may be no value here", far clearer than returning `nullptr` or an error code.

But `optional` cannot carry the reason for the failure. When `find_user` returns `nullopt`, all you know is "not found" — not whether the ID does not exist, the database connection dropped, or permissions were insufficient.

### std::variant: Expressing Multiple States

```cpp
#include <variant>
#include <string>

struct FileNotFoundError { std::string path; };
struct ParseError { int line; std::string detail; };
struct PermissionError { std::string user; };

using ConfigError = std::variant<
    FileNotFoundError,
    ParseError,
    PermissionError
>;

using ConfigResult = std::variant<Config, ConfigError>;

ConfigResult read_config(const std::string& path) {
    // ...
    return Config{42, "default"};
    // or
    // return FileNotFoundError{path};
}
```

`variant` can express multiple error types, giving it more expressive power than `optional`. But the usage experience is not ideal — every access requires `std::visit`, or `std::holds_alternative` combined with `std::get`, so the code turns verbose; and since the error types and the success type are mixed inside the same `variant`, the semantics are not as intuitive as "value or error".

### std::expected: Value or Error

```cpp
#include <expected>
#include <string>

enum class ConfigError {
    kFileNotFound,
    kParseError,
    kPermissionDenied,
};

std::expected<Config, ConfigError> read_config(const std::string& path) {
    auto f = open_file(path);
    if (!f) {
        return std::unexpected(ConfigError::kFileNotFound);
    }

    auto content = read_content(f);
    auto parsed = parse_config(content);
    if (!parsed) {
        return std::unexpected(ConfigError::kParseError);
    }

    return *parsed;
}

// Caller
auto result = read_config("app.cfg");
if (result) {
    apply_config(result.value());
} else {
    // the error details live in result.error()
    handle_error(result.error());
}
```

The semantics of `expected<T, E>` are completely straightforward: **on success it holds a value of type `T`; on failure it holds an error of type `E`**. It has the simplicity of `optional` and, like `variant`, can carry error information. Moreover, C++23's `expected` comes with monadic operations (`and_then`, `transform`, `or_else`, and more), letting you chain multiple fallible operations elegantly — we will cover that in detail in a later article.

------

## Evolution Timeline

Let's summarize the evolution of C++ error-handling approaches with a timeline:

**The C era (1970s)**: error codes + `errno`. Crude and rough, ignorable, little information.

**C++98 (1998)**: the exception mechanism. Elegant but heavyweight, requires RTTI support, opaque control flow.

**C++11 (2011)**: `std::error_code` standardized, providing a more disciplined framework for error codes. The `<system_error>` header introduced a cross-platform error categorization mechanism.

**C++17 (2017)**: `std::optional` expresses "there may be no value", and `std::variant` expresses "one of several possible types". This was the first step toward type-safe error handling, but neither is specialized enough.

**C++23 (2023)**: `std::expected<T, E>` officially entered the standard, with monadic operations attached. This is the C++ standards committee's formal endorsement of the "type-safe error handling" route.

The evolution from error codes to expected is also presented as an animation — you can play it, pause it, or use the step button to single-step through it, and see clearly what each generation solved and what new problems it introduced:

<Anim id="error-evolution" />

------

## Comparing the Approaches

We put the characteristics of the four mainstream approaches side by side in a comparison table:

| Feature | Error code / enum | Exceptions | optional | expected |
|---------|-------------------|------------|----------|----------|
| **Ignorability** | Easy to ignore | Cannot be ignored (uncaught terminates) | Ignorable | Ignorable |
| **Error information** | Limited (integer/enum) | Rich (exception object) | None (value present or not) | Rich (custom E) |
| **Performance (happy path)** | Nearly zero overhead | Nearly zero overhead | Nearly zero overhead | Nearly zero overhead |
| **Performance (failure path)** | Zero overhead | Heavy (stack unwinding) | Zero overhead | Zero overhead |
| **Composability** | Poor (manual propagation) | Good (automatic propagation) | Moderate | Good (monadic operations) |
| **Code bloat** | None | Potentially large | Minimal | Small |
| **Usable on embedded** | Fully usable | Usually disabled | Fully usable | Fully usable |
| **Compiler-enforced checking** | No | No | No | No |
| **Requires RTTI** | No | Yes | No | No |

One fact worth noting: in C++, the types provided by the standard library (such as `expected` and `optional`) are **by default not compiler-enforced the way Rust's `Result<T, E>` is**. Rust's `#[must_use]` attribute makes the compiler warn when a caller ignores a `Result`; C++'s `[[nodiscard]]` has similar functionality, but the standard library has not applied this attribute to these types (this is also a topic of community discussion — see [P2422R1](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2422r1.html)). That said, you can add `[[nodiscard]]` to your own return types in your project and gain the effect of compiler-enforced checking.

------

## Special Considerations for Embedded Scenarios

In embedded development, the choice of error handling is often not a question of "which one is better", but of "which one is even available".

**Disabled exceptions** are the most common constraint in embedded development. ARM compilers' default configuration is typically `-fno-exceptions -fno-rtti`, which means `throw` / `catch` simply will not compile. So if you are writing embedded code, `optional`, `variant`, and `expected` are basically your main options.

**Deterministic error handling** is another key requirement. In a real-time system, you cannot accept "the time taken by error handling is uncertain" — the stack-unwinding time of exceptions is unpredictable, which is unacceptable in hard real-time systems. Return-value approaches (error codes, `optional`, `expected`) have deterministic execution time and fit real-time scenarios better.

**Memory overhead** also needs consideration. `std::expected<T, E>` typically takes up `sizeof(E)` plus some alignment padding more than `T`. If `E` is a simple enum, the extra overhead is only a few bytes; if `E` contains a `std::string`, heap allocations come in. On an MCU with only tens of KB of RAM, these overheads need to be weighed carefully.

**Practical advice**: for embedded projects, the strategy we recommend is to use lightweight error types (enums or small structs) with `expected` semantics — implement a simplified `expected` of your own (possible with C++17), or simply return a struct. In extremely resource-constrained scenarios, you can even fall back to enum error codes — but cultivate the team discipline of "the return value must be checked".

------

## References

- [cppreference: Error handling](https://en.cppreference.com/w/cpp/error)
- [P0786R1 - std::expected proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0786r1.html)
- [C++ Core Guidelines: Error handling](https://isocpp.org/wiki/faq/exceptions)
