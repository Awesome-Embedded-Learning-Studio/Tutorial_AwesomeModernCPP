---
title: "错误处理的演进：从错误码到类型安全"
description: "错误码、异常、optional、expected——错误处理方案的演进与选择"
chapter: 10
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - 类型安全
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20, 23]
reading_time_minutes: 18
prerequisites:
  - "Chapter 4: std::optional"
  - "Chapter 4: std::variant"
related:
  - "optional 用于错误处理"
  - "std::expected"
---

# 错误处理的演进：从错误码到类型安全

笔者写 C++ 这些年，感受最深的一件事就是：**错误处理永远是项目中最难做好的部分**。不是因为它复杂——恰恰是因为它看起来太简单了。很多人觉得 `if (ret != 0)` 或者 `try { ... } catch (...)` 就够了，但真正到了维护阶段，才发现到处都是没处理的错误、被吞掉的异常、以及根本不知道为什么失败的函数调用。

这一章我们就来好好梳理一下 C++ 错误处理的演进历程：从 C 风格的错误码到 C++ 异常，再到 C++17 的 `optional` / `variant`，最后到 C++23 的 `expected`。弄清楚每种方案解决了什么问题、又引入了什么新问题之后，我们才能在面对一个具体场景时，做出合理的选择。

------

## 起点：C 风格错误码

如果你写过 C，或者维护过大型 C 遗留项目，下面这段代码一定不会陌生：

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

这种写法的问题不是"能不能用"，而是**用它写出来的代码能不能可靠地运行**。

第一个问题是**可忽略性**。错误码是一个普通的 `int`，调用方完全可以不检查返回值，编译器不会给出任何警告。笔者见过太多这样的代码：函数返回了错误码，调用方直接无视，继续往下执行，最后程序以一种诡异的方式崩溃——而错误发生的地方和崩溃的地方可能隔了十几个函数调用。

第二个问题是**信息匮乏**。一个 `-1` 能告诉你什么？文件不存在？权限不够？磁盘满了？你得去看文档或者头文件里的宏定义，然后祈祷这个函数的文档是最新版的。更糟糕的是，不同模块可能用相同的整数表示不同的含义，`-1` 在 A 模块里是"文件不存在"，在 B 模块里可能就是"超时"。

第三个问题是**全局状态依赖**。C 标准库经典的 `errno` 机制就是一个例子——它是全局变量，如果你在两个函数调用之间忘了保存 `errno`，它的值就被覆盖了。在多线程环境下这更是灾难，虽然现代实现用了 thread-local storage，但心智负担依然不小。

第四个问题是**资源泄漏风险**。上面的 `read_config` 只有一步操作，所以 `fclose` 的位置还算清晰。但如果你有五个可能失败的步骤，每一步都要在退出前正确清理前面分配的资源——`goto cleanup` 模式就是这么来的，虽然管用，但代码读起来像面条。

------

## 第二阶段：C++ 异常机制

C++ 引入了异常机制，试图解决错误码的核心痛点——让错误处理和控制流分离，让"正常路径"的代码不被错误检查打断：

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

异常解决了很多问题：正常路径的代码变得清晰，错误不会被悄悄忽略（未捕获的异常会终止程序），RAII 配合栈展开可以自动清理资源。在应用层开发中，异常是一个相当好用的工具。

但异常也有它的问题，而且有些问题在特定场景下是致命的。

首当其冲的是**性能的不确定性**。异常在"快乐路径"（即不抛异常时）的性能开销几乎为零——这是零成本抽象的设计目标。但一旦抛出异常，栈展开的开销是巨大的，涉及栈帧遍历、析构函数调用、异常对象拷贝等。对于"偶尔出错"的场景这不是问题，但如果你的网络服务每秒处理 10 万个请求，其中 5% 会失败，用异常来处理这些"预期中的失败"就不太合适了。

