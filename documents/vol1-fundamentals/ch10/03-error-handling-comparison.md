---
title: "错误处理方式对比"
description: "对比异常、错误码、optional 和 expected 的错误处理策略"
chapter: 10
order: 3
difficulty: intermediate
reading_time_minutes: 14
platform: host
prerequisites:
  - "异常安全"
tags:
  - cpp-modern
  - host
  - intermediate
  - 进阶
cpp_standard: [11, 14, 17, 20]
---

# 错误处理方式对比

C++ 这门语言给我们的错误处理工具，说实话，比大多数语言都要多。C 时代我们只有返回值和 `errno`；Java 和 C# 那边几乎全靠异常；Rust 给了 `Result<T, E>` 和 `?` 操作符。而 C++ 呢？它全都有。错误码、异常、`std::optional`、`std::expected`——工具箱里塞得满满当当。多并不是坏事，但如果我们不理解每种工具的设计意图和取舍，就很容易写出风格混乱的代码：同一个项目里，有的函数返回 `-1`，有的抛异常，有的返回 `std::nullopt`，调用者每次都要翻文档才能知道该怎么处理错误。

这一篇我们站在一个更高的视角，把 C++ 中几种主要的错误处理策略放在一起对比。我们的目标不是争论"哪种最好"——那种争论通常没有意义——而是搞清楚每种方式适合什么场景、不适合什么场景，以及在实际项目中如何做选择。先从最古老的错误码说起，一路走到 C++23 的 `std::expected`，最后给一个实用的决策指南。

## 从错误码说起：简单但不安全

错误码（error code）是 C 语言时代遗留下来的方案，也是所有 C++ 程序员最早接触的错误处理方式。原理非常直接：函数通过返回值告诉你它是成功还是失败，通常用 `0` 表示成功、负数表示错误，或者用一组 `#define` 或 `enum` 来区分不同的错误类型。

```cpp
int divide(int a, int b, int* result) {
    if (b == 0) {
        return -1;  // 错误码：除零
    }
    *result = a / b;
    return 0;       // 成功
}

// 调用
int quotient = 0;
if (divide(10, 3, &quotient) != 0) {
    // 处理错误
}
```

错误码的优势在于它的**可预测性**——控制流不会突然跳走，每一行代码都会按顺序执行，你可以在函数签名里一眼看出它可能返回哪些错误。而且它零额外开销：没有异常表、没有栈展开、不需要任何运行时支持。

但错误码有一个致命的问题：**调用者可以选择忽略它**。上面的 `divide` 函数返回一个 `int`，如果调用者压根不检查返回值，编译器不会报错，程序也照样跑——只是结果可能是错的。在一个大型项目里，遗漏对错误码的检查几乎是必然发生的事情。更要命的是，错误码只能传递"发生了什么错误"，无法携带丰富的上下文信息（比如文件路径、失败的参数值），除非你额外定义结构体或使用输出参数，那代码又会变得臃肿不堪。

> **踩坑预警**：如果你的函数返回错误码，但调用者没有检查，那错误就**被静默吞掉了**。这种 bug 极难追踪——程序不会崩溃，不会报错，只是默默给出了错误的结果。在嵌入式系统中，这种"沉默的错误"可能导致硬件行为异常，而且你完全不知道问题出在哪里。

## 异常：不可忽视但代价不小

C++ 的异常机制从语言层面解决了"错误被忽略"的问题。一个 `throw` 语句会打断正常的执行流程，沿着调用栈向上寻找匹配的 `catch` 块。如果你不 catch，程序就直接 `std::terminate`——你无法假装没看见。

```cpp
int divide(int a, int b) {
    if (b == 0) {
        throw std::invalid_argument("division by zero");
    }
    return a / b;
}

// 调用者必须处理，否则异常会继续传播
try {
    int result = divide(10, 0);
} catch (const std::invalid_argument& e) {
    std::cout << "Error: " << e.what() << "\n";
}
```

异常的强项在于它把"错误信息"和"控制流"绑定在一起——你不可能捕获了异常却不处理它。而且异常可以携带任意丰富的信息（通过 `std::exception` 的派生类），在深层调用的底层函数抛出异常后，顶层可以统一捕获处理，中间层完全不需要关心。

但异常也有几个不容忽视的问题。第一是**性能开销**：虽然"Happy path"（没有异常发生时）的开销在现代编译器上已经很小了（零成本模型），但一旦异常被抛出，栈展开的开销是相当可观的——需要逐帧析构局部对象、查找匹配的 catch 块。第二是**控制流不透明**：你光看函数签名，根本不知道它会不会抛异常、会抛什么异常。C++11 曾经引入过 `throw()` 和 `noexcept`，但 `throw(std::invalid_argument)` 这种动态异常规格在 C++17 中已经被移除了，现在只剩 `noexcept` 一个关键字——它只能告诉你"这个函数保证不抛异常"，对于"可能抛什么异常"则完全没有语言层面的约束。

