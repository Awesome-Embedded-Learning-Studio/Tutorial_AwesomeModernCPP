---
chapter: 9
cpp_standard:
- 11
- 14
- 17
- 20
description: 函数式错误处理
difficulty: intermediate
order: 6
platform: host
prerequisites:
- 'Chapter 8: 类型安全'
reading_time_minutes: 26
tags:
- cpp-modern
- host
- intermediate
title: 函数式错误处理模式
---
# 嵌入式C++教程——函数式错误处理模式

## 引言

写C++的这些年，我见过太多错误的错误处理方式。有人把`try-catch`当goto用，有人用`-1`表示所有错误（到底是哪个错误？），有人到处`assert`然后生产环境直接崩溃。问题的核心不是"要不要处理错误"，而是"如何优雅地处理错误"。

函数式编程给了我们一个很好的思路：**把错误当成值**。不再是异常那种控制流的突然跳转，而是像处理返回值一样处理错误——可以传递、可以转换、可以组合。

我们已经讲过`std::optional`和`std::expected`，这一章把它们串起来，看看如何在实际项目中构建一套函数式风格的错误处理体系。

> 一句话总结：**函数式错误处理把错误当成一等公民，通过组合器（combinator）模式让错误传播和转换变得可预测、可组合，比异常更安全，比错误码更清晰。**

------

## 传统错误处理的问题

让我们先看看几种常见的错误处理方式，以及它们的痛点：

### 1. 错误码的泥潭

```cpp
// 经典的C风格错误处理
int parse_config(const char* path, Config* out_config) {
    FILE* f = fopen(path, "r");
    if (!f) return ERR_FILE_NOT_FOUND;

    char buffer[1024];
    if (fread(buffer, 1, sizeof(buffer), f) == 0) {
        fclose(f);
        return ERR_READ_FAILED;
    }

    // ... 一堆if检查
    if (some_condition) {
        fclose(f);
        return ERR_INVALID_FORMAT;  // 又一个错误码
    }

    fclose(f);
    return 0;  // 成功
}

// 调用者要写一堆if检查
Config cfg;
int err = parse_config("config.txt", &cfg);
if (err != 0) {
    // 但到底是哪个错误？要查头文件里的宏定义
    handle_error(err);
    return err;
}

```

这种写法有几个问题：

- 错误码是整数，不知道它代表什么含义
- 调用者必须记得检查返回值（否则错误被吞掉）
- 每层都要手动向上传递错误

### 2. 异常的惊喜感

```cpp
// 用异常写起来很舒服，但...
Config load_config(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("file not found");  // 这里会抛

    // ... 一堆操作
    if (invalid) throw std::runtime_error("invalid format");  // 这里也会抛

    return parse_from_stream(f);  // 别人也可能抛
}

// 调用者不知道哪些地方会抛异常
void init_system() {
    auto cfg = load_config("config.txt");  // 这行可能抛吗？
    apply_config(cfg);  // 这行呢？
    // ... 更多代码
}

```

异常的问题：

- 控制流不透明——你不知道哪行会抛
- 性能不可预测（堆展开、栈展开）
- 嵌入式系统往往禁用异常

### 3. 混合方法的灾难

```cpp
// 有些函数返回错误码，有些抛异常，有些用optional
std::optional<Config> load_config(const std::string& path);  // optional
bool save_config(const Config& cfg, std::string* error);    // 错误码+输出参数
void apply_config(const Config& cfg);                        // 异常

```

这种API让调用者无所适从，每次都要查文档才知道这个函数怎么处理错误。

------

## 函数式错误处理的核心思想

函数式错误处理的核心是：**错误是类型系统的一部分，不是控制流的意外**。

让我们用之前讲过的工具来构建：

```cpp
#include <optional>
#include <expected>
#include <string>
#include <functional>

// 定义错误类型
enum class ErrorCode {
    FileNotFound,
    InvalidFormat,
    ChecksumMismatch,
    OutOfMemory,
};

// 带有额外信息的错误
struct Error {
    ErrorCode code;
    std::string message;

    // 方便构造
    static Error make(ErrorCode c, std::string msg) {
        return Error{c, std::move(msg)};
    }
};

// 操作成功返回T，失败返回Error
template<typename T>
using Result = std::expected<T, Error>;

// 不返回值的操作
using VoidResult = std::expected<void, Error>;

```

