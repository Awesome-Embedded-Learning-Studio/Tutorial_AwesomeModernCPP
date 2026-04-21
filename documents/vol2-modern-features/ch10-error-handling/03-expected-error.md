---
title: "std::expected<T, E>：类型安全的错误传播"
description: "C++23 的 expected 类型与 monadic 操作，实现优雅的错误传播链"
chapter: 10
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - expected
  - 类型安全
difficulty: intermediate
platform: host
cpp_standard: [23]
reading_time_minutes: 18
prerequisites:
  - "Chapter 10: 错误处理的演进"
  - "Chapter 10: optional 用于错误处理"
related:
  - "错误处理模式总结"
---

# std::expected<T, E>：类型安全的错误传播

上一篇我们聊了 `std::optional` 在错误处理中的应用，也指出了它的局限——不能携带错误信息。当你需要知道"为什么失败"的时候，`optional` 就力不从心了。C++23 引入的 `std::expected<T, E>` 正是为了填补这个空白：它既告诉你"有没有值"，也告诉你"没有值的原因是什么"。

如果你接触过 Rust，`expected` 的设计思路和 Rust 的 `Result<T, E>` 如出一辙——成功时持有值 `T`，失败时持有错误 `E`。区别在于 C++ 没有编译器强制的 `must_use` 检查和 `?` 操作符，所以我们需要靠 monadic 操作和编码纪律来弥补。

先说明一下：`std::expected` 是 C++23 的特性。如果你目前使用的是 C++17 或 C++20，文中会提供一个可用的简化实现；嵌入式场景下由于没有 RTTI 依赖，`expected` 同样可以正常使用。

------

## expected 的核心语义

`std::expected<T, E>` 是一个模板类，要么持有一个 `T` 类型的成功值，要么持有一个 `E` 类型的错误对象。它的接口设计借鉴了 `optional`——你可以用 `operator bool()` 或 `has_value()` 来检查是否成功，用 `value()` 获取值，用 `error()` 获取错误：

```cpp
#include <expected>
#include <string>
#include <iostream>

enum class ParseError {
    kEmptyInput,
    kInvalidCharacter,
    kOutOfRange,
};

std::expected<int, ParseError> parse_int(const std::string& s) {
    if (s.empty()) {
        return std::unexpected(ParseError::kEmptyInput);
    }

    try {
        std::size_t pos = 0;
        int value = std::stoi(s, &pos);
        if (pos != s.size()) {
            return std::unexpected(ParseError::kInvalidCharacter);
        }
        return value;
    } catch (...) {
        return std::unexpected(ParseError::kOutOfRange);
    }
}

int main() {
    auto r1 = parse_int("42");
    if (r1) {
        std::cout << "Value: " << r1.value() << "\n";  // 42
    }

    auto r2 = parse_int("abc");
    if (!r2) {
        std::cout << "Error: " << static_cast<int>(r2.error()) << "\n";
        // 输出 Error: 1（kInvalidCharacter）
    }
}
```

`std::unexpected(E)` 是一个辅助模板，专门用于构造 `expected` 的错误分支。它的作用类似于 `std::nullopt` 之于 `optional`——明确表达"这是一个错误"。

------

## 构造与访问

`expected` 的构造方式比较丰富。最基本的：直接用值构造表示成功，用 `std::unexpected` 构造表示失败：

```cpp
// 成功值构造
std::expected<int, std::string> success = 42;

// 错误构造
std::expected<int, std::string> failure =
    std::unexpected("something went wrong");

// 就地构造
std::expected<std::string, int> in_place_success{
    std::in_place, "hello"};
```

访问方面，`expected` 提供了和 `optional` 类似的接口，但增加了一个关键成员——`error()`：

```cpp
std::expected<int, std::string> result = 42;

// 检查
result.has_value();      // true
static_cast<bool>(result);  // true

// 访问值
result.value();          // 42，如果为空则抛出 std::bad_expected_access
*result;                 // 42，未定义行为检查（类似 optional 的 operator*）
result->some_member;     // 如果 T 是结构体

// 访问错误（仅在 !has_value() 时调用）
std::expected<int, std::string> err =
    std::unexpected("oops");
err.error();             // "oops"

// 安全默认值
result.value_or(0);      // 如果有值返回值，否则返回 0
```