第三，也是最实际的一个问题：**很多嵌入式工具链压根不支持异常**。GCC 和 Clang 的 `-fno-exceptions` 选项会完全禁用异常机制，一旦有 `throw` 语句，链接就会报错。在资源极其受限的单片机上，异常的代码体积开销（异常表、RTTI）往往不可接受。这就导致了一个割裂的现状：桌面和服务端的 C++ 大量使用异常，而嵌入式 C++ 基本不用——同一门语言，两套风格。

## std::optional：有还是没有

C++17 引入了 `std::optional<T>`，它表达的是一个很朴素的概念：这个值**可能存在，也可能不存在**。和错误码不同，`optional` 是类型系统的一部分——函数签名 `std::optional<int> divide(int a, int b)` 明确告诉你"返回值可能没有"，调用者必须面对这个事实。

```cpp
#include <optional>

std::optional<int> safe_divide(int a, int b) {
    if (b == 0) {
        return std::nullopt;  // 除零，返回空
    }
    return a / b;
}

// 调用
auto result = safe_divide(10, 0);
if (result.has_value()) {
    std::cout << "Result: " << result.value() << "\n";
} else {
    std::cout << "Division by zero!\n";
}
```

`std::optional` 的好处是**轻量且明确**。它在类型层面强制调用者处理"值不存在"的情况——如果你直接调用 `.value()` 而不检查 `has_value()`，在值为空时会抛出 `std::bad_optional_access`（对，它内部还是用了异常）。你也可以用 `*result` 来跳过检查直接访问，但如果值为空，那就是未定义行为。

`std::optional` 的问题是：它只能告诉你"失败了"，但**不能告诉你为什么失败**。除零是一种失败，溢出是另一种失败，参数非法又是第三种失败——但 `std::optional` 对这些情况一视同仁，全部返回 `std::nullopt`。如果你需要区分不同的错误类型，`optional` 就不够用了。

适合用 `optional` 的场景是：错误的种类只有一种（"没有找到"、"不存在"），而且调用者不需要知道具体原因。比如在容器里查找一个元素：`std::find_if` 没找到就返回 `end()`，但如果你的 API 设计成返回 `std::optional`，语义就很清晰——找到了就是值，没找到就是空，简单明了。

## std::expected：既要值也要原因

`std::expected<T, E>` 是 C++23 引入的类型，它结合了 `std::optional` 的类型安全性和异常的错误信息丰富性。简单来说，`expected<T, E>` 要么包含一个成功的值 `T`，要么包含一个错误 `E`——而且这个错误可以是任意类型，完全由你定义。

```cpp
#include <expected>
#include <string>

enum class DivideError {
    DivisionByZero,
    IntegerOverflow
};

std::expected<int, DivideError> checked_divide(int a, int b) {
    if (b == 0) {
        return std::unexpected(DivideError::DivisionByZero);
    }
    // 简化：暂不处理溢出
    return a / b;
}

// 调用
auto result = checked_divide(10, 0);
if (result.has_value()) {
    std::cout << "Result: " << result.value() << "\n";
} else {
    // 可以根据错误类型做不同处理
    switch (result.error()) {
        case DivideError::DivisionByZero:
            std::cout << "Cannot divide by zero!\n";
            break;
        case DivideError::IntegerOverflow:
            std::cout << "Integer overflow occurred!\n";
            break;
    }
}
```

`std::expected` 和 `std::optional` 最大的区别在于：当失败发生时，`expected` 能告诉你**为什么失败**。错误类型 `E` 可以是枚举、结构体、`std::string`——任何能携带足够信息的类型。这让调用者能够根据不同的错误类型采取不同的恢复策略，而不是面对一个空洞的"失败了"。

C++23 还为 `std::expected` 提供了一组 monadic 操作，让我们能够链式组合多个可能失败的操作：`and_then` 在成功时继续下一步、`transform` 在成功时转换值的类型、`or_else` 在失败时尝试恢复。这些操作在错误时会自动跳过后续步骤，直接传播错误值——和 Rust 的 `?` 操作符思路类似，只是语法上没那么简洁。

不过，`std::expected` 也有它的代价。在 C++23 标准正式落地之前，主流编译器的支持还不完善（GCC 12+、MSVC 19.34+ 支持基本功能，Clang 的支持相对滞后）。如果你的项目还在用 C++17 或更早的标准，可以用第三方库（比如 `tl::expected`）作为替代——接口基本一致，迁移成本很低。