现在我们有了一个类型安全的错误表示。接下来看看怎么用。

------

## 基本模式：Result的传播

最简单的用法是直接检查：

```cpp
Result<int> parse_int(const std::string& s) {
    try {
        size_t pos = 0;
        int value = std::stoi(s, &pos);
        if (pos != s.length()) {
            return Error::make(ErrorCode::InvalidFormat,
                             "trailing characters: " + s.substr(pos));
        }
        return value;
    } catch (const std::exception& e) {
        return Error::make(ErrorCode::InvalidFormat, e.what());
    }
}

void test_parse_int() {
    auto r1 = parse_int("42");
    if (r1) {
        std::cout << "parsed: " << r1.value() << std::endl;
    } else {
        std::cout << "error: " << r1.error().message << std::endl;
    }

    auto r2 = parse_int("abc");
    if (!r2) {
        std::cout << "parse failed: " << r2.error().message << std::endl;
    }
}

```

但这只是基础，函数式风格的威力在于**组合**。

------

## 组合器模式：and_then与map

`and_then`和`map`是函数式错误处理的两个核心组合器。它们让你能像搭积木一样串联操作。

### map：成功时转换值

```cpp
// map: 如果Result有值，用函数转换它；如果是错误，错误直接穿透
template<typename T, typename F>
auto map(Result<T> result, F&& func) -> Result<decltype(func(result.value()))> {
    if (result) {
        return func(result.value());
    }
    return result.error();
}

// 使用：串联操作
Result<std::string> read_file(const std::string& path);
Result<int> parse_size(const std::string& content);

// 先读文件，再解析大小，错误会自动传播
auto size = map(read_file("config.txt"), parse_size);
// 如果read_file失败，直接返回错误
// 如果成功，把内容传给parse_size

```

### and_then：返回Result的函数链

`map`的问题是不能处理返回`Result`的函数。`and_then`就是为了解决这个问题：

```cpp
// and_then: 链接返回Result的操作
template<typename T, typename F>
auto and_then(Result<T> result, F&& func)
    -> decltype(func(result.value()))
{
    using ResultType = decltype(func(result.value()));
    if (result) {
        return func(result.value());
    }
    return ResultType(result.error());  // 错误穿透
}

// 使用示例
Result<json::Value> parse_json(const std::string& content);
Result<Config> validate_config(const json::Value& json);
Result<void> apply_config(const Config& cfg);

// 链式调用：错误自动传播，成功时继续执行
Result<void> load_and_apply(const std::string& path) {
    return and_then(
        and_then(
            and_then(
                read_file(path),              // Result<string>
                parse_json                    // Result<json::Value>
            ),
            validate_config                   // Result<Config>
        ),
        apply_config                          // Result<void>
    );
}

```

看到这个嵌套的`and_then`了吗？有点丑，所以C++23的`std::expected`直接提供了成员函数版本：

```cpp
// C++23风格的链式调用
result.map(transform).and_then(validate).map(finalize);

```

如果你用的是C++17，可以给`expected`加上这些方法（参考第8章第5节的实现）。

------

## 实战：配置加载系统

让我们用一个完整的例子展示函数式错误处理的威力。这个系统要从文件加载配置，解析JSON，验证，然后应用。

