# Chapter 3.5 - 对象大小与平凡类型示例

本目录包含《对象大小，平凡类型》章节的代码示例。

## 说明

展示类型属性（平凡、标准布局）如何影响对象大小和内存布局。

## 编译方法

```bash
mkdir build && cd build
cmake ..
make
./object_size_example
```

## 关键点

1. **is_trivial**：平凡类型可以用 memcpy 安全复制
2. **is_trivially_copyable**：平凡可复制类型适合 DMA 传输
3. **is_standard_layout**：标准布局可与 C 代码互操作
4. **对齐**：填充内存影响对象大小
5. **虚函数**：虚函数表指针增加对象大小

## 嵌入式应用

- **DMA 传输**：使用平凡可复制类型
- **与 C 互操作**：使用标准布局类型
- **RAM 优化**：理解对象大小和对齐

## 环境要求

- C++20 或更高
- GCC 10+ 或 Clang 10+
