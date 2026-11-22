---
title: "atomic_signal_fence"
description: "同一线程里普通代码与信号处理器之间的排序屏障，只压编译器重排，不发 CPU 指令。线程间同步请用 atomic_thread_fence。"
chapter: 5
order: 9
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
  - atomic
difficulty: intermediate
platform: host
cpp_standard: [11]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# atomic_signal_fence

信号处理器会在执行流的任意两条指令之间插进来。您想让某个普通写保证在处理器读之前完成，可处理器与主流程跑在同一条执行流上，CPU 层面本来就一致，要防的只有编译器把指令挪位。`std::atomic_signal_fence`（C++11）就是为此准备的：一条编译器屏障，CPU 一条指令都不发。

## 最小代码

```cpp
#include <atomic>
#include <csignal>
#include <iostream>

std::atomic<bool> notified{false};
volatile std::sig_atomic_t snapshot = 0;
int payload = 0;                          // 普通变量

void handler(int) {
    if (notified.load(std::memory_order_relaxed)) {
        std::atomic_signal_fence(std::memory_order_acquire);  // 下面的读不许提前
        snapshot = payload;               // volatile sig_atomic_t：信号处理器里安全的写
    }
}

int main() {
    std::signal(SIGUSR1, handler);

    payload = 42;                            // 普通写
    std::atomic_signal_fence(std::memory_order_release);  // 它不许挪到通知之后
    notified.store(true, std::memory_order_relaxed);

    std::raise(SIGUSR1);                     // handler 在本线程执行完才返回
    std::cout << "snapshot " << snapshot << "\n";   // snapshot 42
}
```

咱们把代码放进临时目录实测（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，2026-11-18），输出如下：

```text
snapshot 42
```

咱们演示用 `std::raise` 同步发信号，handler 与主流程在同一条执行流上按顺序执行。异步信号的场景里 handler 会在任意指令边界插进来，两条 fence 拦住的正是编译器挪位的自由度。

## 要点

- `std::atomic_signal_fence(order)`（C++11，`<atomic>`）建立同一线程内普通代码与信号处理器之间的排序。语义对应 `atomic_thread_fence`，区别是不发任何 CPU 指令，只拦编译器重排（cppreference 页面原文如此）。
- 两端形状对称：写端是普通写、signal_fence(release)、relaxed 的 atomic 写，处理器读端是 relaxed 读、signal_fence(acquire)、读普通数据。少挂一端排序就断，所以两端您都要挂。
- 线程间同步用它无效：不同线程跑在不同核上，缺的 CPU 屏障它不给。跨线程排序请用 `atomic_thread_fence` 或带内存序的原子操作。
- 信号处理器里能安全碰的对象有限：`volatile std::sig_atomic_t`、无锁的原子对象，外加 atomic_signal_fence 本身。普通对象在处理器里的值，标准只给 unspecified（[intro.execution]，C++11 条款，现行草案已挪节）。演示里读 payload 靠 fence 排序加实测兜底，工程代码您别依赖它。

## 常见错误

- 拿 atomic_signal_fence 做两个线程之间的同步：一条 CPU 指令都不发，弱内存序架构上另一个线程根本看不到您的写。
- 信号处理器里调 iostream、malloc 这类非异步信号安全的函数：handler 可能恰好在 malloc 中途插进来，锁在手上，程序直接卡死。
- 只挂一端 fence：写端 release、读端 acquire 要成对出场，哪一端漏了，编译器都有权挪位，您的排序就是纸糊的。
- 把 fence 当成作用于某个变量的操作：它标记的是执行流上的位置，不接收对象参数，“对哪个变量 fence”并没有对应的机制。

## 链回正文

fence 的三种接法、x86 指令对照与 volatile 边界，正文有一节等着您：《内存序的四档：从顺序一致性到 relaxed》（[链接](../../chapter05-atomic-memory-model/04-choosing-memory-orders)）。
