# Chapter 2.2 - 内联函数与编译器优化示例

本目录包含《内联函数与编译器优化》章节的代码示例。

## 说明

`inline` 关键字在嵌入式开发中经常被误解。它的核心作用是**链接层面的语义**，而不是性能优化的指令。

## 示例文件

| 文件 | 说明 | 关键点 |
|------|------|--------|
| `inline_basics.cpp` | inline 基础概念 | inline 不保证内联，真正作用是 ODR |
| `inline_vs_macro.cpp` | inline 函数 vs 宏 | 类型安全、无副作用、可编译期计算 |
| `code_bloat.cpp` | 代码膨胀示例 | 过度内联会导致 Flash 占用增加 |
| `constexpr_vs_inline.cpp` | constexpr vs inline | 编译期计算 vs 调用展开 |

## 编译方法

```bash
mkdir build && cd build
cmake ..
make
```

## 运行示例

```bash
./inline_basics
./inline_vs_macro
./code_bloat
./constexpr_vs_inline
```

## 验证内联效果

使用 Compiler Explorer (https://godbolt.org/) 或本地命令查看汇编：

```bash
# 查看汇编输出
g++ -O2 -S -fverbose-asm inline_basics.cpp

# 查看可执行文件大小
size build/inline_basics

# 查看符号表（看函数是否真的存在）
nm build/inline_basics
```

## 核心概念

1. **inline 的真正作用**：允许函数在多个翻译单元中定义而不违反 ODR
2. **编译器的决定**：是否内联由编译器根据函数大小、调用频率等决定
3. **代码膨胀风险**：过度内联会增加 Flash 占用，可能影响 I-Cache
4. **constexpr vs inline**：constexpr 关注"是否在编译期计算"，inline 关注"是否展开调用"

## 环境要求

- C++17 或更高
- GCC 7+ 或 Clang 5+
- 启用 -O2 优化才能看到内联效果
