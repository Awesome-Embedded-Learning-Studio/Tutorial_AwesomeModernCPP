# Chapter 8.1: enum class 示例

本目录包含 `enum class` 的示例代码，展示强类型枚举的优势。

## 示例文件

### enum_class_basics.cpp
对比传统 `enum` 与 `enum class` 的基本用法。

### enum_class_bitflags.cpp
演示如何为 `enum class` 实现位运算符，用于位标志。

### enum_class_memory.cpp
展示如何指定底层类型节省内存，以及与 C 接口互操作。

## 编译运行

```bash
mkdir build && cd build
cmake ..
make
./enum_class_basics
./enum_class_bitflags
./enum_class_memory
```
