# Chapter 8.2: 类型安全的寄存器访问 示例

本目录展示如何使用模板和强类型枚举封装 MMIO 寄存器访问。

## 示例文件

### reg.hpp
类型安全的寄存器访问基础模板，提供 `mmio_reg` 和 `reg_field`。

### uart_regs.hpp
UART 寄存器定义示例，展示如何使用 `reg.hpp` 定义具体外设。

### main.cpp
使用示例，演示寄存器读写、位操作和字段访问。

## 编译运行

```bash
mkdir build && cd build
cmake ..
make
./type_safe_register_demo
```

## 特性

- 编译期类型检查
- 强类型枚举支持
- 字段访问封装
- 内存屏障保护
