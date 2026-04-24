---
title: "错误处理模式总结：选择指南与最佳实践"
description: "综合对比所有错误处理方案，提供场景化选择指南"
chapter: 10
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - 类型安全
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20, 23]
reading_time_minutes: 20
prerequisites:
  - "Chapter 10: 错误处理的演进"
  - "Chapter 10: optional 用于错误处理"
  - "Chapter 10: std::expected"
related:
  - "RAII 深入理解"
---

# 错误处理模式总结：选择指南与最佳实践

经过前三篇文章的铺垫，我们分别讨论了错误码、异常、`optional` 和 `expected` 的优缺点。这一篇是整个错误处理主题的收尾——笔者要把所有方案放在一起做一个综合对比，然后给出一个实用的选择指南，以及一些从踩坑中总结出来的最佳实践。

另外，这一篇还会补充一些前面没有展开的内容：函数式错误处理中常用的组合器模式、宏辅助的错误传播技巧、以及与 C API 边界处的错误转换策略。

------

## 综合对比

先把所有方案的关键指标放在一起。这个表很重要，建议收藏：

| 指标 | 枚举/错误码 | 异常 | optional | variant | expected |
|------|------------|------|----------|---------|----------|
| **携带错误信息** | 枚举值 | 丰富（异常对象） | 无 | 有限（持有哪种类型） | 丰富（自定义 E） |
| **可忽略性** | 容易忽略 | 不可忽略 | 可忽略 | 可忽略 | 可忽略 |
| **快乐路径开销** | 零 | 零 | 极小 | 小 | 小 |
| **失败路径开销** | 零 | 重 | 零 | 零 | 零 |
| **可组合性** | 差（手动传播） | 好（自动传播） | 好（C++23 monadic） | 差（visit 啰嗦） | 好（原生 monadic） |
| **控制流透明度** | 高（显式检查） | 低（不可见跳转） | 高 | 中 | 高 |
| **嵌入式可用** | 完全可用 | 通常禁用 | 完全可用 | 完全可用 | 完全可用 |
| **需要 RTTI** | 否 | 是 | 否 | 否 | 否 |
| **C++ 标准要求** | C++98 | C++98 | C++17 | C++17 | C++23 |

表格里的"可忽略性"值得多说一句。C++ 没有像 Rust 那样的 `#[must_use]` 编译器强制检查（虽然 C++17 有 `[[nodiscard]]`，但标准库并没有给 `optional` / `expected` 加上这个属性）。所以在 C++ 中，不管是错误码还是 `expected`，调用方都有可能不检查返回值——这需要靠代码审查和静态分析工具来弥补。

------

## 选择指南

笔者根据实际项目经验总结了一个决策流程，你可以根据具体场景来选择合适的方案。

### 决策树

**第一步：这个错误是"可恢复的"吗？**

如果错误意味着程序逻辑上有严重 bug（比如空指针解引用、数组越界），或者系统处于不可能恢复的状态（内存耗尽、栈溢出），那应该用 `assert` 或者直接终止程序。这类错误不应该用任何"返回值"方案来处理，因为调用方根本不可能做出合理的恢复动作。

**第二步：你运行在允许异常的环境中吗？**

如果环境允许异常（主机应用、服务器），而且错误发生频率很低（"异常"本来就是"不正常的情况"），异常是最好的选择——代码简洁、RAII 自动清理、不会忘记处理。嵌入式环境或者性能敏感的热路径通常禁用异常，这时候走第三步。

**第三步：调用方需要知道失败原因吗？**

如果不需要——比如查找操作只关心"有没有"，缓存只关心"命中没命中"——用 `optional`。简单、轻量、语义明确。

如果需要——比如文件操作需要区分"文件不存在"和"权限不足"，网络请求需要区分"超时"和"连接拒绝"——用 `expected`。

**第四步：你的编译器支持 C++23 吗？**

支持的话直接用 `std::expected<T, E>`，享受原生的 monadic 操作。还在 C++17 的话，用自己实现的简化版 `expected`，或者用枚举 + 结构体的方式。

### 场景化推荐

笔者按常见场景整理了一份推荐清单：