> **踩坑预警**：`std::expected` 的 `value()` 方法在值为空时会抛出 `std::bad_expected_access<E>` 异常。如果你选 `expected` 的初衷就是"不用异常"，那切记要用 `has_value()` 先检查，或者用 `*` 解引用（值为空时是 UB，但不会抛异常）。混用 `expected` 和异常处理是一种很容易忽视的风格不一致。

## 四种策略的正面交锋

我们把四种错误处理方式的关键属性放在一起比较。下表是我们做选择时的核心参考：

| 特性 | 错误码 | 异常 | `std::optional` | `std::expected` |
|------|--------|------|------------------|------------------|
| 能否被忽略 | 能（这是最大问题） | 不能 | 能（但类型系统提醒你） | 能（但类型系统提醒你） |
| 携带错误信息 | 需要额外机制 | 天然支持 | 无（只有有/无） | 支持，错误类型自定义 |
| 性能开销 | 零 | 栈展开有开销 | 极小 | 极小 |
| 嵌入式可用性 | 完全可用 | 多数禁用 | 完全可用 | 完全可用（C++23） |
| 调用栈展开 | 无 | 有 | 无 | 无 |
| 标准要求 | C 语言即可 | C++（需启用） | C++17 | C++23 |

从这个表格里，我们能看到一个清晰的分野。异常和另外三种方式的本质区别在于**控制流模型**：异常是非局部的跳转，错误码 / `optional` / `expected` 都是局部的值传递。这个区别决定了它们各自的适用场景。

在实际项目中，我们的选择逻辑大致是这样的：如果项目允许使用异常（桌面/服务端应用），对于"不可恢复的、意外的"错误用异常，对于"可预期的、调用者需要处理的"错误用 `expected` 或 `optional`。如果项目禁用异常（嵌入式、游戏引擎、实时系统），那就只用错误码和 `optional` / `expected`，并且确保在所有错误路径上都有明确的处理逻辑。**最糟糕的情况是混用多种方式而没有统一的约定**——那会让整个代码库的错误处理变得一团糟。

## 实战：安全除法的三种写法

现在我们用一个完整的示例程序，把三种"不使用异常"的错误处理方式放在一起——同样的功能（安全的整数除法），分别用错误码、`std::optional` 和 `std::expected` 实现，然后在 `main` 里统一测试。

```cpp
// error_cmp.cpp
// 对比三种错误处理方式：错误码、optional、expected

#include <cstdio>
#include <optional>
#include <expected>
#include <string>

// ========== 方式一：错误码 ==========

constexpr int kErrDivisionByZero = -1;
constexpr int kErrSuccess = 0;

int divide_error_code(int a, int b, int* out) {
    if (b == 0) {
        return kErrDivisionByZero;
    }
    *out = a / b;
    return kErrSuccess;
}

// ========== 方式二：std::optional ==========

std::optional<int> divide_optional(int a, int b) {
    if (b == 0) {
        return std::nullopt;
    }
    return a / b;
}

// ========== 方式三：std::expected ==========

enum class MathError {
    DivisionByZero,
};

std::expected<int, MathError> divide_expected(int a, int b) {
    if (b == 0) {
        return std::unexpected(MathError::DivisionByZero);
    }
    return a / b;
}

// ========== 测试 ==========

int main() {
    struct TestCase {
        int a;
        int b;
        const char* label;
    };

    TestCase cases[] = {
        {10, 3,  "10 / 3"},
        {10, 0,  "10 / 0 (error)"},
        {7,  2,  "7 / 2"},
    };

    for (const auto& tc : cases) {
        std::printf("--- Test: %s ---\n", tc.label);

        // 错误码版本
        int result_code = 0;
        int err = divide_error_code(tc.a, tc.b, &result_code);
        if (err == kErrSuccess) {
            std::printf("  [ErrorCode]  result = %d\n", result_code);
        } else {
            std::printf("  [ErrorCode]  error: division by zero\n");
        }

        // optional 版本
        auto result_opt = divide_optional(tc.a, tc.b);
        if (result_opt.has_value()) {
            std::printf("  [Optional]   result = %d\n", result_opt.value());
        } else {
            std::printf("  [Optional]   error: no value\n");
        }

        // expected 版本
        auto result_exp = divide_expected(tc.a, tc.b);
        if (result_exp.has_value()) {
            std::printf("  [Expected]   result = %d\n", result_exp.value());
        } else {
            switch (result_exp.error()) {
                case MathError::DivisionByZero:
                    std::printf("  [Expected]   error: DivisionByZero\n");
                    break;
            }
        }
    }

    return 0;
}
```

