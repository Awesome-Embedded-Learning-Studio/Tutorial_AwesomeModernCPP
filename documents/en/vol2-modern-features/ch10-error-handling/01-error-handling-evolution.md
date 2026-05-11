# The Evolution of Error Handling: From Error Codes to Type Safety

In my years of writing C++, one thing has stood out above all else: **error handling is always the hardest part to get right in a project**. Not because it's complex—precisely because it looks too simple. Many people assume ``if (ret != 0)`` or ``try { ... } catch (...)`` is good enough, but once you hit the maintenance phase, you discover unhandled errors everywhere, swallowed exceptions, and function calls failing for completely unknown reasons.

In this chapter, we will thoroughly trace the evolution of C++ error handling: from C-style error codes to C++ exceptions, then to C++17's ``optional`` / ``variant``, and finally to C++23's ``expected``. Only by understanding what problems each approach solves and what new problems it introduces can we make sound decisions when facing a specific scenario.

------

## The Starting Point: C-Style Error Codes

If you have ever written C or maintained a large legacy C codebase, the following code will look all too familiar:

```cpp
// 经典 C 风格：用整数返回值表示成功/失败
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

    // 解析逻辑...
    return 0;  // 成功
}

// 调用方
Config cfg;
int ret = read_config("app.cfg", &cfg);
if (ret != 0) {
    // ret 到底是 -1、-2 还是 -3？
    // 得去翻头文件里的宏定义
    printf("Error: %d\n", ret);
}
```

The problem with this approach is not whether it "works," but whether **code written with it can run reliably**.

The first problem is **ignorability**. An error code is a plain ``int``, and the caller can completely ignore the return value without the compiler issuing any warning. I have seen far too much code like this: a function returns an error code, the caller ignores it and continues execution, and eventually the program crashes in a bizarre way—and the point where the error occurred might be a dozen function calls away from the point of the crash.

The second problem is **scant information**. What can a ``-1`` tell you? File not found? Insufficient permissions? Disk full? You have to check the documentation or the macro definitions in the header file, and then pray that the documentation is up to date. Even worse, different modules might use the same integer to represent different meanings; ``-1`` might mean "file not found" in module A, but "timeout" in module B.

The third problem is **reliance on global state**. The classic ``errno`` mechanism in the C standard library is a prime example—it is a global variable, and if you forget to save ``errno`` between two function calls, its value gets overwritten. In a multithreaded environment, this is a disaster. Although modern implementations use thread-local storage, the mental burden remains significant.

The fourth problem is **the risk of resource leaks**. The ``read_config`` above has only one step, so the placement of ``fclose`` is relatively clear. But if you have five steps that could fail, and each step requires properly cleaning up previously allocated resources before exiting—that is exactly how the ``goto cleanup`` pattern came about. It works, but the code reads like spaghetti.

------

## Stage Two: The C++ Exception Mechanism

C++ introduced the exception mechanism to solve the core pain points of error codes—separating error handling from control flow, and keeping the "happy path" code free from error-checking interruptions:

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

    return parse_config(content);  // parse_config 也可能抛异常
}

// 调用方
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

Exceptions solve many problems: the happy path code becomes clear, errors cannot be silently ignored (uncaught exceptions terminate the program), and RAII combined with stack unwinding automatically cleans up resources. In application-layer development, exceptions are a quite handy tool.

But exceptions have their own problems, and some of them are fatal in specific scenarios.

The most prominent issue is **performance non-determinism**. The performance overhead of exceptions on the "happy path" (i.e., when no exception is thrown) is nearly zero—this is the design goal of zero-overhead abstraction. However, once an exception is thrown, the overhead of stack unwinding is massive, involving stack frame traversal, destructor calls, and exception object copying. For "occasional errors" this is not a problem, but if your network service handles 100,000 requests per second and 5% of them fail, using exceptions to handle these "expected failures" is inappropriate.

The second issue is **opaque control flow**. Looking at the ``init_system`` code above, can you tell at a glance what exceptions ``read_config`` and ``apply_config`` might throw? Probably not, unless you carefully read the documentation or the function implementation. C++ exceptions are "invisible"—function signatures do not indicate what they might throw (the ``throw()`` specification was removed in C++17, and ``noexcept`` as a specifier only promises not to throw, but cannot annotate what types of exceptions might be thrown).

