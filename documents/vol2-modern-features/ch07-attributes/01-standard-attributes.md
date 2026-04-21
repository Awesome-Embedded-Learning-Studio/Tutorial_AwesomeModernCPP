---
title: "标准属性详解：让编译器成为你的代码审查员"
description: "C++11-17 标准属性的语义、用法与最佳实践"
chapter: 7
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 18
prerequisites:
  - "Chapter 1: RAII 深入理解"
related:
  - "C++20-23 新属性"
---

# 标准属性详解：让编译器成为你的代码审查员

笔者在写代码的时候，经常遇到几种令人头大的情况：调用了一个返回错误码的函数但忘了检查，编译器一声不吭就放过了；有个参数在某个编译配置下用不到，编译器满屏警告未使用变量；想标记某个 API 过时了，但只能靠文档或注释提醒调用方。C++11 开始引入的标准属性语法 `[[attribute]]` 就是来解决这些问题的——用标准化的方式给编译器传递额外信息，让它帮我们做静态检查。

> 一句话总结：**属性是给编译器的声明性提示，不改变程序的语义，但能帮助编译器发现错误或生成更好的代码。**

------

## 属性的基本语法

C++ 标准属性使用双中括号 `[[attr]]`。多个属性可以写在一起 `[[attr1, attr2]]`，也可以分开 `[[attr1]] [[attr2]]`，效果相同。属性可以放在很多位置——函数声明、变量声明、类声明、枚举声明、switch 的 case 语句等——具体取决于属性的类型。

在标准化属性之前，各个编译器各有各的语法：GCC/Clang 用 `__attribute__((xxx))`，MSVC 用 `__declspec(xxx)`。标准属性的优势是可移植——所有符合标准的编译器都必须支持。但标准也预留了命名空间前缀的机制，比如 `[[gnu::always_inline]]` 或 `[[clang::fallthrough]]`，让编译器扩展也能用统一语法表达。

```cpp
// 单个属性
[[nodiscard]] int check_status();

// 多个属性
[[nodiscard, deprecated("Use new_version()")]]
int old_function();

// 编译器扩展属性
[[gnu::always_inline]] inline void hot_path();
[[gnu::format(printf, 1, 2)]] void log_msg(const char* fmt, ...);
```

------

## [[nodiscard]]：忽略返回值就报警告

这恐怕是在系统编程中最有实用价值的属性。它告诉编译器：如果调用者忽略了这个函数的返回值，请发出警告。

### 基本用法

```cpp
[[nodiscard]] ErrorCode initialize_hardware() {
    if (!check_power_supply()) return ErrorCode::PowerFailure;
    if (!setup_clocks())       return ErrorCode::ClockError;
    return ErrorCode::Ok;
}

// 不检查返回值——编译器发出警告
initialize_hardware();

// 正确用法
if (initialize_hardware() != ErrorCode::Ok) {
    handle_error();
}
```

在系统开发中，硬件初始化、传感器读取、通信操作都可能失败。忽略返回值意味着你可能在系统已经出错的状态下继续运行，后果很难预料。`[[nodiscard]]` 把"应该检查但忘了"变成了编译器警告，而不是上线后才暴露的运行时 bug。

### C++20 的增强：自定义消息

C++20 允许为 `[[nodiscard]]` 添加自定义消息，让编译器在发出警告时显示更具体的说明：

```cpp
[[nodiscard("Must check: hardware initialization may fail")]]
ErrorCode init_board();
```

如果调用者写了 `init_board();` 不检查返回值，编译器会显示你写的消息，而不是一个笼统的 "ignoring return value" 警告。

### 应用在类型上

`[[nodiscard]]` 还可以放在类或枚举的定义上。这样所有返回该类型的函数都自动带有 nodiscard 语义：

```cpp
[[nodiscard]] enum class ErrorCode {
    Ok,
    InvalidParam,
    Timeout,
    HardwareError
};

// 任何返回 ErrorCode 的函数都会自动触发检查
ErrorCode read_sensor(uint8_t id);
read_sensor(5);  // 警告：忽略了返回值
```

### ⚠️ nodiscard 不是强制的

需要注意的是，`[[nodiscard]]` 产生的只是警告，不是错误。调用者仍然可以通过显式转换来绕过：

```cpp
(void)init_board();             // 显式转换，消除警告
static_cast<void>(init_board()); // 同上
```

这意味着团队规范中可能需要禁止这种写法。`[[nodiscard]]` 是"请检查"而不是"必须检查"——但它已经比没有好太多了。

------

## [[maybe_unused]]：消除"未使用"警告

这个属性告诉编译器：这个变量或参数可能不会被使用，请不要发出警告。

### 条件编译场景

最常见的用途是条件编译。某个参数在一种配置下会用，在另一种配置下不用：

