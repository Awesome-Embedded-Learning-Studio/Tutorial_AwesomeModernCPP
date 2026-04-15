# Chapter 8.6: 类型别名与 using 声明 示例

本目录展示 `using` 声明的用法，这是现代 C++ 中定义类型别名的主流方式。

## 示例文件

### using_basics.cpp
对比 `typedef` 与 `using` 的基本用法。

### using_templates.cpp
展示 `using` 的模板别名能力，这是 `typedef` 无法做到的。

### using_register.cpp
使用 `using` 构建类型安全的寄存器访问体系。

## 编译运行

```bash
mkdir build && cd build
cmake ..
make
./using_basics
./using_templates
./using_register
```

## 特性

- 语法更直观：`using 新名称 = 旧类型`
- 支持模板别名
- 函数指针语法更清晰
- 可与模板配合减少代码重复