```cpp
#include <expected>
#include <string>
#include <fstream>
#include <sstream>

// 错误定义
struct ConfigError {
    enum Code { FileNotFound, ParseError, ValidationError, ApplyError };
    Code code;
    std::string message;

    static ConfigError file_not_found(const std::string& path) {
        return {FileNotFound, "File not found: " + path};
    }

    static ConfigError parse_error(const std::string& detail) {
        return {ParseError, "Parse error: " + detail};
    }

    static ConfigError validation_error(const std::string& field) {
        return {ValidationError, "Validation failed for field: " + field};
    }
};

template<typename T>
using Result = std::expected<T, ConfigError>;

// ========== 各个操作 ==========

// 1. 读文件
Result<std::string> read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        return ConfigError::file_not_found(path);
    }

    std::stringstream buffer;
    buffer << f.rdbuf();
    return buffer.str();
}

// 2. 解析JSON（简化版）
struct JsonValue {
    std::string raw;
};

Result<JsonValue> parse_json(const std::string& content) {
    if (content.empty() || content[0] != '{') {
        return ConfigError::parse_error("not a JSON object");
    }
    return JsonValue{content};
}

// 3. 验证配置
struct Config {
    int baudrate = 115200;
    int timeout = 1000;
};

Result<Config> validate_config(const JsonValue& json) {
    Config cfg;
    // 简化：实际会解析JSON内容
    if (json.raw.find("baudrate") == std::string::npos) {
        return ConfigError::validation_error("baudrate");
    }
    return cfg;
}

// 4. 应用配置
Result<void> apply_config(const Config& cfg) {
    // 实际会写入硬件寄存器
    std::cout << "Applied config: baudrate=" << cfg.baudrate
              << ", timeout=" << cfg.timeout << std::endl;
    return {};  // void expected的成功值
}

// ========== 链式组装 ==========

Result<void> load_config(const std::string& path) {
    // 手动版本的and_then链
    auto content_result = read_file(path);
    if (!content_result) return content_result.error();

    auto json_result = parse_json(content_result.value());
    if (!json_result) return json_result.error();

    auto config_result = validate_config(json_result.value());
    if (!config_result) return config_result.error();

    return apply_config(config_result.value());
}

// 或者用我们之前写的and_then简化
Result<void> load_config_chain(const std::string& path) {
    return and_then(
        and_then(
            and_then(
                read_file(path),
                parse_json
            ),
            validate_config
        ),
        apply_config
    );
}

```

这个例子里，每个操作只关心自己的事情，错误的传播是自动的。你不需要在每个中间步骤写`if (error) return error;`。

------

## 宏辅助：简化链式调用

说实话，嵌套的`and_then`写起来确实有点烦。如果编译器支持C++23，你可以用新的monadic操作；如果暂时用不了，我们可以写个宏：

```cpp
#define TRY(...) ({ \
    auto _result = (__VA_ARGS__); \
    if (!_result) return _result.error(); \
    _result.value(); \
})

// 使用
Result<void> load_config_clean(const std::string& path) {
    auto content = TRY(read_file(path));
    auto json = TRY(parse_json(content));
    auto config = TRY(validate_config(json));
    return apply_config(config);
}

```

这个`TRY`宏在Rust里是内置语法（用`?`操作符），在C++里我们可以用宏模拟。它的作用是：如果表达式返回错误，直接把错误返回给上层；否则提取值继续执行。

**注意**：这是GCC/Clang的statement expression语法，MSVC可能需要调整。

------

## 错误的转换与增强

有时候底层函数返回的错误不够详细，你需要在传播过程中加上上下文。这可以用`map_error`实现：

```cpp
template<typename T, typename F>
Result<T> map_error(Result<T> result, F&& func) {
    if (!result) {
        return func(result.error());
    }
    return result;
}

// 使用：给错误加上上下文
Result<void> load_config_with_context(const std::string& path) {
    return and_then(
        read_file(path),
        [&](const std::string& content) {
            // 在解析失败时加上文件路径上下文
            return map_error(
                parse_json(content),
                [&](ConfigError err) {
                    err.message += " (in file: " + path + ")";
                    return err;
                }
            );
        }
    );
}

```

这样当解析失败时，错误信息会告诉你哪个文件出问题了，而不是仅仅说"JSON格式错误"。

------

## 多个错误的聚合

有时候你不想遇到第一个错误就返回，而是想收集所有错误一起报告：

```cpp
#include <vector>

template<typename T>
struct AggregateResult {
    std::optional<T> value;
    std::vector<ConfigError> errors;

    bool is_ok() const { return errors.empty(); }
};

// 批量验证：收集所有错误而不是遇到第一个就停
AggregateResult<Config> validate_config_batch(const JsonValue& json) {
    AggregateResult<Config> result;
    Config cfg;
    std::vector<ConfigError> errors;

    // 验证多个字段
    auto check_field = [&](const std::string& name, auto& out) {
        if (json.raw.find(name) == std::string::npos) {
            errors.push_back(ConfigError::validation_error(name));
        }
    };

    check_field("baudrate", cfg.baudrate);
    check_field("timeout", cfg.timeout);
    check_field("address", cfg.address);

    if (errors.empty()) {
        result.value = cfg;
    } else {
        result.errors = std::move(errors);
    }

    return result;
}

```

这在配置验证、表单验证等场景特别有用——用户可以一次性看到所有问题，而不是修一个错误再运行看下一个。

