# Chapter 2.4 - 编译期多态 vs 运行时多态

本目录包含《编译期多态 vs 运行时多态》章节的代码示例。

## 说明

编译期多态（模板、CRTP、std::variant）与运行时多态（虚函数）在嵌入式系统中的权衡。

## 示例文件

| 文件 | 说明 | 关键点 |
|------|------|--------|
| `runtime_polymorphism.cpp` | 运行时多态：虚函数实现 | vtable 间接调用，对象携带 vptr |
| `compile_time_polymorphism.cpp` | 编译期多态：模板实现 | 编译期解析，可内联，零开销 |
| `crtp_example.cpp` | CRTP 示例 | 基类调用派生类方法，代码复用 |
| `variant_example.cpp` | std::variant 和 std::visit | 封闭型多态，编译期分发 |

## 编译方法

```bash
mkdir build && cd build
cmake ..
make
```

## 运行示例

```bash
./runtime_polymorphism
./compile_time_polymorphism
./crtp_example
./variant_example
```

## 性能对比

| 特性 | 运行时多态 | 编译期多态 |
|------|-----------|-----------|
| 调用开销 | vtable 间接查找 | 直接调用或内联 |
| RAM 占用 | 每对象 +vptr | 无额外开销 |
| Flash 占用 | 函数体一份 | 每实例一份（可能膨胀）|
| 类型安全 | 运行时 | 编译期 |
| 灵活性 | 可运行时替换 | 编译期确定 |

## 使用建议

1. **优先使用编译期多态**：在性能敏感路径、ISR 中
2. **保留运行时多态**：需要插件、动态替换的场景
3. **CRTP 适合**：驱动框架、代码复用
4. **std::variant 适合**：封闭的几种状态/事件类型

## 环境要求

- C++17 或更高
- GCC 7+ 或 Clang 5+
