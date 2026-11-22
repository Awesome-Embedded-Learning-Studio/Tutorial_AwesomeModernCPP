---
title: "atomic_wait 与 atomic_notify"
description: "wait(old) 比较的是旧值而不是目标值，标准允许伪唤醒。值更新之后才 notify，自由函数 atomic_wait 与成员 wait 等价。"
chapter: 5
order: 7
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
  - atomic
difficulty: intermediate
platform: host
cpp_standard: [20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# atomic_wait 与 atomic_notify

忙等一个 atomic 标志烧 CPU，mutex 加条件变量又嫌重。C++20 给原子变量装上了等待与通知：值没变就挂起，通知一到就醒。咱们跑一段主线程等工作线程完成初始化的程序，成员函数与自由函数两种形状都在里面。

## 最小代码

```cpp
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

std::atomic<int> state{0};

int main() {
    std::thread producer([] {
        std::this_thread::sleep_for(100ms);
        state.store(42, std::memory_order_release);
        state.notify_one();                    // 值更新之后才通知
    });

    state.wait(0, std::memory_order_acquire);  // 还是 0 就挂起；不再是 0 立即返回
    std::cout << "got " << state.load() << "\n";

    producer.join();
}
```

咱们把代码放进临时目录实测（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，2026-11-18），输出如下：

```text
got 42
```

## 要点

- `state.wait(old)`（C++20）的语义：原子读当前值，等于 old 就挂起，不等于就立即返回。参数是“我上次看到的旧值”，不是“我想等的目标值”。
- `notify_one` 与 `notify_all` 唤醒在该原子对象上等待的线程。标准允许伪唤醒，稳妥的形状是 `while (flag.load(acquire) == 未就绪值) flag.wait(...)`，醒来复核一次，咱们不赌运气。
- 自由函数 `std::atomic_wait(&obj, old)`、`std::atomic_notify_one(&obj)`（C++20，`<atomic>`）与成员函数等价，泛型代码里您用得上。
- Linux 上映射 futex（内核在用户态地址上挂起与唤醒线程的接口），Windows 上映射 WaitOnAddress。没有这类原语的实现回退到 mutex 加条件变量，接口一致，挂起的收益归零。
- 它只解决“等一个值变化”。等待没有超时版本，多变量条件与限时等待还是条件变量的地盘。

## 常见错误

- 把 wait 的参数当目标值传：想等 true 却写 `flag.wait(true)`，当前已是 true 时不等直接过，当前是 false 时等的是“不再等于 false”。所以想等 true，您传 false。
- notify 放在 store 之前：等待者被唤醒后读到的还是旧值，只能再睡回去等下一轮。值变了再通知，顺序别反。
- 正确性押在“醒几次”上：notify_one 可能没唤醒任何人（没人等着），伪唤醒也可能多醒一次。咱们把押注点放在值的复核上，而不是通知的次数上。

## 链回正文

wait 与 notify 在忙等与挂起之间的取舍，还有自旋锁的边界，正文《原子操作解决到哪一步》都讲过，您往下接着读：[《原子操作解决到哪一步》](../../chapter05-atomic-memory-model/01-what-atomics-solve)。