------

## 嵌入式实战：外设初始化链

嵌入式系统里，外设初始化往往是一长串操作，每步都可能失败。函数式错误处理能让这个流程变得清晰：

```cpp
#include <expected>

// 外设错误类型
struct PeripheralError {
    enum Code {
        ClockNotEnabled,
        GPIOInitFailed,
        PeripheralInitFailed,
        DMAConfigFailed,
    };
    Code code;
    const char* peripheral_name;

    static PeripheralError clock_failed(const char* name) {
        return {ClockNotEnabled, name};
    }

    // ... 其他工厂方法
};

template<typename T>
usingPeriphResult = std::expected<T, PeripheralError>;

// ========== 初始化步骤 ==========

PeriphResult<void> enable_clock(const char* peripheral_name) {
    // 实际会操作RCC寄存器
    std::cout << "Enabling clock for " << peripheral_name << std::endl;
    return {};  // 成功
}

PeriphResult<void> init_gpio(uint8_t pin, const char* mode) {
    std::cout << "Init GPIO " << (int)pin << " as " << mode << std::endl;
    return {};
}

PeriphResult<void> init_uart(uint32_t baudrate) {
    std::cout << "Init UART at " << baudrate << " baud" << std::endl;
    return {};
}

PeriphResult<void> init_dma_channel(uint8_t channel) {
    std::cout << "Init DMA channel " << (int)channel << std::endl;
    return {};
}

// ========== 完整初始化流程 ==========

PeriphResult<void> init_uart_system() {
    const char* uart_name = "USART1";

    // 方式1：手动链式
    auto r1 = enable_clock(uart_name);
    if (!r1) return r1.error();

    auto r2 = init_gpio(9, "alternate");
    if (!r2) return r2.error();

    auto r3 = init_gpio(10, "alternate");
    if (!r3) return r3.error();

    auto r4 = init_uart(115200);
    if (!r4) return r4.error();

    auto r5 = init_dma_channel(4);
    if (!r5) return r5.error();

    return {};

    // 方式2：用TRY宏
    // TRY(enable_clock(uart_name));
    // TRY(init_gpio(9, "alternate"));
    // TRY(init_gpio(10, "alternate"));
    // TRY(init_uart(115200));
    // return init_dma_channel(4);
}

```

对比传统写法，函数式风格的优势在于：

- 每一步的错误处理是一致的
- 不用维护全局的"错误状态变量"
- 流程是线性的，从上到下读一遍就知道做了什么

------

## 错误恢复与重试策略

函数式错误处理也方便实现重试逻辑：

```cpp
#include <chrono>
#include <thread>

// 重试包装器
template<typename F, typename Rep, typename Period>
auto retry_with_backoff(F&& func,
                        unsigned max_attempts,
                        std::chrono::duration<Rep, Period> initial_delay)
    -> decltype(func())
{
    using ResultType = decltype(func());

    auto delay = initial_delay;
    for (unsigned attempt = 0; attempt < max_attempts; ++attempt) {
        auto result = func();

        if (result) {
            return result;  // 成功，直接返回
        }

        // 最后一次尝试失败，不再等待
        if (attempt == max_attempts - 1) {
            return result;
        }

        // 等待后重试
        std::this_thread::sleep_for(delay);
        delay *= 2;  // 指数退避
    }

    return ResultType();  // 不应该到这里
}

// 使用：重试可能失败的网络操作
Result<std::string> fetch_http(const std::string& url) {
    // 实际的网络请求...
    if (rand() % 3 == 0) {  // 模拟随机失败
        return ConfigError{ConfigError::FileNotFound, "timeout"};
    }
    return "response body";
}

void test_retry() {
    auto result = retry_with_backoff(
        []() { return fetch_http("http://example.com"); },
        5,                          // 最多5次
        std::chrono::milliseconds(100)  // 初始延迟100ms
    );

    if (result) {
        std::cout << "Got response: " << result.value() << std::endl;
    } else {
        std::cout << "Failed after retries: " << result.error().message << std::endl;
    }
}

```

------

## 与C API的边界处理

嵌入式开发中经常要和C API打交道，它们通常用错误码或返回值表示错误。我们需要在边界处转换：

