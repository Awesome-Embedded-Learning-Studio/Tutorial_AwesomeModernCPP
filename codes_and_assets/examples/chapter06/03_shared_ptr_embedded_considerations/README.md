# Chapter 6.3: std::shared_ptr 嵌入式环境注意事项

本目录包含 `std::shared_ptr` 的使用示例，重点展示其在嵌入式环境下的代价和注意事项。

## 文件说明

| 文件 | 描述 |
|------|------|
| `basic_usage.cpp` | shared_ptr 基本用法和内存开销分析 |
| `circular_reference.cpp` | 循环引用问题和 weak_ptr 解决方案 |
| `atomic_overhead.cpp` | 原子操作开销和性能对比 |

## 关键概念

- **控制块**: shared_ptr 额外的内存分配开销
- **原子操作**: 引用计数使用原子操作，可能不是 lock-free
- **make_shared**: 一次性分配优化，减少内存碎片
- **循环引用**: 会导致内存泄漏，使用 weak_ptr 打破
- **enable_shared_from_this**: 安全地从成员函数获取 shared_ptr

## 嵌入式注意事项

1. **内存开销**: 控制块额外分配，在 RAM 受限环境下需谨慎
2. **原子操作**: 某些平台可能不是 lock-free，会禁用中断
3. **ISR 环境**: 不要在 ISR 中使用 shared_ptr
4. **替代方案**: 考虑侵入式引用计数或对象池

## 编译运行

```bash
mkdir build && cd build
cmake ..
make
./basic_usage
./circular_reference
./atomic_overhead
```
