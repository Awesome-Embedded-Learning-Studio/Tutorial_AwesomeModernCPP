# Chapter 2.1 - 零开销抽象示例

本目录包含《零开销抽象》章节的所有代码示例。

## 说明

零开销抽象（Zero Overhead Abstraction）是 C++ 的核心设计理念：**你不需要为你不使用的东西付出代价，你使用的东西不可能手写得更好。**

## 示例文件

| 文件 | 说明 | 关键点 |
|------|------|--------|
| `gpio_example.cpp` | GPIO 控制：C 宏 vs C++ 类型安全 | 模板在编译期展开，生成的机器码与 C 相同 |
| `state_machine.cpp` | 状态机：switch-case vs CRTP | 编译时多态无虚函数开销，std::variant 零开销 |
| `raii_example.cpp` | RAII 资源管理 | 析构函数自动调用，编译器内联无性能代价 |
| `constexpr_example.cpp` | 编译期计算 | constexpr 让计算在编译期完成，零运行时开销 |
| `inline_vs_macro.cpp` | 内联函数 vs 宏 | 类型安全、无副作用、可编译期计算 |
| `loop_unroll.cpp` | 模板元编程循环展开 | 编译期展开循环，避免分支预测失败 |
| `strong_types.cpp` | 强类型包装 | 运行时零开销，编译期类型检查 |

## 编译方法

```bash
mkdir build && cd build
cmake ..
make
```

## 运行示例

```bash
./gpio_example
./state_machine
./raii_example
./constexpr_example
./inline_vs_macro
./loop_unroll
./strong_types
```

## 验证零开销

使用 Compiler Explorer (https://godbolt.org/) 或本地命令查看汇编：

```bash
g++ -O2 -S -fverbose-asm gpio_example.cpp
```

对比 C 版本和 C++ 版本生成的汇编是否相同。

## 环境要求

- C++17 或更高
- GCC 7+ 或 Clang 5+
- 启用 -O2 优化（零开销抽象依赖编译器优化）

## 关键编译选项

```bash
-O2 或 -O3         # 优化级别，至少要 O2
-flto              # 链接时优化
-fno-rtti          # 禁用 RTTI（嵌入式常用）
-fno-exceptions    # 禁用异常（可选）
```

## 嵌入式注意事项

1. **必须开启优化**：`-O0` 下很多零开销抽象会有开销
2. **避免虚函数**：使用 CRTP 或 `std::variant` 替代
3. **避免动态分配**：使用栈、静态存储或对象池
4. **善用 constexpr**：把配置计算放在编译期