`value()` 和 `operator*` 的区别在于：前者在 `expected` 处于错误状态时会抛出 `std::bad_expected_access<E>` 异常，后者是未定义行为。所以在"你确信有值"的路径上用 `*`，在"不太确定"的路径上用 `value()` 或者先检查 `has_value()`。

------

## monadic 操作

这是 `expected` 最强大的部分。C++23 的 `expected` 原生支持四个 monadic 操作，让你可以用链式调用的方式组织多个可能失败的操作，而不需要层层嵌套 `if/else`。

### and_then：链接可能失败的操作

`and_then` 接受一个函数 `f`，`f` 接受 `expected` 内部的值并返回一个新的 `expected`。如果当前 `expected` 处于错误状态，`f` 不会被调用，错误直接穿透到链尾：

```cpp
#include <expected>
#include <string>
#include <iostream>

std::expected<int, std::string> validate_positive(int value) {
    if (value > 0) return value;
    return std::unexpected("Value must be positive");
}

std::expected<double, std::string> safe_divide(int num, int denom) {
    if (denom == 0) {
        return std::unexpected("Division by zero");
    }
    return static_cast<double>(num) / denom;
}

int main() {
    std::string input = "42";

    auto result = parse_int(input)
        .and_then(validate_positive)
        .and_then([](int v) {
            return safe_divide(v, 2);
        });

    if (result) {
        std::cout << "Result: " << *result << "\n";  // 21.0
    } else {
        std::cout << "Error: " << result.error() << "\n";
    }
}
```

如果 `parse_int` 返回错误，后续的 `validate_positive` 和 `lambda` 都不会执行，错误直接出现在 `result.error()` 里。这就是"错误自动穿透"的含义。

### transform：对值做变换

`transform` 和 `and_then` 的区别在于，传入的函数返回普通值而不是 `expected`。`transform` 会自动把返回值包进一个新的 `expected`：

```cpp
auto result = parse_int("42")
    .transform([](int v) { return v * 2; })
    .transform([](int v) { return std::to_string(v); });
// result 的类型是 std::expected<std::string, ParseError>
```

这里第一个 `transform` 把 `int` 变成 `int`（翻倍），第二个把 `int` 变成 `std::string`。如果中间任何一步失败，后续 `transform` 不会执行。

`transform` 适合那些"本身不会失败"的变换操作。如果一个操作可能失败，用 `and_then`；如果一定成功，用 `transform`。

### or_else：处理错误

`or_else` 在 `expected` 处于错误状态时调用传入的函数，通常用于错误恢复、日志记录、或者错误增强：

```cpp
std::expected<int, std::string> try_cache(int key) {
    return std::unexpected("cache miss for " + std::to_string(key));
}

std::expected<int, std::string> try_database(int key) {
    return key * 100;  // 模拟从数据库获取
}

int main() {
    auto result = try_cache(42)
        .or_else([](const std::string& err) {
            std::cerr << "Cache failed: " << err << ", trying DB\n";
            return try_database(42);
        });

    // result 持有 4200
}
```

`or_else` 的函数必须返回相同类型的 `expected`。这意味着你可以在 `or_else` 里做错误恢复——如果备选操作成功，链的后续部分会继续执行成功路径。

### transform_error：变换错误类型

`transform_error` 允许你在错误穿透的过程中变换错误对象，而不影响成功路径。这在跨层错误传播时非常有用——底层可能用一种错误类型，上层需要另一种：

```cpp
struct AppError {
    int code;
    std::string message;
    std::string context;  // 额外的上下文信息
};

auto result = parse_int("abc")
    .transform_error([](ParseError e) -> AppError {
        return AppError{static_cast<int>(e),
                        "Parse error",
                        "in config file line 1"};
    });
// result 的类型是 std::expected<int, AppError>
```

### 完整链式示例

把四个操作组合起来，就是一个完整的错误处理管道：