```cpp
void sensor_task([[maybe_unused]] void* param) {
#ifdef USE_RTOS
    // RTOS 模式下使用 param
    auto* config = static_cast<TaskConfig*>(param);
    configure_sensor(config->port);
#else
    // 裸机模式下不用 param
    configure_sensor(kDefaultPort);
#endif
}
```

如果不用 `[[maybe_unused]]`，裸机编译时编译器会警告 `param` 未使用。以前的做法是在函数体内写 `(void)param;` 或者把参数名注释掉 `void* /*param*/`。`[[maybe_unused]]` 比 `(void)` 更语义化，比注释参数名更不容易出错。

### 结构化绑定中的未使用成员

当你只需要结构化绑定的部分成员时，其他成员可以标记 `[[maybe_unused]]`。不过更常见的做法是用下划线 `_` 作为"我不关心这个"的占位符：

```cpp
std::map<int, std::string> cache;

for (const auto& [key, value] : cache) {
    // 如果你只关心 value，不关心 key
}

// 或者用 _ 占位（C++20 引入）
// 但注意 _ 在全局命名空间可能有特殊含义
```

### 与传统方法的对比

以前消除未使用警告的几种方式各有缺点：`(void)param` 是一个运行时无操作的语句混在代码里，看起来像遗漏了什么；把参数名注释掉 `/*param*/` 在修改参数类型时容易忘记更新注释；编译器特定属性 `__attribute__((unused))` 不可移植。`[[maybe_unused]]` 是标准化的、语义明确的解决方案。

------

## [[deprecated]]：标记过时的 API

`[[deprecated]]` 让你以编译器警告的方式标记过时的函数、类或变量。C++14 开始就支持，并且可以附带自定义消息说明应该用什么替代。

### 基本用法

```cpp
[[deprecated("Use new_handler() instead")]]
void old_handler();

// 调用 old_handler() 会产生编译警告，附带你写的消息
old_handler();
// warning: 'old_handler' is deprecated: Use new_handler() instead
```

### 库版本迁移中的应用

在库的版本升级中，`[[deprecated]]` 是一个非常有用的工具。你可以标记旧 API 为 deprecated 而不是直接删除它们，让使用者有时间迁移：

```cpp
class SensorManager {
public:
    // 旧 API——仍然可用，但标记为过时
    [[deprecated("Use read_sensor_data() which returns more information")]]
    bool read_sensor(uint8_t id, uint16_t* value);

    // 新 API
    SensorData read_sensor_data(uint8_t id);
};

// 枚举值也可以标记为 deprecated
enum class SensorType {
    Temperature,
    Humidity,
    [[deprecated("Use Pressure instead")]]
    Barometer,   // 旧名称
    Pressure      // 新名称
};
```

这种"先标记 deprecated，下个大版本再删除"的做法，比直接删掉 API 友好得多。调用方在编译时就能看到警告，知道需要迁移。

### deprecated 的作用范围

`[[deprecated]]` 可以放在函数、类、枚举、枚举值、变量、模板特化、命名空间（C++17 起）等几乎任何实体上。这意味着你可以废弃整个类而不仅仅是单个函数：

```cpp
[[deprecated("Use NewSensorManager instead")]]
class OldSensorManager { /* ... */ };
```

------

## [[fallthrough]]：switch 贯穿是有意的

在 switch 语句中，如果一个 case 没有以 `break` 结尾，执行会"贯穿"到下一个 case。编译器会对这种情况发出警告，因为它可能是忘了写 `break`。但有些时候贯穿是故意的行为——`[[fallthrough]]` 就是用来告诉编译器"我是故意的，别警告了"。

### 基本用法

```cpp
void handle_event(uint8_t event) {
    switch (event) {
        case 0x01:
            toggle_led(LED1);
            [[fallthrough]];  // 明确表示有意贯穿

        case 0x02:
            toggle_led(LED2);
            break;

        case 0x03:
            toggle_led(LED3);
            break;

        default:
            handle_unknown(event);
            break;
    }
}
```

`[[fallthrough]]` 必须放在 case 的最后一条语句之后、下一个 case 标签之前，而且后面必须跟分号。如果你放在其他位置，编译器可能会忽略它或报错。

### 状态机中的典型场景

在状态机的实现中，多个状态共享某些处理逻辑时，fallthrough 是很自然的选择：

```cpp
enum class State { Idle, Initializing, Running, Paused, Error };

void handle_state(State current, Event ev) {
    switch (current) {
        case State::Idle:
            if (ev == Event::Start) {
                current = State::Initializing;
            }
            [[fallthrough]];  // Idle 和 Initializing 共享初始化逻辑

        case State::Initializing:
            init_hardware();
            current = init_ok() ? State::Running : State::Error;
            break;

        case State::Running:
            run_task();
            break;

        case State::Paused:
        case State::Error:
            // 两个状态共享处理逻辑，直接贯穿
            recover();
            break;
    }
}
```

注意最后一个例子：`Paused` 和 `Error` 之间没有 `[[fallthrough]]`——因为它们之间没有任何语句，编译器不会对空 case 发出警告。

