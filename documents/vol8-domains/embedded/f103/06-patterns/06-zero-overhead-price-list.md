---
title: "零开销的价格表：18200 字节日志固件的解剖"
description: "用 nm --size-sort 把日志示例固件开膛：main 一个函数吃掉 8.2KB——装配链按实参类型组合内联进每个调用点；render 三胞胎各 1.3KB、write_integral 724B……『零开销』指的不是零体积，是零抽象税，本文把每一项的真实价格列成清单"
chapter: 6
order: 6
tags:
  - stm32f1
  - intermediate
  - 零开销抽象
  - 嵌入式
  - 实战
  - 优化
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 14
prerequisites:
  - "终审在 map：『裁剪级别连字符串都不进固件』的完整证据链"
related:
  - "两行编译旗子砍掉 64% 固件：-ffunction-sections 的函数粒度回收实录"
---

# 零开销的价格表：18200 字节日志固件的解剖

## 引言："零开销"不等于"零体积"

日志示例固件量出来 18200 字节 text，隔壁手写串口控制台的 uart_example 只有 5476——差价 12.7KB。有人会问：不是说零开销吗？这正是把"零开销抽象"这个词讲透的机会：它说的是**不为抽象机制本身付运行时税**（虚调用、类型擦除、动态分发、解释器循环），而不是产物体积为零。模板把"分发"全部编译成了直代码，直代码就有体积。这篇文章用 `nm --size-sort` 把固件开膛，给"零开销"开一张诚实的价格表——每项功能多少钱，一目了然。

## 解剖：谁在吃 flash

```sh
arm-none-eabi-nm --size-sort -t d build/examples/06_log/log_example | tail -12
```

按体积排出的头部名单（节选，单位字节）：

| 符号 | 体积 | 是什么 |
|---|---|---|
| `main` | **8224** | 整条日志装配链内联后的现场 |
| `render<string_view,...>` ×3 | 1368/1364/1300 | 格式串渲染的三个实例化（不同实参组合） |
| `HAL_RCC_OscConfig` | 1004 | HAL 时钟树 |
| `HAL_GPIO_Init` | 1060 | HAL GPIO |
| `write_integral<uint32_t>` | 724 | to_chars 整数路径 |
| `HAL_RCC_ClockConfig` | 384 | HAL |
| `HAL_UART_Transmit` | 290 | HAL 发送 |
| `append_escaped` | 284 | 转义折叠 |

两个立刻跳出来的事实：

**第一，`main` 一个函数占了固件的 45%。** 不是哪个"库组件"大——是**内联**把 logger 的全部机器摊进了调用现场。示例里有五条日志调用，每条的实参类型组合都不一样（拼接式 `blink=n` 与格式式 `n={} hex={:x}` 各自展开），每条调用点的 `emit`/`append_header`/`render` 都按自己的类型组合实例化并全内联。COMDAT 去重只救得了**跨编译单元的同签名实例化**；同一个 main 里不同实参组合本来就不是同一份代码。

**第二，"三胞胎"现象。** 三个 `render` 各 1.3KB——同一份模板源码，因为三组不同的实参类型（无实参尾部、string_view 头、uint32_t 头……）各生成一份。这是模板体积模型的基本盘：**价格跟着类型组合数走，不跟源码行数走**。

## 这 12.7KB 差价买到了什么

和手写控制台比不公平的地方在于价目不同。12.7KB 的价目单：两种语法（拼接 + format 串）、编译期字段校验（数量/spec/逐字段类型，信标报错）、毫秒时间戳、无宏 `source_location` 元数据、级别裁剪机制、截断安全、以及自定义类型的开放扩展点（`Formatter<T>` 特化）。而手写控制台是"每条响应一个手写字符串"。**买的是"日志基础设施"，不是"几行输出"**——64KB 的 BluePill 上这个价目能不能接受，取决于项目里日志的分量；20KB RAM 的板子至少不会再为 1KB 的 `std::format` 查表盘发愁（我们的整数路径全套 724 字节）。

## 调价旋钮（v2 的课题，先记账）

价格表在手，降价方向也就清楚了，这里只记账不动手：

- **`-Os` 或按目标拆分优化**：教程库默认 `-O3` 换 `-Os`，内联决策会保守很多，这类"main 吃 8KB"的形态会显著收敛；
- **关键节点 `noinline`**：把 `emit`/`render` 的公共骨架挡在内联之外，让多个调用点共享一份机器，代价是每次调用的调用开销和丢掉的部分常量折叠；
- **减少类型组合**：打日志时实参类型规整一点（统一 `uint32_t` 而不是 int/unsigned 混用），实例化份数直接下降；
- **`LINE_BYTES` 调小**：行缓冲是栈不是 flash，但截断更早发生。

这些旋钮各自的收益要重新量，价格表会跟着变——方法论不变：**先 `nm --size-sort`，再谈优化**。

## 小结

- "零开销"= 零抽象税，不是零体积；模板分发编译成直代码，直代码有价格；
- 价格表的头部是内联：`main` 8.2KB（45%），因为每条日志调用按实参类型组合各生成一套机器；
- 同一模板的多份实例化（render 三胞胎）是模板体积模型的基本盘：价格跟着类型组合数走；
- 12.7KB 买到的是日志基础设施（双语法/编译期校验/时间戳/裁剪/扩展点），不是几行输出；
- 降价旋钮（-Os、noinline、类型规整）先记账后实验，量了再动。

## 参考资源

- [binutils：nm 与 --size-sort](https://sourceware.org/binutils/docs/binutils/nm.html)
- [GCC 优化选项（-O3/-Os/inline limits）](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html)
- [libestdx 仓库（logger 模块与 examples/06_log）](https://github.com/Charliechen114514/libestdx)
