---
title: "std::mutex"
description: "提供独占、非递归的所有权语义，用于保护共享数据免受多线程同时访问。"
chapter: 99
order: 3
tags:
  - host
  - mutex
  - beginner
difficulty: beginner
cpp_standard: [11, 14, 17, 20, 23]
---

# std::mutex（C++11）

## 一句话

最基础的互斥锁，同一时刻只允许一个线程持有，用于保护多线程间的共享数据。

## 头文件

`#include <mutex>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 构造 | `mutex()` | 构造互斥锁 |
| 析构 | `~mutex()` | 销毁互斥锁 |
| 加锁 | `void lock()` | 锁定互斥锁，若不可用则阻塞 |
| 尝试加锁 | `bool try_lock()` | 尝试锁定，若不可用立即返回 false |
| 解锁 | `void unlock()` | 解锁互斥锁 |
| 原生句柄 | `native_handle_type native_handle()` | 返回底层实现定义的原生句柄 |

## 最小示例

```cpp
#include <iostream>
#include <mutex>
#include <thread>

int counter = 0;
std::mutex m;

void increment() {
    std::lock_guard<std::mutex> lock(m);
    ++counter;
}

int main() {
    std::thread t1(increment);
    std::thread t2(increment);
    t1.join();
    t2.join();
    std::cout << counter << '\n'; // 输出: 2
}
```

## 嵌入式适用性：高

- 通常为零开销抽象，未竞争时仅产生原子操作开销
- 不可复制、不可移动，内存布局明确可控
- 建议搭配 `lock_guard` 使用，避免异常路径导致死锁
- 需注意：RTOS 环境需确保底层 pthread 或 OS 原语可用

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 3.3   | 2010 |

## 另见

- [教程：互斥锁与 RAII 守卫](../vol5-concurrency/04-mutex-and-raii-guards.md)
- [cppreference: std::mutex](https://en.cppreference.com/w/cpp/thread/mutex)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
