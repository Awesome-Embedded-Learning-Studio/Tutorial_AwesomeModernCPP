# Chapter 8.7: 字面量运算符与自定义单位 示例

本目录展示如何使用字面量运算符实现类型安全的单位系统。

## 示例文件

### time_units.cpp
时间单位字面量：`_ms`、`_us`、`_s`，以及单位之间的运算和转换。

### frequency_units.cpp
频率和波特率单位字面量：`_Hz`、`_kHz`、`_MHz`、`_baud`。

### voltage_units.cpp
浮点字面量运算符示例，用于电压和长度单位。

## 编译运行

```bash
mkdir build && cd build
cmake ..
make
./time_units
./frequency_units
./voltage_units
```

## 特性

- 编译期计算，零运行时开销
- 类型安全，防止单位混淆
- 代码自文档化：`5000_ms` 比 `5000` 更清晰
- 支持单位运算和转换