```cpp
// C API（假设）
extern "C" {
    typedef int32_t c_status_t;
    #define C_OK 0
    #define C_ERR (-1)

    c_status_t c_hal_init(void);
    c_status_t c_hal_send(const uint8_t* data, size_t len);
}

// C++包装层
struct HalError {
    enum Code { InitFailed, SendFailed, BusError };
    Code code;
    std::string detail;

    static HalError from_c_status(c_status_t status, const char* operation) {
        switch (status) {
            case C_OK: __builtin_unreachable();
            case C_ERR: return {SendFailed, operation};
            default: return {BusError, "unknown error"};
        }
    }
};

template<typename T>
using HalResult = std::expected<T, HalError>;

// 包装C API
HalResult<void> hal_init() {
    c_status_t status = c_hal_init();
    if (status != C_OK) {
        return HalError::from_c_status(status, "init");
    }
    return {};
}

HalResult<void> hal_send(const std::vector<uint8_t>& data) {
    c_status_t status = c_hal_send(data.data(), data.size());
    if (status != C_OK) {
        return HalError::from_c_status(status, "send");
    }
    return {};
}

// 现在可以用函数式风格组织代码
HalResult<void> send_packet(const std::vector<uint8_t>& payload) {
    TRY(hal_init());
    return hal_send(payload);
}

```

关键是在边界处做一次性转换，然后内部全用函数式风格。这样既保持了与C生态的兼容性，又让C++代码更加清晰。

------

## 性能考虑

函数式错误处理的性能开销主要在：

1. **额外的类型构造**：`expected<T, E>`比裸`T`多存一个错误
2. **间接调用**：组合器可能是lambda，带来额外调用
3. **代码体积**：每个`Result<T>`是不同类型，增加实例化

实测数据（简化测试）：

```cpp
// 错误码版本
int code_version(int x) {
    if (x < 0) return -1;
    if (x > 100) return -2;
    return x * 2;
}

// Result版本
Result<int> result_version(int x) {
    if (x < 0) return HalError{HalError::BusError, "negative"};
    if (x > 100) return HalError{HalError::BusError, "too large"};
    return x * 2;
}

```

在`-O2`优化下：

- **错误码版本**：~2条指令（检查+乘法）
- **Result版本**：~5条指令（构造expected+检查+乘法+解引用）

性能差距大约2-3倍。但这里的关键是：**如果你的函数本身有一定工作量，这个开销可以忽略不计**。在IO操作、硬件访问等场景，这点开销完全不可见。

只有在极热路径（比如信号处理、高频中断），才需要考虑用更轻量的错误码。

------

## 何时不用函数式错误处理

虽然函数式错误处理很优雅，但也不是万能的：

### 1. 超热路径

```cpp
// 1MHz采样率的ADC中断——用错误码
void __attribute__((interrupt)) ADC_IRQHandler() {
    int value = read_adc();
    if (value < 0) { error_count++; return; }  // 简单快速
    process_sample(value);
}

```

### 2. 与遗留代码集成

```cpp
// 如果整个项目都用错误码，你一个人Result反而增加复杂度
int legacy_function() {
    // 调用一堆返回int错误码的函数
}

```

### 3. 错误处理不需要上下文

```cpp
// 简单的true/false就够了
bool try_lock() {
    // 成功返回true，失败返回false
    // 不需要详细错误信息
}

```

------

## 小结

函数式错误处理是一种让代码更优雅、更可维护的方式：

- **错误是值**：类型安全，不能被忽略
- **可组合**：用`map`/`and_then`等组合器串联操作
- **可预测**：控制流是线性的，没有异常的突然跳转
- **上下文丰富**：错误信息可以逐层增强

在嵌入式开发中的建议：

| 场景 | 推荐方案 |
|------|---------|
| 应用层初始化 | Result/expected |
| 配置解析 | Result/expected |
| IO操作 | Result/expected |
| 硬件初始化 | Result/expected |
| 高频中断 | 错误码 |
| 极简操作 | bool/optional |

工具箱里放得下多种工具，但要知道什么时候用什么。函数式错误处理不是银弹，但在很多场景下，它确实比传统方式更清晰、更安全、更易维护。

到这里，我们已经讲完了现代C++中函数式相关的核心工具。Lambda、`std::function`、`std::invoke`、`optional`、`expected`、函数式错误处理——这些组合起来，能让你写出既高效又优雅的代码。嵌入式不代表必须用C风格，现代C++给我们的选择远比想象的多。