| 场景 | 推荐方案 | 理由 |
|------|---------|------|
| 查找/搜索 | `optional` | 只关心有没有，不需要原因 |
| 缓存命中 | `optional` | 同上 |
| 用户输入验证 | `expected` | 需要告诉用户哪里错了 |
| 配置文件解析 | `expected` | 需要区分"文件不存在"和"格式错误" |
| 网络 IO | `expected` | 需要区分超时、拒绝、DNS 失败等 |
| 文件 IO | `expected` | 需要区分不存在、权限、磁盘满等 |
| 数据库查询 | `expected` | 需要区分连接失败、语法错误、无结果等 |
| 构造函数失败 | 异常 | 构造函数没有返回值 |
| 不可恢复错误 | `assert` / 终止 | 不应该尝试恢复 |
| 高频中断/信号处理 | 错误码 | 极低开销，确定性执行时间 |
| 跨 C/C++ 边界 | 错误码 | C 不认识 C++ 类型 |

------

## 性能对比

性能是很多人关心的问题。笔者做一个简化的分析，帮助你在性能敏感场景下做决策。

`expected` 相比裸错误码，额外的开销主要来自两个方面：一是类型构造——`expected<T, E>` 需要存储一个标志位（成功/失败）和 `T` 或 `E` 的存储空间；二是移动/拷贝——在错误传播过程中，错误对象可能被多次移动。

在 `-O2` 优化级别下，这些开销大部分会被编译器内联和优化掉。一个返回 `expected<int, EnumError>` 的函数，优化后的汇编代码和返回 `int` 错误码的函数几乎没有区别——因为编译器可以把标志位优化成一个寄存器，把错误枚举值优化成另一个寄存器。

真正有性能差异的场景是 `expected<std::string, std::string>` 这种——值类型和错误类型都可能涉及堆分配。在这种情况下，每次传播都会移动 `std::string` 的内容。如果你的操作链很长（比如超过 5 步），建议用轻量级的错误类型（枚举、小型结构体、`std::string_view`）。

异常的性能模型完全不同。在"快乐路径"上，异常的开销接近于零（现代编译器使用 "zero-cost exception handling" 模型）。但在抛出异常时，栈展开的开销是巨大的——需要遍历栈帧、查找 catch 块、析构局部对象。这意味着异常不适合"预期中会频繁发生的失败"——如果你的 HTTP 服务有 10% 的请求会超时，用异常处理超时就是一个糟糕的选择。

------

## 函数式错误处理模式

函数式错误处理的核心思想是：**错误是值，不是控制流的意外**。通过组合器（combinator）模式，让错误的传播和转换变得可预测、可组合。

### TRY 宏：模拟 Rust 的 ? 操作符

C++ 没有内置的 `?` 操作符，但我们可以用宏来模拟。这个宏在函数式风格的错误处理中非常好用：

```cpp
/// TRY 宏：如果表达式返回错误，直接向上传播
/// 使用 GCC/Clang 的 statement expression 语法
#define TRY(expr)                                           \
    ({                                                      \
        auto _result = (expr);                              \
        if (!_result) return std::unexpected(_result.error()); \
        std::move(_result.value());                         \
    })

// 使用示例
std::expected<std::string, ConfigError> read_file(const std::string& path);
std::expected<Config, ConfigError> parse_config(const std::string& content);
std::expected<Config, ConfigError> validate_config(const Config& cfg);

std::expected<Config, ConfigError> load_config(const std::string& path) {
    auto content = TRY(read_file(path));
    auto config = TRY(parse_config(content));
    auto validated = TRY(validate_config(config));
    return validated;
}
```

对比不用宏的手动检查版本：

```cpp
std::expected<Config, ConfigError> load_config(const std::string& path) {
    auto content_result = read_file(path);
    if (!content_result) {
        return std::unexpected(content_result.error());
    }

    auto config_result = parse_config(content_result.value());
    if (!config_result) {
        return std::unexpected(config_result.error());
    }

    auto validated_result = validate_config(config_result.value());
    if (!validated_result) {
        return std::unexpected(validated_result.error());
    }

    return validated_result;
}
```

宏版本简洁得多，而且语义清晰——`TRY` 的意思就是"试试这一步，失败了就放弃"。但要注意，这个宏用了 GCC/Clang 的 statement expression 语法，MSVC 需要用其他方式实现。

