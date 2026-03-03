# Chapter 6.4: unique_ptr、shared_ptr 的嵌入式取舍

本目录包含智能指针在嵌入式环境下的取舍对比示例。

## 文件说明

| 文件 | 描述 |
|------|------|
| `comparison.cpp` | unique_ptr vs shared_ptr vs 侵入式引用计数对比 |

## 关键概念

- **unique_ptr**: 零开销，独占所有权，嵌入式首选
- **shared_ptr**: 有额外开销（控制块、原子操作），谨慎使用
- **侵入式计数**: 内存紧张时的替代方案

## 选择准则

1. **确定所有权** - 优先用 unique_ptr
2. **必须共享** - 考虑 shared_ptr 的开销
3. **内存受限** - 使用侵入式引用计数

## 编译运行

```bash
mkdir build && cd build
cmake ..
make
./comparison
```
