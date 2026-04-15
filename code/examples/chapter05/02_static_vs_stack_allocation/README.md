# Chapter 5.2 - 静态存储与栈上分配策略

本目录包含《静态存储与栈上分配策略》章节的代码示例。

## 说明

在嵌入式系统中，选择把数据放在静态区还是栈上直接关系到程序的可靠性、启动时间、代码可维护性与实时性。

## 示例文件

| 文件 | 说明 | 关键点 |
|------|------|--------|
| `static_allocation_demo.cpp` | 静态存储演示 | .data/.bss/.rodata段、自定义段、静态局部变量 |
| `stack_allocation_demo.cpp` | 栈分配演示 | 栈帧增长、递归危险、VLA/alloca警告 |
| `ring_buffer_demo.cpp` | 环形缓冲区 | 静态分配的经典应用，ISR与主循环通信 |

## 编译方法

```bash
mkdir build && cd build
cmake ..
make
```

## 运行示例

```bash
./static_allocation_demo
./stack_allocation_demo
./ring_buffer_demo
```

## 核心概念

### 静态存储类型

- **.data**: 已初始化全局/静态变量，启动时从Flash复制到RAM
- **.bss**: 未初始化全局/静态变量，启动时清零
- **.rodata**: 只读常量，通常留在Flash中节省RAM
- **自定义段**: 通过`__attribute__((section(...)))`控制数据放置

### 栈分配特点

- **快速**: 指针加减操作
- **自动**: 作用域结束自动释放
- **有限**: 典型几KB大小
- **危险**: 大分配、深递归可能溢出

### 使用建议

1. **查表、常量**: 使用`.rodata`放在Flash
2. **大缓冲区**: 使用静态分配
3. **临时小对象**: 使用栈分配
4. **ISR通信**: 使用环形缓冲区
5. **避免**: VLA、alloca、深递归

## 环境要求

- C++17 或更高
- GCC 7+ 或 Clang 5+
- 支持`__attribute__((section(...)))`的编译器