对于不支持 statement expression 的编译器，可以用一个稍微啰嗦但可移植的版本：

```cpp
// 可移植版本：需要调用方声明变量
#define TRY_OUT(result, expr)            \
    auto result = (expr);                \
    if (!result) return std::unexpected(result.error())

// 使用
std::expected<Config, ConfigError> load_config(const std::string& path) {
    TRY_OUT(content, read_file(path));
    TRY_OUT(config, parse_config(content.value()));
    TRY_OUT(validated, validate_config(config.value()));
    return validated;
}
```

### 错误恢复与重试

函数式风格也方便实现重试逻辑。一个通用的重试包装器：

```cpp
#include <chrono>
#include <thread>

/// 带指数退避的重试包装器
template <typename F, typename Rep, typename Period>
auto retry(F&& func, unsigned max_attempts,
           std::chrono::duration<Rep, Period> initial_delay)
    -> decltype(func()) {
    using ResultType = decltype(func());
    auto delay = initial_delay;

    for (unsigned attempt = 0; attempt < max_attempts; ++attempt) {
        auto result = func();
        if (result) return result;

        if (attempt == max_attempts - 1) return result;

        std::this_thread::sleep_for(delay);
        delay *= 2;  // 指数退避
    }
    return ResultType();  // 不会到这里
}

// 使用
auto result = retry(
    []() { return fetch_url("https://example.com"); },
    3,                          // 最多 3 次
    std::chrono::milliseconds(100)  // 初始延迟 100ms
);
```

### 错误聚合

有时候你想收集所有错误一起报告，而不是遇到第一个就返回。比如表单验证——用户提交一个表单，多个字段可能同时有问题，一次性告诉用户比一个个修要好得多：

```cpp
#include <vector>
#include <string>
#include <iostream>

struct ValidationError {
    std::string field;
    std::string message;
};

struct ValidationReport {
    std::vector<ValidationError> errors;

    void add(std::string field, std::string message) {
        errors.push_back({std::move(field), std::move(message)});
    }

    bool ok() const { return errors.empty(); }

    void print() const {
        for (const auto& e : errors) {
            std::cerr << "  - " << e.field << ": " << e.message << "\n";
        }
    }
};

void validate_form(const std::string& name,
                   const std::string& email,
                   int age,
                   ValidationReport& report) {
    if (name.empty()) report.add("name", "Name cannot be empty");
    if (name.size() > 100) report.add("name", "Name too long");

    if (email.find('@') == std::string::npos) {
        report.add("email", "Invalid email format");
    }

    if (age < 0 || age > 200) report.add("age", "Age out of range");
}

int main() {
    ValidationReport report;
    validate_form("", "invalid", -1, report);

    if (!report.ok()) {
        std::cerr << "Validation failed:\n";
        report.print();
    }
}
```

------

## 与 C API 的边界处理

嵌入式开发中经常需要和 C API 打交道。C API 通常用整数错误码表示错误，而我们的 C++ 代码用 `expected`。在边界处做一次性转换，然后内部全用 C++ 风格：

```cpp
// 假设 C API 长这样
extern "C" {
    int hal_init(void);       // 返回 0 表示成功
    int hal_send(const uint8_t* data, int len);
    int hal_read(uint8_t* buffer, int len);
}

// C++ 包装层
enum class HalError {
    kInitFailed,
    kSendFailed,
    kReadFailed,
    kTimeout,
};

std::expected<void, HalError> wrapped_hal_init() {
    int ret = hal_init();
    if (ret != 0) return std::unexpected(HalError::kInitFailed);
    return {};
}

std::expected<void, HalError> wrapped_hal_send(
    const uint8_t* data, int len) {
    int ret = hal_send(data, len);
    if (ret != 0) return std::unexpected(HalError::kSendFailed);
    return {};
}

std::expected<int, HalError> wrapped_hal_read(
    uint8_t* buffer, int len) {
    int ret = hal_read(buffer, len);
    if (ret < 0) return std::unexpected(HalError::kReadFailed);
    return ret;  // 返回实际读取的字节数
}

// 现在可以用函数式风格组织
std::expected<void, HalError> send_command(const uint8_t* cmd, int len) {
    TRY_OUT(init_result, wrapped_hal_init());
    TRY_OUT(send_result, wrapped_hal_send(cmd, len));
    return {};
}
```

