# 嵌入式的现代C++教程——`constexpr` 与设计技巧

大伙都知道一个经典的技巧——如果一个配置项目是编译时就会确定的，一般都会做成宏的方式在编译器的预处理阶段替换掉。

这类事情，就牵扯到宏的一大堆问题上了，这里不再重复说明宏要做多么复杂的操作回避掉以外的事情了。现代C++出现了一个专门处理编译期计算的一个关键字constexpr，可以说是开发者手上的一把捷径。把可以预先算好的东西放到编译期，不仅能让运行时快上一截，还能把复杂逻辑的错误尽早暴露在编译器面前——这在资源受限、调试不便的嵌入式世界里，价值非常高。

------

## `constexpr` 的本质与演化

`constexpr` 的初衷是：标注一个函数或变量可以在编译期求值。随着标准演进，`constexpr` 的能力越来越强——从只能用于整型常量表达式（C++11）变成可以含有分支、循环、动态数组、虚拟调用受限（C++20 中更强）甚至 `consteval`（保证必须在编译期求值）。用一句更直接的话：**把“可预计算”的工作在编译期完成，最终生成更小、更快、更易验证的运行时代码。**

------

## 为什么嵌入式项目应该重视 `constexpr`？

在嵌入式开发中，我们常常遇到这些场景：

- 对复杂数学函数做采样，生成查表（sine/cos、滤波器系数、CRC 表等）。
- 协议中大量固定但需要计算的常量（位域掩码、偏移、校验初始值）。
- 配置模板化（不同目标板使用不同常量配置）——在编译期就生成正确数据，避免运行时分支和闪存占用的浪费。
- 想在编译阶段让错误暴露（例如非法参数、数组越界）而不是在设备上崩溃。

把这些工作交给编译器，可以减少运行时 RAM/CPU 占用，并且让编译器做静态检查（`static_assert`）——这是嵌入式最喜欢的“尽早失败”策略。

------

## 经典示例：`constexpr` 计算（阶乘、斐波那契、pow）

先从最基础的数学例子开始，便于理解编译期求值。

```cpp
// C++14 起，constexpr 函数可以包含循环
constexpr unsigned factorial(unsigned n) {
    unsigned r = 1;
    for (unsigned i = 2; i <= n; ++i) r *= i;
    return r;
}

static_assert(factorial(6) == 720, "compile-time check");

// C++11 版本通常用递归（注意递归深度）
constexpr unsigned factorial_rec(unsigned n) {
    return n <= 1 ? 1 : n * factorial_rec(n - 1);
}
```

上面 `factorial(6)` 在编译期被展开为常量并用于 `static_assert`。在嵌入式里，避免运行时乘法循环有时也会节省能耗和代码路径复杂度（例如引导代码、实时中断处理路径）。

另一个常用的是 `pow`，但在嵌入式我们常更愿意做整数幂的 `constexpr`：

```cpp
constexpr long ipow(long base, unsigned exp) {
    long r = 1;
    while (exp) {
        if (exp & 1) r *= base;
        base *= base;
        exp >>= 1;
    }
    return r;
}
static_assert(ipow(2, 10) == 1024);
```

需要注意的是：C++11 的 `constexpr` 函数受限，只允许单一 return 表达式；C++14 放宽了这一点，允许循环和局部变量，这使得许多算法可以以更自然的方式实现为 `constexpr`。

------

## Lookup table（查表）生成：把昂贵的计算移到编译期

查表是嵌入式常见优化。以下展示几种生成查表的模式。

#### 4.1 用 `constexpr` 生成数组（C++14/17 风格）

```cpp
#include <array>
#include <cstddef>

// 生成 N 个元素的查表
template <std::size_t N>
constexpr std::array<float, N> make_sin_table() {
    std::array<float, N> t{};
    for (std::size_t i = 0; i < N; ++i) {
        // 注意：编译期不能调用 std::sin 在部分编译器下不被识别
        // 这里用泰勒或近似函数，或在支持的环境下使用 constexpr math
        long double ang = (2.0L * 3.14159265358979323846L) * i / N;
        // 用简单的二阶近似：sin(x) ≈ x - x^3/6 (在 [-pi/4, pi/4] 以外误差增大)
        long double x = ang;
        long double x3 = x*x*x;
        t[i] = static_cast<float>(x - x3/6.0L);
    }
    return t;
}

constexpr auto sin16 = make_sin_table<256>();
```

上面示例的重点在于：你可以在编译期生成一个 `std::array`，并在运行时直接以只读数据使用。对于资源极限的 MCU，这样的表通常被放到 `.rodata`（flash），不占 RAM。

> 小提醒：C++ 标准库数学函数（`std::sin`）并不保证是 `constexpr`（直到最新标准/实现才开始支持），所以在编译期要么用近似公式，要么自己实现一个 `constexpr` 数学近似。

#### 4.2 CRC 表/位反转表（实战）

CRC 查表非常适合用 `constexpr` 生成，既保证一致性又便于测试。

```cpp
#include <array>
#include <cstdint>

constexpr std::array<uint32_t, 256> make_crc32_table(uint32_t poly = 0xEDB88320u) {
    std::array<uint32_t, 256> table{};
    for (std::size_t i = 0; i < 256; ++i) {
        uint32_t c = static_cast<uint32_t>(i);
        for (int j = 0; j < 8; ++j)
            c = c & 1 ? (poly ^ (c >> 1)) : (c >> 1);
        table[i] = c;
    }
    return table;
}

constexpr auto crc32_table = make_crc32_table();
```