The third, and most critical issue, is that **exceptions are typically disabled in embedded environments**. The exception mechanism requires runtime support (stack unwinding information, RTTI, etc.), all of which increase binary size. On many embedded platforms, ``-fno-exceptions`` is the default option, meaning you simply cannot use ``throw`` / ``catch``. Code generated by the GNU ARM toolchain with exception support can be 50KB to 200KB larger than code without it. On an MCU with only 64KB of Flash, this overhead is fatal.

Finally, there is the **complexity of exception safety**. Writing exception-safe code requires a deep understanding of RAII, the strong exception guarantee, the basic exception guarantee, and other concepts. If an exception is thrown in a constructor, the object might be in a half-constructed state; if a ``push_back`` throws an exception, the container might be in a half-modified state. This is not the fault of the exception mechanism itself, but it does increase the mental burden.

------

## Stage Three: Improving Error Codes with Enums

Since exceptions are unavailable in certain scenarios, we return to the error code approach, but use C++'s type system to make up for its shortcomings:

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
    std::string message;  // 附加的错误描述

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

Using ``enum class`` instead of macros or bare ``int`` to represent error codes is already a significant step forward—it offers type safety, namespace isolation, and IDE auto-completion friendliness. With ``std::string`` attached for additional information, the caller can finally know exactly what went wrong.

But the core problem remains: **the compiler does not force you to check the return value**. ``ConfigResult`` is still a plain struct. If you do not call ``.ok()``, the program will continue running, using an uninitialized ``Config`` object for subsequent operations. Additionally, the ``std::string`` in ``ConfigResult`` implies heap allocation, which in an embedded environment might not be what you want.

------

## Stage Four: Type-Safe Error Types

C++17 introduced ``std::optional`` and ``std::variant``, and C++23 introduced ``std::expected``. These re-examine error handling from the level of the type system. The core idea is: **make "might fail" part of the type itself, letting the compiler do the checking instead of relying on programmer discipline**.

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

// 调用方——必须检查是否有值
auto user = find_user(42);
if (user) {
    std::cout << user->name << "\n";
} else {
    std::cout << "User not found\n";
}
```

``optional`` is well-suited for simple scenarios expressing "successfully returns a value, or fails with no value." Its advantage lies in clear semantics—seeing ``std::optional<User>`` immediately tells you "there might be no value here," which is much clearer than returning ``nullptr`` or an error code.

However, ``optional`` cannot carry an error reason. When ``find_user`` returns ``nullopt``, you only know "not found," but you do not know whether it is because the ID does not exist, the database connection dropped, or there are insufficient permissions.

### std::variant: Multi-State Expression

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
    // 或
    // return FileNotFoundError{path};
}
```

``variant`` can express multiple error types, offering stronger expressiveness than ``optional``. But the developer experience is not ideal—every access requires ``std::visit`` or ``std::holds_alternative`` combined with ``std::get``, making the code rather verbose. Furthermore, error types and success types are mixed together in the same ``variant``, which is semantically less intuitive than "value or error."

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