关键原则是：**在 C/C++ 边界做一次性转换，内部全用 C++ 风格**。这样既保持了与 C 生态的兼容性，又让 C++ 代码保持清晰。

------

## 最佳实践

最后是笔者在实际项目中总结的一些最佳实践，每一条都是从踩坑里学来的。

### 1. 选择一种方案并坚持一致

混合使用多种错误处理方式是代码混乱的最大根源。如果团队决定用 `expected`，就全部用 `expected`；如果决定用错误码，就全部用错误码。不要一个函数返回 `optional`，另一个抛异常，还有一个用输出参数——调用方每次都要查文档才能知道怎么处理错误。

### 2. 错误类型要轻量

`expected<T, E>` 里的 `E` 应该尽量轻量——枚举、小型结构体、或者 `std::string_view`。避免用 `std::string` 或者包含堆分配成员的结构体作为错误类型，因为在错误传播过程中，错误对象可能被多次拷贝或移动。如果你的错误类型需要携带复杂信息，考虑用错误码 + 错误消息查找表的方式。

### 3. 用 [[nodiscard]] 强制检查返回值

虽然标准库没有给 `optional` 和 `expected` 加 `[[nodiscard]]`，但你可以给自己定义的返回类型加上：

```cpp
struct [[nodiscard]] Result {
    ErrorCode error;
    std::string message;
    constexpr bool ok() const noexcept { return error == ErrorCode::kSuccess; }
};
```

这样如果调用方忽略了返回值，编译器会发出警告。虽然不如 Rust 的 `#[must_use]` 严格，但聊胜于无。

### 4. 不要在 expected 的 E 里存异常

`std::expected<T, std::exception_ptr>` 看起来很诱人——既能避免异常的开销，又能保留异常的丰富信息。但实际上这会让 `expected` 变得笨重，而且你需要在最终处理处重新抛出异常才能获取信息。更好的做法是定义一个轻量的错误类型。

### 5. 错误处理要有层次

底层函数用简单的错误类型（枚举），中间层在传播过程中增强错误信息（加上上下文），顶层做最终的日志记录和用户提示。这样底层保持通用性，顶层获得足够丰富的信息：

```cpp
// 底层：简单的枚举
std::expected<int, IoError> read_byte(int fd);

// 中间层：增强错误信息
std::expected<Config, AppError> load_config(const std::string& path) {
    auto byte = read_byte(fd)
        .transform_error([path](IoError e) -> AppError {
            return AppError{e, "while reading config: " + path};
        });
    // ...
}
```

### 6. 性能敏感的热路径用错误码

在高频中断处理、信号处理、实时采样等场景下，`expected` 的构造和移动开销（虽然很小）也可能不可接受。这些场景下，用最简单的错误码和全局错误状态，把性能压到极致。

### 7. 断言用于不可能发生的情况

`assert` 用于检查程序逻辑上的不变量——如果断言失败，说明代码有 bug。不要用 `assert` 检查外部输入（用户输入、文件内容、网络数据），因为外部输入是"可能出错"的，不是"不可能发生"的。前者用 `expected` / 错误码，后者用 `assert`。

------

## 小结

错误处理没有银弹。错误码简单粗暴，异常优雅但重，`optional` 轻量但没信息，`expected` 是目前最均衡的方案但需要 C++23（或自己实现）。选择方案时需要考虑环境约束（是否能用异常）、性能需求（是否有热路径）、以及团队偏好（是否统一风格）。

笔者建议的策略是：**默认用 `expected`，查找/缓存场景用 `optional`，构造函数和不可恢复错误用异常/终止，C API 边界做一次性转换**。工具箱里放得下多种工具，但要知道什么时候用什么。

到这里，ch10 错误处理就全部讲完了。下一篇我们进入 ch11，聊一聊用户自定义字面量——一种让代码更直观、更安全的有趣机制。

## 参考资源

- [cppreference: Error handling](https://en.cppreference.com/w/cpp/error)
- [C++ Core Guidelines: Error handling](https://isocpp.org/wiki/faq/exceptions)
- [P2505R5 - Monadic Functions for std::expected](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2505r1.html)
