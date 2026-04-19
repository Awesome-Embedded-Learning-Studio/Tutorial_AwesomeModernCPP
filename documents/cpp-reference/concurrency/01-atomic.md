---
title: "std::atomic"
description: "无锁原子操作类型，用于多线程间无数据竞争的安全数据共享"
chapter: 99
order: 1
tags:
  - host
  - atomic
  - intermediate
difficulty: intermediate
cpp_standard: [11, 14, 17, 20, 23]
---

# std::atomic（C++11）

## 一句话

一种保证读写操作不可分割的模板类，让多线程并发访问同一变量时不会出现数据竞争。

## 头文件

`#include <atomic>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 构造 | `atomic() noexcept = default;` | 默认构造（值未初始化） |
| 赋值 | `T operator=(T desired) noexcept;` | 原子写入选定值 |
| 读取 | `operator T() const noexcept;` | 原子读取并返回当前值 |
| 存储 | `void store(T desired, memory_order order = memory_order_seq_cst) noexcept;` | 原子写入 |
| 加载 | `T load(memory_order order = memory_order_seq_cst) const noexcept;` | 原子读取 |
| 交换 | `T exchange(T desired, memory_order order = memory_order_seq_cst) noexcept;` | 原子替换旧值并返回旧值 |
| 比较交换 | `bool compare_exchange_weak(T& expected, T desired, ...) noexcept;` | 弱 CAS，可能伪失败 |
| 比较交换 | `bool compare_exchange_strong(T& expected, T desired, ...) noexcept;` | 强 CAS，仅在真正不匹配时失败 |
| 原子加 | `T fetch_add(T arg, memory_order order = memory_order_seq_cst) noexcept;` | 原子加并返回旧值（整型/指针） |
| 无锁检查 | `bool is_lock_free() const noexcept;` | 判断当前类型是否无锁实现 |

## 最小示例

```cpp
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

std::atomic<int> cnt{0};

int main() {
    std::vector<std::jthread> pool;
    for (int i = 0; i < 10; ++i)
        pool.emplace_back([] { for (int n = 0; n < 10000; ++n) cnt++; });
    std::cout << cnt << '\n'; // 输出 100000
}
```

## 嵌入式适用性：高

- 对齐正确的整型和指针类型通常直接映射为硬件原子指令，零额外开销
- `is_lock_free()` 可在运行时确认是否真正无锁，避免隐式系统调用
- 替代笨重的互斥锁，适合中断与主循环间的轻量级状态同步
- 过大的自定义结构体可能退化为内部加锁实现，需重点规避

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 3.1 | 19.0 |

## 另见

- [教程：对应章节](../../vol5-concurrency/01-atomic.md)
- [cppreference: std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
