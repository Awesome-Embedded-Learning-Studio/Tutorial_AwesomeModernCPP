---
title: "卷 1 · 基础 Lab：气象站——从一条读数到一台解码器"
description: "卷 1 动手实验：把类型侦察、constexpr 换算、数组清洗、运算符重载、多态与异常安全串成一条气象站流水线——六个步骤从 sizeof 侦察一路做到 ASan 零报告，最后附一道不用 pow/ldexp 徒手解码 float 的 L5 挑战（CSAPP 浮点位级操作改编）。每步含目标、步骤与验收标准。"
chapter: 1
order: 3
tags: [host, beginner, cpp-modern, 实战]
difficulty: beginner
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 7
prerequisites: []
related: []
---

# 卷 1 · 基础 Lab：气象站——从一条读数到一台解码器

## 实验目标

卷 1 的知识点像一桌零件：`sizeof`、`numeric_limits`、`constexpr`、`std::array`、运算符重载、虚函数、异常。这个 Lab 把它们拧成一条真实的气象站流水线——你从「摸清本机类型」出发，一路做到「多态传感器阵列 + 异常安全的数据解析」，最后用纯位运算手搓一台 float 解码器。做完你会对「C++ 的类型系统不只是语法糖，它和硬件、和内存是通的」这件事有肌肉记忆。

每个步骤就是一个独立的 `.cpp` 文件，在 `/tmp` 下自己的目录里真编译真跑。每步都有验收标准——输出对不上就说明有一步没走对，先回题面标注的章节链接读教材，再不行看[实验参考](./04-lab-solutions.md)。默认编译命令 `g++ -std=c++17 -Wall -Wextra`，个别步骤要求 sanitizer 或关闭优化，题面会写明。

## 步骤 1：类型侦察 {#lab-1}

难度 **L1** · 涉及[基本数据类型](../ch01/01-basic-types.md)

**目标**：把本机的类型家底一次摸清，确认数据模型。

1. 写一个程序，打印 `char / short / int / long / long long / double` 的 `sizeof`。
2. 用 `std::numeric_limits` 打印 `int` 和 `unsigned int` 的完整取值范围。
3. 打印定宽类型 `int32_t / int64_t / uint8_t / size_t` 的大小。

**验收标准**：贴出完整输出；说清本机 `long` 是几字节——这证明本机是 LP64 还是 LLP64 数据模型？为什么「long 是 8 字节」这个事实不能写进你的跨平台代码假设里？

