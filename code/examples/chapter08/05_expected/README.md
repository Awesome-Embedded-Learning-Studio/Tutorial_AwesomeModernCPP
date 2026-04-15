# Chapter 8.5: std::expected 示例

本目录展示 `std::expected` 的用法，用于表示"成功或失败（带错误信息）"的场景。

## 示例文件

### expected.hpp
简化的 `std::expected` 实现，兼容 C++17。

### parse_example.cpp
使用 `expected` 进行错误处理和链式调用的示例。

## 编译运行

```bash
mkdir build && cd build
cmake ..
make
./parse_example
```

## 特性

- 比异常更可控的错误处理
- 比 `optional` 携带更多信息
- 支持链式调用（`map`、`and_then`）
- 零开销抽象
