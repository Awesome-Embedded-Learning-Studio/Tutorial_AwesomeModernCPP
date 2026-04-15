# Chapter 2.3 - constexpr 示例

本目录包含《constexpr：把计算推到编译期》章节的代码示例。

## 说明

`constexpr` 的核心价值不是"让代码更快"，而是**让计算完全消失在运行时**。

## 示例文件

| 文件 | 说明 |
|------|------|
| `basic_constexpr.cpp` | constexpr 基础用法，包括查表、寄存器位定义、波特率计算 |

## 编译方法

```bash
mkdir build && cd build
cmake ..
make
./basic_constexpr
```

## 关键点

1. **constexpr vs inline**：inline 关注"是否在调用点展开"，constexpr 关注"是否需要在运行时计算"
2. **查表生成**：在编译期生成查找表，存储在 Flash 中，节省 RAM
3. **寄存器定义**：用 constexpr 替代 #define，获得类型安全
4. **编译期计算失败**：如果 constexpr 无法在编译期完成，编译器会报错（这是好事）

## 环境要求

- C++17 或更高
- GCC 7+ 或 Clang 5+
