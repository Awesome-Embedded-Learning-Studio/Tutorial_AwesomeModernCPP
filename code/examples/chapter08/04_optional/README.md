# Chapter 8.4: std::optional 示例

本目录展示 `std::optional` 的用法，用于表示"有值或无值"的场景。

## 示例文件

### optional_basics.cpp
`std::optional` 基本用法：创建、检查、访问和 `value_or`。

### optional_parse.cpp
实际应用：解析函数返回 `optional` 表示可能失败。

### optional_chain.cpp
使用 `optional_map` 实现链式转换。

## 编译运行

```bash
mkdir build && cd build
cmake ..
make
./optional_basics
./optional_parse
./optional_chain
```

## 特性

- 比 `nullptr` 更清晰的语义
- 避免"哨兵值"（如 -1、空字符串）
- 适合"失败是常态"的场景
- 支持函数式风格的链式调用
