# Chapter 7.3 - 循环缓冲区示例

本目录包含《循环缓冲区》章节的所有代码示例。

## 说明

循环缓冲区（Circular Buffer / Ring Buffer）是一个古老但永不过时的数据结构。它有固定大小，装满了就从头再来。没有扩容、没有碎片、没有"new 失败"，非常适合 MCU、驱动、中断、DMA、串口、音频流等场景。

## 为什么嵌入式这么爱循环缓冲区？

- **固定大小，编译期或初始化时确定**
- **O(1) 入队 / 出队**
- **内存连续，Cache 友好**
- **不需要动态分配**
- **实现简单，容易做成 lock-free / 中断安全**

## 核心设计

使用"浪费一个元素"的策略来区分"满"和"空"：
- 缓冲区大小为 `N`
- 实际最多只能存 `N - 1` 个元素
- 空：`head == tail`
- 满：`(head + 1) % N == tail`

## 示例文件

| 文件 | 说明 | 关键点 |
|------|------|--------|
| `basic_example.cpp` | 基本用法演示 | push/pop 操作，状态查询 |
| `uart_example.cpp` | UART 串口接收场景 | ISR + 主循环通信 |
| `thread_safe_example.cpp` | 线程安全版本 | 原子操作，多线程场景 |

## 编译方法

```bash
mkdir build && cd build
cmake ..
make
```

## 运行示例

```bash
./basic_example
./uart_example
./thread_safe_example
```

## 环境要求

- C++17 或更高
- GCC 7+ 或 Clang 5+

## 嵌入式注意事项

1. 单生产者 + 单消费者场景通常是天然安全的（只要索引读写是原子的）
2. 多线程/多核/RTOS 场景需要：关中断、原子变量、mutex 或 spinlock
3. 在 ISR 中使用时要确保操作足够短
4. 对于 SMP 系统，需要使用原子操作确保可见性