然后在运行时代码中直接使用 `crc32_table`，无需在启动时生成表或包含外部二进制文件。

------

## 5. 编译期字符串处理：实现配置与静态解析

字符串常常是协议/命令/日志的核心。在运行时进行大量字符串解析会浪费 RAM 与 CPU。我们可以把静态字符串的解析提前到编译期，比如计算字符串哈希以实现 switch-like 的选择、或把配置字符串解析为整数常量。

#### 5.1 编译期字符串哈希（快速实现字符串 switch）

C++ 不允许 `switch` 直接使用字符串，但我们可以使用 `constexpr` 哈希在编译期把字符串映射成整型，然后 `switch` 分支。

```cpp
// constexpr FNV-1a 哈希（简单、广泛使用）
constexpr uint32_t fnv1a(const char* s, std::size_t n) {
    uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < n; ++i) {
        h ^= static_cast<uint32_t>(s[i]);
        h *= 16777619u;
    }
    return h;
}

// helper 模板把字面量字符串长度作为参数传入
template <std::size_t N>
constexpr uint32_t hash_ct(const char (&s)[N]) {
    return fnv1a(s, N - 1); // omit trailing '\0'
}

// 用法
constexpr auto h1 = hash_ct("CMD_START");
constexpr auto h2 = hash_ct("CMD_STOP");

void handle_command(const char* cmd) {
    switch (fnv1a(cmd, std::strlen(cmd))) {
    case hash_ct("CMD_START"):
        // handle start
        break;
    case hash_ct("CMD_STOP"):
        // handle stop
        break;
    default:
        // unknown
        break;
    }
}
```

这个方案简单可靠，但要注意哈希冲突可能带来的问题：在关键代码里，用 `static_assert` 验证已知字面值之间没有冲突（这只适用于字面量集合能在编译期枚举的场景）。

```cpp
static_assert(hash_ct("CMD_START") != hash_ct("CMD_STOP"), "Hash collision!");
```

#### 5.2 编译期字符串作为类型（C++20 模板化字符串）

C++20 引入了更强的模板参数特化，可以把字符串字面量作为模板参数（借助 `const char(&)[N]` 或自定义 `ct_string` 类型）。这使得把字符串直接映射到类型系统成为可能，从而能把很多配置放到类型层面，获得零运行时开销。

示例：用字符串做类型标签（摘自常见技巧）：

```cpp
// C++20 风格：将字符串字面量包装为类型参数
template <std::size_t N>
struct ct_string {
    char chars[N];
    constexpr ct_string(const char (&s)[N]) {
        for (std::size_t i = 0; i < N; ++i) chars[i] = s[i];
    }
};

template <ct_string Name>
struct ConfigItem {
    static constexpr auto name = Name;
};

constexpr ct_string cfg_name("uart.baudrate");
using BaudCfg = ConfigItem<cfg_name>;
```

然后可以基于类型做元编程，或把多个 `ConfigItem` 放进映射结构，编译器会把全部信息在编译期解析。

------

## 6. 编译期生成表格 vs 运行时生成：内存与闪存的权衡

搬到编译期并不总是万能良药。下面列出几条简单的工程经验（叙述形式）：

- **Flash 占用 vs RAM 占用**：编译期表数据通常被放到 `.rodata`（flash），不会占用 RAM；但如果表很大，flash 使用增长可能影响 OTA、大型固件部署。衡量时考虑你的闪存预算（例如 256 KB flash 与一个 20 KB 查表）。
- **编译时间**：大量复杂的编译期计算会延长编译时间，尤其是模板元编程。对于 CI/开发频繁迭代的项目，可能需要把“可选的重编译期优化”放在单独构建配置里（Release-with-constexpr vs Dev-with-runtime）。
- **调试与可读性**：把所有逻辑都写成复杂的 `constexpr` 模板会让代码难以读、难以调试。实用主义原则：把真正稳定且对运行时性能敏感的部分放到编译期，保持核心算法的实现可读。
- **工具链支持**：不同编译器对 `constexpr` 的实现差别很大（尤其是数学和库函数是否为 `constexpr`）。在交叉编译链上，先在主机上做小样例试验再推广到 CI。

------

## 编译期表的存放建议与链接选项

不同平台/链接器会把 `constexpr` 生成的 `std::array` 放到只读段或内联到代码中。要确认数据在 flash 而不是 RAM（尤其在嵌入式的启动阶段），请注意：

- 使用 `constexpr` + `const` 的全局对象通常放到只读段（flash）。例如：`constexpr auto table = ...;`
- 在某些工具链上，如果编译器认为表被修改或不安全，可能会把它复制到 RAM。在关键路径上，用 `const` 和 `alignas` 明确标注，并检查生成的 map 文件与链接脚本。
- 对于非常大的表，考虑使用 `__attribute__((section(".rodata")))` 或链接脚本把表放在特定段，或用 objcopy 把外部生成的二进制表直接合并进镜像（只在极端场景下）。

------

## 结语：实用主义的 `constexpr` 策略

- `constexpr` 并不是为了“炫技”，而是把那些**可确定、稳定、并对性能/资源有好处**的工作交给编译器。
- 在嵌入式里，它能把关键数据放到 flash、在编译期捕获错误、并减少运行时开销。
- 但也要理性：别把所有东西都模板化。重点是用好编译期能力去替代那些在设备上不易调试或代价昂贵的运行时工作。
- 开发流程上，建议把复杂的编译期生成（比如大表、复杂元编程）放在 CI 的 Release 构建中，保留快速编译的开发配置以提高迭代速度