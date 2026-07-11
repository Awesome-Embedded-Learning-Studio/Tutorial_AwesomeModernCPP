---
chapter: 99
cpp_standard:
- 23
description: 捕获与打印调用栈的工具，程序自省与崩溃诊断的标准手段
difficulty: intermediate
order: 19
reading_time_minutes: 3
tags:
- host
- cpp-modern
- intermediate
- 调试
title: std::stacktrace
---
<!--
参考卡：std::stacktrace（C++23）。本机 GCC 16.1.1 -std=c++23 实测。
承重坑：GCC 的 stacktrace 符号不在主库，要单链实验性库；GCC 16+ 是 -lstdc++exp（无下划线），GCC 12-15 是 -lstdc++_exp（有下划线）。漏链接报 undefined reference to std::__stacktrace_impl::_S_current。
-->

# std::stacktrace（C++23）

## 一句话

在任意位置抓取当前调用栈，拿到每个栈帧的函数名、源文件、行号——程序自省与崩溃诊断的标准工具，替代 backtrace 一类笨办法。

## 头文件

`#include <stacktrace>`

::: warning GCC 链接坑（实测）
GCC 的 `<stacktrace>` 符号不在主库里，要单独链接实验性库：**GCC 16+ 是 `-lstdc++exp`（注意无下划线），GCC 12–15 是 `-lstdc++_exp`（有下划线）**。CMake 里写 `target_link_libraries(target PRIVATE stdc++exp)`。漏链接会报 `undefined reference to std::__stacktrace_impl::_S_current`。
:::

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 抓当前栈 | `std::stacktrace::current()` | 返回当前调用栈快照 |
| 带跳过帧 | `stacktrace::current(skip, max_depth)` | 跳过前 skip 帧、最多取 max_depth |
| 栈深度 | `st.size()` | 帧数 |
| 取一帧 | `st[i]` | 拿到 `stacktrace_entry` |
| 帧描述 | `entry.description()` | 函数名加地址等可读串 |
| 帧源位置 | `entry.source_file()` / `source_line()` | 源文件名与行号（需调试符号） |
| 流输出 | `std::cout << st` | 直接打印整个栈 |

## 最小示例

```cpp
// Standard: C++23
// 编译： g++ -std=c++23 demo.cpp -lstdc++exp     （GCC 16+）
#include <stacktrace>
#include <iostream>

void inner() {
    auto st = std::stacktrace::current();
    std::cout << "depth=" << st.size() << "\n";
    for (std::size_t i = 0; i < st.size(); ++i)
        std::cout << "  #" << i << " " << st[i] << "\n";
}
void outer() { inner(); }
int main() { outer(); }
```

实测输出（GCC 16.1.1，`-std=c++23 -lstdc++exp`）：

```text
depth=6
  #0  inner() [0x5ca9fa0aa28d]
  #1  outer() [0x5ca9fa0aa3d4]
  #2  main [0x5ca9fa0aa3e0]
  #3  <unknown> [0x7f1709227740]
  #4  __libc_start_main [0x7f1709227878]
  #5  _start [0x5ca9fa0aa184]
```

自己写的函数（inner/outer/main）名解析出来了；动态库部分显示 `<unknown>` 属正常，那几帧地址未符号化。

## 嵌入式适用性：低

- 依赖符号表与运行时栈回溯，裸机和 RTOS 上通常不可用或需专门移植
- 带符号信息会撑大镜像；`-s` strip 后只剩地址
- 嵌入式更常用硬件断点、ITM、SEGGER RTT，或自定义 crashdump 回溯
- host 端工具链、测试桩、CI 失败诊断倒是正好用得上

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 12 | 16 | 19.34 |

## 另见

- [cppreference: std::stacktrace](https://en.cppreference.com/w/cpp/header/stacktrace)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
