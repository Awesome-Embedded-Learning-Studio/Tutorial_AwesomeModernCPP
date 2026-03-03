# Chapter 6.2: std::unique_ptr 零开销的独占所有权

本目录包含 `std::unique_ptr` 的使用示例，展示其在嵌入式环境中的零开销特性和应用。

## 文件说明

| 文件 | 描述 |
|------|------|
| `basic_usage.cpp` | unique_ptr 基本用法和零开销验证 |
| `custom_deleter.cpp` | 自定义删除器管理非堆资源 |
| `polymorphism.cpp` | unique_ptr 与多态和 PIMPL 模式 |

## 关键概念

- **零开销**: `sizeof(unique_ptr<T>) == sizeof(T*)` （默认删除器）
- **独占所有权**: 不可拷贝，只能移动
- **异常安全**: 使用 `make_unique` 确保异常安全
- **自定义删除器**: 管理任意类型的资源
- **EBO**: 空基类优化使无状态删除器不增加大小

## 编译运行

```bash
mkdir build && cd build
cmake ..
make
./basic_usage
./custom_deleter
./polymorphism
```