其次是**控制流不透明**。看上面 `init_system` 的代码，你能一眼看出 `read_config` 和 `apply_config` 分别可能抛什么异常吗？大概率不行，除非你仔细读文档或者函数实现。C++ 异常是"不可见"的——函数签名里不标注它可能抛什么异常（`throw()` 规范在 C++17 中已被移除，`noexcept` 只表示"不会抛"）。

第三，也是最关键的——**嵌入式环境通常禁用异常**。异常机制需要运行时支持（栈展开信息、RTTI 等），这些都会增加二进制体积。在很多嵌入式平台上，`-fno-exceptions` 是默认选项，这意味着你根本不能用 `throw` / `catch`。GNU ARM 工具链生成的带异常支持的代码比不带异常的代码大 50KB 到 200KB 不等，在 Flash 只有 64KB 的 MCU 上，这个开销是致命的。

最后是**异常安全**的复杂度。写异常安全的代码需要深入理解 RAII、强异常保证、基本异常保证等概念。一个构造函数里抛异常，对象可能处于半构造状态；一个 `push_back` 抛异常，容器可能处于半修改状态。这不是异常机制的锅，但确实增加了心智负担。

------

## 第三阶段：错误码 + 枚举的改进

既然异常在某些场景下不可用，我们回到错误码的思路，但用 C++ 的类型系统来弥补它的不足：

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

用 `enum class` 而不是宏或裸 `int` 来表示错误码，已经是一个不小的进步——类型安全、命名空间隔离、IDE 补全友好。加上 `std::string` 附加信息，调用方终于能知道具体出了什么问题。

但核心问题依然存在：**编译器不会强制你检查返回值**。`ConfigResult` 仍然是一个普通的结构体，如果你不调用 `.ok()`，程序照样会继续跑下去，用未初始化的 `Config` 对象做后续操作。另外，`ConfigResult` 里的 `std::string` 意味着堆分配，在嵌入式环境下这可能不是你想要的。

------

## 第四阶段：类型安全的错误类型

C++17 引入了 `std::optional` 和 `std::variant`，C++23 引入了 `std::expected`，它们从类型系统的层面重新审视了错误处理。核心思想是：**让"可能失败"这一信息成为类型的一部分，编译器帮你检查，而不是靠程序员自觉**。

### std::optional：成功或无值

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

`optional` 适合表达"成功返回一个值，失败则无值"的简单场景。它的优势在于语义明确——`std::optional<User>` 一看就知道"这里可能没有值"，比返回 `nullptr` 或者错误码要清晰得多。

但 `optional` 无法携带错误原因。`find_user` 返回 `nullopt` 时，你只知道"没找到"，却不知道是因为 ID 不存在、数据库连接断了、还是权限不足。

### std::variant：多状态表达

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

`variant` 可以表达多种错误类型，比 `optional` 的表达力更强。但使用体验并不理想——每次访问都要 `std::visit` 或者 `std::holds_alternative` 加 `std::get`，代码写起来比较啰嗦，而且错误类型和成功类型混在同一个 `variant` 里，语义上不如"值或错误"那么直观。

### std::expected：值或错误

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

`expected<T, E>` 的语义非常直接：**成功则持有 `T` 类型的值，失败则持有 `E` 类型的错误**。它既有 `optional` 的简洁性，又能像 `variant` 一样携带错误信息。而且 C++23 的 `expected` 还自带 monadic 操作（`and_then`、`transform`、`or_else` 等），可以优雅地串联多个可能失败的操作——这一点我们会在后续文章中详细介绍。

------

## 演进时间线

让我们用一个时间线来总结 C++ 错误处理方案的演进：

**C 语言时代（1970s）**：错误码 + `errno`。简单粗暴，可忽略，信息少。

**C++98（1998）**：异常机制。优雅但重，需要 RTTI 支持，控制流不透明。

**C++11（2011）**：`std::error_code` 标准化，为错误码提供更规范的框架。`<system_error>` 头文件引入了跨平台的错误分类机制。

**C++17（2017）**：`std::optional` 表示"可能没有值"，`std::variant` 表示"多种可能类型"。这是类型安全错误处理的第一步，但都不够专精。