[实验参考 →](./04-lab-solutions.md#lab-1)

## 步骤 2：编译期温度换算 {#lab-2}

难度 **L2** · 涉及[const 初探](../ch01/03-const-basics.md)、[inline 与 constexpr 函数](../ch03/04-inline-constexpr.md)

**目标**：把温度换算做成 `constexpr` 函数，让编译器在编译期就替你验算。

1. 写 `constexpr double c_to_f(double c)` 和 `constexpr double f_to_c(double f)`。
2. 用 `static_assert` 把 `c_to_f(0.0) == 32.0`、`c_to_f(-40.0) == -40.0`、`f_to_c(32.0) == 0.0` 钉死成编译期断言。
3. 运行时读入一个摄氏温度打印华氏值；打印一张 $-40 / 0 / 25 / 37$ 的换算对照表。
4. 用容差 $1e-9$ 验证 `f_to_c(c_to_f(25.0))` 能回到 25.0——浮点比较为什么必须走容差而不是 `==`？

**验收标准**：贴出编译（`static_assert` 通过）和运行输出；说清 `static_assert` 里为什么敢用 `==` 比较浮点（提示：选的都是能精确表示的值）。

[实验参考 →](./04-lab-solutions.md#lab-2)

## 步骤 3：温度序列清洗 {#lab-3}

难度 **L2** · 涉及[std::array](../ch05/02-std-array.md)、[range-for 循环](../ch02/03-range-for.md)、[std::vector 快速上手](../ch11/01-vector.md)

**目标**：给一条带坏点的读数序列做清洗统计。

1. 用 `std::array<double, 8>` 存 8 个读数：`{22.5, 19.8, -99.0, 25.1, 88.8, 21.3, 24.0, 20.2}`——其中 $-99.0$ 和 $88.8$ 是传感器故障产生的异常值。
2. 合理范围定为 $[-50, 60]$：用 range-for 过滤，合法读数 push 进 `std::vector<double>`，异常值打印一行「剔除异常值: …」。
3. 用 `std::minmax_element` 求最低最高，range-for 累加求平均。

**验收标准**：贴出输出（应有 6 个合法读数，最低 19.8、最高 25.1、平均 22.15）；说清这里为什么用 `std::vector` 接清洗结果而不是 `std::array`（提示：异常值的个数编译期不知道）。

[实验参考 →](./04-lab-solutions.md#lab-3)

## 步骤 4：Temperature 类 {#lab-4}

难度 **L3** · 涉及[类的定义](../ch06/01-class-basics.md)、[算术与比较运算符](../ch07/01-arithmetic-comparison.md)

**目标**：把「温度」从裸 `double` 升级成有契约的类。

1. 写 `class Temperature`：私有 `double celsius_`；构造函数把低于 $-273.15$ 的输入钳制到绝对零度。
2. 提供 `as_celsius()`、`as_fahrenheit()`、`as_kelvin()` 三个只读转换。
3. 按教材模式实现成员 `operator+=`、`operator-=`，非成员 `operator+`、`operator-`、`==`、`!=`、`<` 和输出运算符 `<<`（格式 `25C`）。
4. 验证：`Temperature(25.0) + Temperature(-10.0)` 得 `15C`；`25.0 - (-10.0)` 得 `35C`；`Temperature(-280.0)` 被钳成 `-273.15C`。

**验收标准**：贴出完整输出；说清构造函数里的钳制为什么保证了「任何 Temperature 对象都满足绝对零度下界」这条不变量，以及 `operator+=` 里为什么还要再钳一次。

[实验参考 →](./04-lab-solutions.md#lab-4)

## 步骤 5：多态传感器阵列 {#lab-5}

难度 **L3** · 涉及[抽象类与接口](../ch08/03-abstract-classes.md)、[智能指针预告](../ch04/04-smart-ptr-preview.md)

**目标**：把两种传感器装进同一个阵列，用统一接口读数。

1. 写抽象基类 `ISensor`：纯虚 `virtual std::string name() const = 0;` 和 `virtual double read() const = 0;`，虚析构 `= default`。
2. 派生 `TemperatureSensor` 和 `HumiditySensor`（各存 `id_` 与当前读数），分别重写两个纯虚函数。
3. 用 `std::vector<std::unique_ptr<ISensor>>` 装三个传感器（T-01: 23.5、H-01: 61.0、T-02: 24.1），range-for 遍历打印每个传感器的名字和读数，最后打印读数平均值。

**验收标准**：贴出输出（平均应为 36.2）；回答：为什么容器装的是 `unique_ptr<ISensor>` 而不是 `ISensor` 值？主循环一行都不用改的前提下，加一种「风速计」传感器要动几处代码？

[实验参考 →](./04-lab-solutions.md#lab-5)

## 步骤 6：异常与 RAII {#lab-6}

难度 **L4** · 涉及[异常基础](../ch10/01-try-catch.md)、[异常安全](../ch10/02-exception-safety.md)

**目标**：给数据解析装上异常通道和 RAII 守卫，并让 sanitizer 全程零报告。

1. 写 `Reading parse_reading(const std::string& line)`：解析一行 `"TEMP,23.5"`——缺逗号抛 `std::invalid_argument`、传感器名为空抛 `std::invalid_argument`、数值部分用 `std::stod` 解析失败转成 `std::invalid_argument` 抛出。
2. 写一个 RAII 守卫 `ScopedReport`：构造打印 `[label] 开始采集`，析构打印 `[label] 报告已生成(析构自动执行)`，拷贝构造与拷贝赋值 `= delete`。
3. 在 `main` 里对 5 行输入（`"TEMP,23.5"`、`"HUMI,61.0"`、`"TEMP,abc"`、`"PRES,1013.2"`、`"BROKEN"`）逐行处理：**守卫对象声明在 try 块内部**，解析异常用层次化 catch（`invalid_argument` → `exception`）兜住。
4. 用 `-fsanitize=address,undefined` 构建，整个程序跑完必须零报告。

**验收标准**：贴出普通构建和 ASan 构建两份输出（都应是零报告）；观察输出的先后顺序，说清「[轮次] 报告已生成」为什么出现在「参数错误」**之前**——析构函数和 catch 块谁先执行？这一步就是栈展开在干活。

[实验参考 →](./04-lab-solutions.md#lab-6)

## 附加挑战（L5）：徒手解码 float {#lab-l5}

难度 **L5** · 涉及[内存对齐与填充](../ch12/03-alignment-padding.md)（字节与位）、[循环语句](../ch02/02-loops.md)、[联合体、枚举、位域与 typedef](../c_tutorials/13-union-enum-bitfield-typedef.md)（union 类型双关见该篇「用类型双关查看浮点数的二进制表示」一节；IEEE 754 单精度结构——符号 1/指数 8/尾数 23——为本 Lab 引入的教材外补充）

**目标**：**不用 `math.h` 的 `pow`/`ldexp`**，用纯位运算和循环把一台「float 解码器」手搓出来。本题受 CSAPP「浮点位级操作」练习启发（入门卷 L5 口径＝「用本卷知识可解的最难问题」，档位口径见[练习总览](./index.md)）。

1. 写 `std::uint32_t float_bits(float f)`：用 **`std::memcpy`** 把 float 的 32 个比特拷进 `uint32_t`——注意，**C++ 里 union 类型双关是未定义行为**（和 C 不一样，回看 [c_tutorials 的联合体](../c_tutorials/13-union-enum-bitfield-typedef.md)里的对比），所以必须走 `memcpy`。
2. 拆位：符号位 = `bits >> 31 & 1`；指数域 = `(bits >> 23) & 0xFF`；尾数域 = `bits & 0x7FFFFF`。
3. 写 `double power_of_two(int e)`：`e >= 0` 就循环乘 2、`e < 0` 就循环除 2。
4. 写 `double decode_float(float f)`：规格化数按 `value = (1 + mant / 2^23) * 2^(exp - 127)` 解码，`exp == 0` 的非规格化数（含 0）按 `mant / 2^23 * 2^(-126)` 解码，符号位决定正负。
5. 验证 `1.0f`、`3.14f`、`-0.5f`、`0.0f`、`0.1f`：解码结果与 `static_cast<double>(f)` 的相对误差应为 **0**（想想为什么是精确的）；再打印 `decode_float(3.14f)` 与十进制 $3.14$ 的相对差。

**验收标准**：贴出五个输入的解码结果与误差；回答：①`1 + mant / 2^23` 里的那个 `1` 是什么？（隐含前导 1。）②为什么解码值和 `double(f)` 完全一致、但和十进制 3.14 差 $3.34e-08$？③`memcpy` 在这里为什么不是「浪费了一次拷贝」？（编译器会把它优化掉。）

[实验参考 →](./04-lab-solutions.md#lab-l5)

## 提交物清单

一个目录装下 7 个 `.cpp` 文件、每步的终端记录（`stepN.log`），以及 200 字以内的小结——用你自己的话说清「类型系统的每一步，最后都落回了字节和位」这件事你在哪一步看得最真切。
