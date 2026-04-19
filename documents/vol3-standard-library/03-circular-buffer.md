---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: 高效循环缓冲区
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 6: RAII与智能指针'
reading_time_minutes: 6
tags:
- cpp-modern
- host
- intermediate
title: 循环缓冲区实现
---
# 嵌入式C++教程——循环缓冲区

在嵌入式世界里，有一类问题反复出现：**数据源不停地产生数据，消费者慢慢地处理数据，中间还不想 malloc。**于是，一个古老但永不过时的数据结构登场了——**循环缓冲区（Circular Buffer / Ring Buffer）**。

你可以把它理解为一个仓库，只有固定大小，装满了就从头再来。没有扩容、没有碎片、没有"new 失败"，非常适合 MCU、驱动、中断、DMA、串口、音频流等场景。

------

## 为什么嵌入式这么爱循环缓冲区？

在 PC 世界，我们可以随便 `new`、随便 `std::vector::push_back`。但在嵌入式里，这些操作听起来就很危险：

- 堆内存小，还容易碎片化
- 中断上下文里不能 malloc
- 实时系统里不希望出现不可控延迟

而循环缓冲区的特性，几乎是为嵌入式量身定做的：

- **固定大小，编译期或初始化时确定**
- **O(1) 入队 / 出队**
- **内存连续，Cache 友好**
- **不需要动态分配**
- **实现简单，容易做成 lock-free / 中断安全**

一句话总结：

> **它不聪明，但它很可靠。**

------

## 循环缓冲区的核心思想（其实非常朴素）

循环缓冲区本质上就是：

- 一块固定大小的数组
- 两个索引：
  - `head`：写入位置
  - `tail`：读取位置

当索引走到数组末尾，就**绕回开头**，像一个圆。

```cpp

[ 0 ][ 1 ][ 2 ][ 3 ][ 4 ][ 5 ]
        ↑         ↑
      tail      head

```

写入数据：移动 `head`
读取数据：移动 `tail`

重点只有一个问题需要想清楚：
👉 **如何区分"满"和"空"？**

------

## 如何区分"空"和"满"？（经典难题）

有三种常见方案：

1. **浪费一个元素（最常见）**
2. 额外维护一个 `count`
3. 用一个额外的 `full` 标志位

在嵌入式里，**方案 1 最受欢迎**：简单、无歧义、逻辑清晰。规则是：

- 缓冲区大小为 `N`
- 实际最多只能存 `N - 1` 个元素
- 条件判断：
  - 空：`head == tail`
  - 满：`(head + 1) % N == tail`

是的，我们牺牲了一个格子，换来一生的安宁。

------

## 一个干净的 C++ 循环缓冲区实现

下面是一个**无动态内存、模板化、适合嵌入式**的实现。

### 基本接口设计

```cpp
#pragma once
#include <cstddef>
#include <array>

template<typename T, std::size_t Capacity>
class RingBuffer {
public:
    bool push(const T& value);
    bool pop(T& out);

    bool empty() const;
    bool full() const;

    std::size_t size() const;
    std::size_t capacity() const { return Capacity - 1; }

private:
    std::array<T, Capacity> buffer_{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
};

```

注意一个细节：
👉 **`Capacity` 实际数组大小 = 用户可用容量 + 1**

------

## 入队（push）：向前走一步

```cpp
template<typename T, std::size_t Capacity>
bool RingBuffer<T, Capacity>::push(const T& value)
{
    if (full()) {
        return false;  // 缓冲区满了
    }

    buffer_[head_] = value;
    head_ = (head_ + 1) % Capacity;
    return true;
}

```

这里没有任何黑魔法：

- 先判断是否满
- 写数据
- 移动 `head`
- 如果走到末尾，绕回开头

**O(1)，永远不会慢。**

------

## 出队（pop）：消费者登场

```cpp
template<typename T, std::size_t Capacity>
bool RingBuffer<T, Capacity>::pop(T& out)
{
    if (empty()) {
        return false;  // 没数据
    }

    out = buffer_[tail_];
    tail_ = (tail_ + 1) % Capacity;
    return true;
}

```

同样简单：

- 空就失败
- 读数据
- 移动 `tail`

------

## 状态判断函数

```cpp
template<typename T, std::size_t Capacity>
bool RingBuffer<T, Capacity>::empty() const
{
    return head_ == tail_;
}

template<typename T, std::size_t Capacity>
bool RingBuffer<T, Capacity>::full() const
{
    return (head_ + 1) % Capacity == tail_;
}

template<typename T, std::size_t Capacity>
std::size_t RingBuffer<T, Capacity>::size() const
{
    if (head_ >= tail_) {
        return head_ - tail_;
    }
    return Capacity - (tail_ - head_);
}

```

`size()` 这个写法在嵌入式里很常见，
避免分支复杂化，也没有使用额外计数器。

------

## 一个真实的嵌入式使用场景

### 串口接收（ISR + 主循环）

```cpp
RingBuffer<uint8_t, 128> rx_buffer;

void USART_IRQHandler()
{
    uint8_t data = UART_Read();
    rx_buffer.push(data);  // 中断里只做这件事
}

int main()
{
    while (1) {
        uint8_t ch;
        if (rx_buffer.pop(ch)) {
            process_char(ch);
        }
    }
}

```

这种写法有几个非常嵌入式的优点：

- ISR 里逻辑极短
- 不 malloc
- 主循环慢慢处理
- 即使处理慢一点，也不会阻塞中断

------

## 关于线程安全 / 中断安全的一点现实提醒

上面的实现是：

- **单生产者 + 单消费者**
- 一个在中断，一个在主循环

在很多 MCU 上，这是**天然安全的**（只要索引读写是原子的）。

但如果你遇到下面情况之一：

- 多线程
- 多个生产者
- SMP
- RTOS 任务间通信

那你需要：

- 关中断
- 原子变量
- 或者 mutex / spinlock

------

## 和 std::queue / std::vector 比一比

| 方案        | 是否动态分配 | 是否确定性 | 嵌入式友好 |
| ----------- | ------------ | ---------- | ---------- |
| std::vector | 是           | 否         | ❌          |
| std::queue  | 取决于底层   | 否         | ❌          |
| 循环缓冲区  | 否           | 是         | ✅          |
