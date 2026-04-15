# Chapter 8.3: std::variant 示例

本目录展示 `std::variant` 类型安全联合体的用法。

## 示例文件

### variant_basics.cpp
`std::variant` 基本用法：创建、赋值、访问和 `std::visit`。

### variant_visit.cpp
使用 `std::visit` 与重载集合模式匹配不同类型。

### variant_message.cpp
实际应用：使用 `variant` 实现类型安全的消息队列。

## 编译运行

```bash
mkdir build && cd build
cmake ..
make
./variant_basics
./variant_visit
./variant_message
```

## 特性

- 类型安全的联合体
- 自动管理对象生命周期
- `std::visit` 访问者模式
- 编译期类型检查