// 调用方
auto result = read_config("app.cfg");
if (result) {
    apply_config(result.value());
} else {
    // 错误信息就在 result.error() 里
    handle_error(result.error());
}
```

The semantics of ``expected<T, E>`` are very straightforward: **on success, it holds a value of type ``T``; on failure, it holds an error of type ``E``**. It has the conciseness of ``optional`` and can carry error information like ``variant``. Moreover, C++23's ``expected`` comes with built-in monadic operations (``and_then``, ``transform``, ``or_else``, etc.), allowing you to elegantly chain multiple operations that might fail—we will cover this in detail in a future article.

------

## Evolution Timeline

Let us use a timeline to summarize the evolution of C++ error handling approaches:

**C Language Era (1970s)**: Error codes + ``errno``. Simple and brute-force, ignorable, with little information.

**C++98 (1998)**: Exception mechanism. Elegant but heavy, requires RTTI support, opaque control flow.

**C++11 (2011)**: ``std::error_code`` standardization, providing a more standardized framework for error codes. The ``<system_error>`` header introduced a cross-platform error categorization mechanism.

**C++17 (2017)**: ``std::optional`` represents "possibly no value," and ``std::variant`` represents "one of several possible types." This is the first step toward type-safe error handling, but neither is specialized enough.

**C++23 (2023)**: ``std::expected<T, E>`` officially enters the standard, accompanied by monadic operations. This is the C++ committee's official endorsement of the "type-safe error handling" direction.

------

## Approach Comparison

I have put together a comparison table to view the characteristics of the four mainstream approaches side by side:

| Feature | Error Code/Enum | Exception | optional | expected |
|---------|-----------------|-----------|----------|----------|
| **Ignorability** | Easily ignored | Cannot be ignored (uncaught terminates) | Can be ignored | Can be ignored |
| **Error Information** | Limited (integer/enum) | Rich (exception object) | None (only presence/absence) | Rich (custom E) |
| **Performance (Happy Path)** | Nearly zero overhead | Nearly zero overhead | Nearly zero overhead | Nearly zero overhead |
| **Performance (Failure Path)** | Zero overhead | Heavy (stack unwinding) | Zero overhead | Zero overhead |
| **Composability** | Poor (manual propagation) | Good (automatic propagation) | Moderate | Good (monadic operations) |
| **Code Bloat** | None | Potentially large | Minimal | Small |
| **Embedded Usability** | Fully usable | Typically disabled | Fully usable | Fully usable |
| **Compiler-Enforced Checking** | No | No | No | No |
| **Requires RTTI** | No | Yes | No | No |

A noteworthy fact is that in C++, the types provided by the standard library (such as ``expected`` and ``optional``) **are not enforced by the compiler by default, unlike Rust's ``Result<T, E>``**. Rust's ``#[must_use]`` attribute makes the compiler emit a warning when the caller ignores a ``Result``. C++'s ``[[nodiscard]]`` has similar functionality, but the standard library does not apply this attribute to these types (this is also a topic of community discussion; see [P2422R1](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2422r1.html)). However, you can add ``[[nodiscard]]`` to your return types in your own projects to achieve compiler-enforced checking.

------

## Special Considerations for Embedded Scenarios

In embedded development, the choice of error handling is often not a question of "which is better," but "which is usable."

**Disabling exceptions** is the most common constraint in embedded development. The default configuration of ARM compilers is typically ``-fno-exceptions -fno-rtti``, which means ``throw`` / ``catch`` simply will not compile. So if you are writing embedded code, ``optional``, ``variant``, and ``expected`` are basically your primary choices.

**Deterministic error handling** is another key requirement. In real-time systems, you cannot accept "non-deterministic error handling time"—the time taken by exception stack unwinding is unpredictable, which is unacceptable in hard real-time systems. Return value approaches (error codes, ``optional``, ``expected``) have deterministic execution times, making them more suitable for real-time scenarios.

**Memory overhead** also needs to be considered. ``std::expected<T, E>`` typically occupies ``sizeof(E)`` plus some alignment padding more space than ``T``. If ``E`` is a simple enum, the extra overhead is only a few bytes; if ``E`` contains a ``std::string``, it introduces heap allocation. On an MCU with only a few dozen KB of RAM, these overheads need to be carefully weighed.

**Practical recommendation**: For embedded projects, the strategy I recommend is to use lightweight error types (enums or small structs) combined with ``expected`` semantics, implement a simplified version of ``expected`` yourself (usable in C++17), or simply use a struct return approach. In extremely resource-constrained scenarios, you can even fall back to enum error codes—but you must cultivate the team discipline of "always checking return values."

------

## Summary

In this chapter, we reviewed the evolution of C++ error handling: from C's error codes, to C++ exceptions, to the type-safe approaches of C++17/23. Each approach has its reasons for existing; there is no silver bullet. In the next three articles, we will dive deep into using ``optional`` for error handling, the usage of ``std::expected<T, E>``, and a comprehensive selection guide to help you make the right decisions in your actual projects.

## References

- [cppreference: Error handling](https://en.cppreference.com/w/cpp/error)
- [P0786R1 - std::expected proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0786r1.html)
- [C++ Core Guidelines: Error handling](https://isocpp.org/wiki/faq/exceptions)