```cpp
#include <expected>
#include <string>
#include <iostream>
#include <charconv>
#include <system_error>

enum class ConfigError {
    kFileNotFound,
    kParseError,
    kValidationError,
};

struct ServerConfig {
    std::string host;
    int port;
};

std::expected<std::string, ConfigError> read_file(
    const std::string& path) {
    // 简化：假设总是成功
    return "host=192.168.1.1\nport=8080\n";
}

std::expected<ServerConfig, ConfigError> parse_config(
    const std::string& content) {
    ServerConfig cfg;
    cfg.host = "localhost";
    cfg.port = 8080;
    // 简化：实际解析内容
    return cfg;
}

std::expected<ServerConfig, ConfigError> validate_config(
    ServerConfig cfg) {
    if (cfg.port < 1 || cfg.port > 65535) {
        return std::unexpected(ConfigError::kValidationError);
    }
    return cfg;
}

int main() {
    auto result = read_file("server.cfg")
        .and_then(parse_config)
        .and_then(validate_config)
        .transform([](const ServerConfig& cfg) -> std::string {
            return cfg.host + ":" + std::to_string(cfg.port);
        })
        .transform_error([](ConfigError e) -> std::string {
            switch (e) {
                case ConfigError::kFileNotFound:
                    return "Config file not found";
                case ConfigError::kParseError:
                    return "Config parse error";
                case ConfigError::kValidationError:
                    return "Config validation failed";
            }
            return "Unknown error";
        });

    if (result) {
        std::cout << "Server: " << *result << "\n";
    } else {
        std::cerr << "Failed: " << result.error() << "\n";
    }
}
```

这条链读起来非常清晰：读文件 -> 解析配置 -> 校验配置 -> 转换为连接字符串。任何一步失败，后续步骤自动跳过，错误信息在链尾统一处理。

------

## expected vs 异常 vs optional

笔者整理了一个对比表，帮你在实际场景中做出选择：

| 场景 | 推荐方案 | 原因 |
|------|---------|------|
| 查找/缓存，失败无原因 | `optional` | 简洁，不需要错误信息 |
| 解析/IO，需要知道失败原因 | `expected` | 携带错误信息 |
| 多步操作链，需要错误传播 | `expected` | monadic 操作支持链式 |
| 不可恢复的严重错误 | 异常 | 强制中断，RAII 自动清理 |
| 构造函数失败 | 异常 | 构造函数没有返回值 |
| 嵌入式（无异常支持） | `expected` 或枚举 | 不依赖 RTTI |

一个实用的判断方法是：**如果调用方需要根据错误类型做不同的事情（重试、降级、报告），用 `expected`；如果只需要知道"成功或失败"，用 `optional`；如果是程序逻辑层面的严重错误（不可能恢复），用异常。**

------

## C++17 环境下的简化实现

如果你的项目还在 C++17，不用担心，可以实现一个功能完备的简化版 `expected`。下面这个实现覆盖了核心功能，可以直接用在项目中：

```cpp
#include <utility>
#include <type_traits>
#include <stdexcept>

/// 辅助类型：用于构造错误分支
template <typename E>
struct unexpected {
    E value;
    constexpr explicit unexpected(E v) : value(std::move(v)) {}
};

/// 简化版 expected<T, E>
template <typename T, typename E>
class expected {
    bool has_value_;
    union {
        T val_;
        E err_;
    } storage_;

public:
    // 成功值构造
    expected(const T& v) : has_value_(true) {
        new(&storage_.val_) T(v);
    }

    expected(T&& v) : has_value_(true) {
        new(&storage_.val_) T(std::move(v));
    }

    // 错误构造
    expected(unexpected<E> u) : has_value_(false) {
        new(&storage_.err_) E(std::move(u.value));
    }

    // 析构
    ~expected() {
        if (has_value_) storage_.val_.~T();
        else storage_.err_.~E();
    }

    constexpr bool has_value() const noexcept { return has_value_; }
    constexpr explicit operator bool() const noexcept {
        return has_value_;
    }

    T& value() {
        if (!has_value_)
            throw std::runtime_error("bad expected access");
        return storage_.val_;
    }

    const T& value() const {
        if (!has_value_)
            throw std::runtime_error("bad expected access");
        return storage_.val_;
    }

    const E& error() const {
        if (has_value_)
            throw std::runtime_error("no error present");
        return storage_.err_;
    }

    T& operator*() { return storage_.val_; }
    T* operator->() { return &storage_.val_; }

    T value_or(T default_val) const {
        return has_value_ ? storage_.val_ : default_val;
    }

    /// and_then：链接返回 expected 的操作
    template <typename F>
    auto and_then(F&& f) -> decltype(f(std::declval<T>())) {
        using ResultType = decltype(f(std::declval<T>()));
        if (has_value_) return f(storage_.val_);
        return ResultType(unexpected<E>{storage_.err_});
    }

    /// transform：对值做变换
    template <typename F>
    auto transform(F&& f)
        -> expected<decltype(f(std::declval<T>())), E> {
        using U = decltype(f(std::declval<T>()));
        if (has_value_)
            return expected<U, E>(f(storage_.val_));
        return expected<U, E>(unexpected<E>{storage_.err_});
    }

    /// or_else：处理错误
    template <typename F>
    expected or_else(F&& f) {
        if (has_value_) return *this;
        return f(storage_.err_);
    }
};
```