编译运行：

```bash
g++ -std=c++23 -Wall -Wextra error_cmp.cpp -o error_cmp && ./error_cmp
```

如果你的编译器还不完全支持 `std::expected`，可以暂时把标准改为 C++20 并使用 `tl::expected` 头文件库替代。在 GCC 13+ 和 MSVC 19.34+ 上，上面的代码可以直接编译。

预期输出：

```text
--- Test: 10 / 3 ---
  [ErrorCode]  result = 3
  [Optional]   result = 3
  [Expected]   result = 3
--- Test: 10 / 0 (error) ---
  [ErrorCode]  error: division by zero
  [Optional]   error: no value
  [Expected]   error: DivisionByZero
--- Test: 7 / 2 ---
  [ErrorCode]  result = 3
  [Optional]   result = 3
  [Expected]   result = 3
```

三个测试用例、三种实现方式，结果完全一致——但"一致"只是表面现象。注意看 `10 / 0` 这个错误用例：错误码版本输出了一个字符串 `"division by zero"`，`optional` 版本只能说 `"no value"`，而 `expected` 版本给出了具体的 `DivisionByZero` 枚举值。在这么简单的例子中差异不大，但想象一下，如果函数有五种不同的失败模式，`optional` 就完全无能为力了——它没法告诉你到底是哪种失败。

> **踩坑预警**：上面这三种写法里，错误码版本的 `divide_error_code` 有一个很容易忽视的陷阱——如果调用者不检查返回值而直接使用 `result_code`，在错误路径下 `result_code` 的值是未初始化的（我们虽然用 `= 0` 初始化了，但那只是测试代码的写法；在真实代码中，输出参数经常被遗忘初始化）。`optional` 和 `expected` 在这方面更安全：你不检查 `has_value()` 就调用 `.value()` 会直接抛异常或导致 UB，至少不会让你拿着一个垃圾值继续往下跑。

## 练习

### 练习一：扩展错误类型

给上面的 `error_cmp.cpp` 添加一个 `IntegerOverflow` 错误类型。提示：`checked_divide` 中如果 `a == INT_MIN && b == -1`，在补码表示下会导致溢出（结果超出了 `int` 的范围）。在三种实现中分别处理这个额外的错误条件，并添加对应的测试用例。

### 练习二：文件读取的错误处理

假设你有一个函数 `std::string read_file(const std::string& path)`，它可能因为文件不存在、权限不足、读取超时三种原因失败。分别用 `std::optional` 和 `std::expected` 设计这个函数的接口（不需要实现具体逻辑，只需要设计签名和错误类型），比较两种方案的表达能力差异。

### 练习三：错误传播链

用 `std::expected` 实现一个简单的解析链：`read_file` -> `parse_config` -> `validate_config`，每个函数返回 `std::expected`，在 `main` 中写一个完整的调用链，确保任何一步失败都能正确传播到顶层并给出清晰的错误信息。

## 小结

到这里，我们把 C++ 中四种主流的错误处理方式——错误码、异常、`std::optional`、`std::expected`——完整地过了一遍。错误码最古老、最简单，但太容易被忽略；异常从语言层面保证了"错误不可忽视"，但代价是运行时开销和嵌入式场景的不可用；`std::optional` 轻量优雅，但只能表达"有没有"，无法传达"为什么没有"；`std::expected` 是目前最全面的方案，既有类型安全的值传递，又能携带丰富的错误信息，不过它需要 C++23 的支持。

选哪种方式没有绝对的对错，关键是要在项目级别保持一致。在允许异常的桌面和服务端项目中，异常处理"意外的、不可恢复的"错误，`expected` 处理"可预期的、需要恢复的"错误，`optional` 处理"没有找到、不存在"这类简单的缺失情况。在禁用异常的嵌入式项目中，错误码用于极简场景和高频路径，`optional` 和 `expected` 承担大部分错误处理职责。不管选哪种，**最重要的是整个团队对"什么时候用什么"达成共识**，而不是让每个人凭直觉选择。

第 10 章到这里就全部结束了。我们讨论了异常的基础机制、异常安全的四个等级和 RAII 守卫模式，以及今天这场错误处理策略的大对比。掌握了这些知识，我们就有了一个扎实的错误处理工具箱。接下来第 11 章，我们要进入一个全新的领域——标准模板库（STL）。从 `std::vector` 开始，我们会逐步认识 C++ 标准库提供的一系列强大容器和算法，它们能让我们少造很多轮子。