**C++23（2023）**：`std::expected<T, E>` 正式进入标准，并附带 monadic 操作。这是 C++ 标准委员会对"类型安全错误处理"路线的正式肯定。

------

## 方案对比

笔者整理了一个对比表，把四种主流方案的特性放在一起看：

| 特性 | 错误码/枚举 | 异常 | optional | expected |
|------|------------|------|----------|----------|
| **可忽略性** | 容易被忽略 | 不可忽略（未捕获终止） | 可忽略 | 可忽略 |
| **错误信息** | 有限（整数/枚举） | 丰富（异常对象） | 无（只有有无） | 丰富（自定义 E） |
| **性能（快乐路径）** | 几乎零开销 | 几乎零开销 | 几乎零开销 | 几乎零开销 |
| **性能（失败路径）** | 零开销 | 重（栈展开） | 零开销 | 零开销 |
| **可组合性** | 差（手动传播） | 好（自动传播） | 中等 | 好（monadic 操作） |
| **代码膨胀** | 无 | 可能较大 | 极小 | 小 |
| **嵌入式可用** | 完全可用 | 通常禁用 | 完全可用 | 完全可用 |
| **编译器强制检查** | 否 | 否 | 否 | 否 |
| **需要 RTTI** | 否 | 是 | 否 | 否 |

一个值得注意的事实是：在 C++ 中，**没有任何一种方案能像 Rust 的 `Result<T, E>` 那样被编译器强制检查**。Rust 的 `#[must_use]` 属性会让编译器在调用方忽略 `Result` 时发出警告；C++ 的 `[[nodiscard]]` 虽然有类似功能，但 `expected` 和 `optional` 在标准中并未标记为 `[[nodiscard]]`，所以这仍然依赖于程序员的自觉。不过，你可以在自己的项目里给返回类型加上 `[[nodiscard]]`，获得类似的效果。

------

## 嵌入式场景的特殊考量

嵌入式开发中，错误处理的选择往往不是"哪个更好"的问题，而是"哪个能用"的问题。

**禁用异常**是嵌入式开发中最常见的约束。ARM 编译器的默认配置通常就是 `-fno-exceptions -fno-rtti`，这意味着 `throw` / `catch` 根本无法编译通过。所以如果你在写嵌入式代码，`optional`、`variant`、`expected` 基本就是你的主力选择。

**确定性错误处理**是另一个关键需求。在实时系统中，你不能接受"错误处理的时间不确定"这件事——异常的栈展开时间是不可预测的，这在硬实时系统中是不可接受的。返回值方案（错误码、`optional`、`expected`）的执行时间是确定的，更适合实时场景。

**内存开销**也需要考虑。`std::expected<T, E>` 通常比 `T` 多占 `sizeof(E)` 加上一些对齐填充的空间。如果 `E` 是一个简单的枚举，额外开销只有几个字节；如果 `E` 包含 `std::string`，就会引入堆分配。在 RAM 只有几十 KB 的 MCU 上，这些开销需要仔细权衡。

**实际建议**：对于嵌入式项目，笔者推荐的策略是使用轻量级的错误类型（枚举或小型结构体）配合 `expected` 语义，自己实现一个简化版的 `expected`（C++17 可用），或者直接用返回结构体的方式。在资源极其受限的场景下，甚至可以回到枚举错误码——但要养成"必须检查返回值"的团队纪律。

------

## 小结

这一章我们回顾了 C++ 错误处理的演进：从 C 的错误码，到 C++ 的异常，再到 C++17/23 的类型安全方案。每种方案都有它存在的理由，没有银弹。接下来的三篇文章，我们会分别深入 `optional` 用于错误处理、`std::expected<T, E>` 的用法、以及一个综合的选择指南，帮你在实际项目中做出正确的决策。

## 参考资源

- [cppreference: Error handling](https://en.cppreference.com/w/cpp/error)
- [P0786R1 - std::expected proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0786r1.html)
- [C++ Core Guidelines: Error handling](https://isocpp.org/wiki/faq/exceptions)