这个实现省略了一些细节（拷贝/移动语义的细粒度控制、`constexpr` 支持等），但核心语义完全正确，可以用于生产环境的错误处理。

------

## 通用示例：多层解析链

来看一个更贴近实际开发的例子——从字符串解析出网络地址，涉及多步验证和转换：

```cpp
#include <string>
#include <string_view>
#include <expected>
#include <iostream>
#include <charconv>

struct AddressError {
    enum Code {
        kEmptyInput,
        kMissingPort,
        kInvalidHost,
        kInvalidPort,
        kPortOutOfRange,
    } code;
    std::string detail;
};

struct NetworkAddress {
    std::string host;
    int port;
};

std::expected<std::string, AddressError> validate_input(
    std::string_view input) {
    if (input.empty()) {
        return std::unexpected(AddressError{
            AddressError::kEmptyInput, "Input is empty"});
    }
    return std::string(input);
}

std::expected<NetworkAddress, AddressError> split_address(
    std::string input) {
    auto colon = input.rfind(':');
    if (colon == std::string::npos) {
        return std::unexpected(AddressError{
            AddressError::kMissingPort,
            "No port specified: " + input});
    }

    NetworkAddress addr;
    addr.host = input.substr(0, colon);
    if (addr.host.empty()) {
        return std::unexpected(AddressError{
            AddressError::kInvalidHost, "Host is empty"});
    }

    auto port_str = input.substr(colon + 1);
    int port = 0;
    auto [ptr, ec] = std::from_chars(
        port_str.data(), port_str.data() + port_str.size(), port);
    if (ec != std::errc{} || ptr != port_str.data() + port_str.size()) {
        return std::unexpected(AddressError{
            AddressError::kInvalidPort,
            "Port is not a number: " + std::string(port_str)});
    }
    if (port < 1 || port > 65535) {
        return std::unexpected(AddressError{
            AddressError::kPortOutOfRange,
            "Port out of range: " + std::to_string(port)});
    }
    addr.port = port;
    return addr;
}

int main() {
    auto result = validate_input("192.168.1.1:8080")
        .and_then(split_address)
        .transform([](const NetworkAddress& a) -> std::string {
            return a.host + ":" + std::to_string(a.port);
        })
        .or_else([](const AddressError& e) -> std::expected<std::string, AddressError> {
            std::cerr << "Error: " << e.detail << "\n";
            return std::unexpected(e);
        });

    if (result) {
        std::cout << "Address: " << *result << "\n";
    }
}
```

这个例子展示了 `expected` 在多层操作中的优势：每一步都返回 `expected`，任何一步失败都会自动穿透，最终在链尾统一处理。错误信息携带了足够多的上下文——`detail` 字段告诉你具体出了什么问题。

------

## 小结

`std::expected<T, E>` 是 C++23 在类型安全错误处理方面的核心工具。它比 `optional` 多了错误信息，比异常更适合性能敏感和嵌入式场景，monadic 操作让错误传播链变得优雅。如果你还在 C++17，一个简化版的 `expected` 实现就能覆盖大部分需求。

下一篇我们会综合对比所有错误处理方案，给出一个场景化的选择指南。

## 参考资源

- [cppreference: std::expected](https://en.cppreference.com/w/cpp/utility/expected)
- [P2505R5 - Monadic Functions for std::expected](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2505r1.html)
- [C++ Stories: std::expected monadic extensions](https://www.cppstories.com/2024/expected-cpp23-monadic/)