------

## [[noreturn]]：函数不会返回

`[[noreturn]]` 标记那些永远不会返回到调用者的函数。这类函数要么调用 `std::terminate()` 或 `exit()`，要么进入无限循环，要么抛出异常。

```cpp
[[noreturn]] void fatal_error(const char* msg) {
    std::fprintf(stderr, "FATAL: %s\n", msg);
    std::abort();
}

[[noreturn]] void hang_forever() {
    while (true) {
        // 嵌入式中的安全停机模式
    }
}

void check_critical(bool ok) {
    if (!ok) {
        fatal_error("Critical check failed");
        // 编译器知道这里不会返回，后续代码不可达
    }
    // 编译器可以优化此分支，不需要考虑 fatal_error 返回的情况
    proceed();
}
```

`[[noreturn]]` 对编译器的价值在于优化：编译器知道 `fatal_error()` 之后不会有控制流回来，所以不需要为返回路径生成代码。此外，编译器还能基于此消除"函数可能没有返回值"的警告。

⚠️ 注意：如果你给一个实际上会返回的函数加了 `[[noreturn]]`，行为是未定义的。编译器可能不会报错，但生成的代码可能完全不符合预期。

------

## [[carries_dependency]]

这个属性是 C++11 引入的，用于 `std::memory_order_consume` 相关的内存序依赖链传播。在实际开发中极少使用——因为主流编译器（GCC、Clang）把 `memory_order_consume` 直接升级为 `memory_order_acquire` 了，这个属性几乎成了摆设。除非你在写 lock-free 数据结构并且需要精确控制依赖链传播，否则可以安全忽略它。

------

## 编译器扩展属性

标准属性之外，主流编译器还支持通过命名空间前缀使用编译器特定的属性。这些属性虽然不是标准的，但在特定平台上很有用：

```cpp
// GCC/Clang 扩展
[[gnu::always_inline]]           // 强制内联
[[gnu::hot]]                     // 标记为热点函数
[[gnu::cold]]                    // 标记为冷路径
[[gnu::format(printf, 1, 2)]]    // printf 格式检查
[[clang::fallthrough]]           // Clang 专用的 fallthrough

// MSVC 扩展
[[msvc::forceinline]]            // 强制内联
```

这些属性在跨平台代码中需要谨慎使用。如果必须用，建议通过宏定义来统一包装：

```cpp
#if defined(__GNUC__)
    #define FORCE_INLINE [[gnu::always_inline]]
#elif defined(_MSC_VER)
    #define FORCE_INLINE [[msvc::forceinline]]
#else
    #define FORCE_INLINE
#endif

FORCE_INLINE void hot_function();
```

------

## 属性位置的正确放置

属性放在不同的位置有不同的含义。放错了位置可能被编译器忽略，或者作用到错误的目标上：

```cpp
// 函数属性——放在返回类型之前或声明符之后
[[nodiscard]] int func();      // 正确
int func [[nodiscard]]();      // 也正确（但不太常见）

// 变量属性——放在变量名之前
[[maybe_unused]] int x;

// 类属性——放在 class 关键字之后
class [[deprecated]] OldClass {};

// 枚举属性——放在 enum 关键字之后
enum class [[deprecated]] OldEnum {};

// switch case 属性——放在 case 内最后一条语句之后
switch (x) {
    case 1:
        do_something();
        [[fallthrough]];     // 注意分号
    case 2:
        do_more();
        break;
}
```

如果不确定属性应该放在哪里，cppreference 是最可靠的参考。

------

## 小结

C++11 到 C++17 的标准属性为日常开发提供了实用的静态检查工具。`[[nodiscard]]` 强制检查返回值，`[[maybe_unused]]` 消除未使用警告，`[[deprecated]]` 标记过时 API，`[[fallthrough]]` 标记有意贯穿，`[[noreturn]]` 标记不返回函数。每个属性都解决了一个具体的工程问题——不是炫技，而是让编译器帮你做代码审查。

在团队开发中，建议为这些属性的使用建立统一规范：哪些函数必须加 `[[nodiscard]]`（比如所有返回错误码的函数），哪些场景适合用 `[[deprecated]]`（比如 API 版本迁移期间），何时使用编译器扩展属性。统一的规范比零散的个人习惯更有效。

下一章我们会看 C++20 和 C++23 新增的属性——`[[likely]]`/`[[unlikely]]`、`[[no_unique_address]]`、`[[assume]]` 等——它们更偏向性能优化，是"让编译器生成更好代码"的方向。

## 参考资源

- [cppreference: C++ attributes](https://en.cppreference.com/w/cpp/language/attributes)
- [cppreference: nodiscard](https://en.cppreference.com/w/cpp/language/attributes/nodiscard)
- [cppreference: maybe_unused](https://en.cppreference.com/w/cpp/language/attributes/maybe_unused)
- [cppreference: deprecated](https://en.cppreference.com/w/cpp/language/attributes/deprecated)
