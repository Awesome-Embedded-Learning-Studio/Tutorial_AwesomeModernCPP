---
title: "recursive_mutex 与 timed_mutex"
description: "同线程重入用 recursive_mutex 的计数语义，加锁要限时用 timed_mutex 的 try_lock_for/until。两者都是备选项，默认还是普通 mutex。"
chapter: 5
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
  - mutex
difficulty: intermediate
platform: host
cpp_standard: [11]
---

# recursive_mutex 与 timed_mutex

两个变体各管一件事：递归调用链里同一把锁要被同一线程加两次，归 `std::recursive_mutex`。加锁不想无限等下去，归 `std::timed_mutex`。咱们各跑一段最小代码，再看为什么我不建议把它们当默认选择。

## 最小代码

```cpp
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

std::recursive_mutex walk_mutex;
std::timed_mutex log_mutex;

int walk(int n) {
    std::lock_guard<std::recursive_mutex> lk(walk_mutex);  // 每层各加一次，计数递增
    if (n == 0) return 0;
    return n + walk(n - 1);                                // 同一线程再次加锁，合法
}

int main() {
    std::cout << "walk(5) = " << walk(5) << "\n";          // walk(5) = 15

    log_mutex.lock();                                      // 主线程占住不放
    std::thread waiter([] {
        if (log_mutex.try_lock_for(std::chrono::milliseconds(100))) {
            std::cout << "got the lock\n";
            log_mutex.unlock();
        } else {
            std::cout << "timed out\n";                    // 100ms 内没等到
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    log_mutex.unlock();                                    // 放得太迟，等待方已经超时
    waiter.join();
}
```

咱们把代码放进临时目录实测（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，2026-11-18），输出如下：

```text
walk(5) = 15
timed out
```

## 要点

- `recursive_mutex` 内部记一个层数：同一线程每次 lock 层数加一，每次 unlock 减一，减到零才真正放锁。其他线程在此期间依旧进不来，您不用担心它们搭便车。
- `timed_mutex` 比 mutex 多 `try_lock_for`（等一段时长）与 `try_lock_until`（等到一个时刻），到点拿不到就返回 false，您的程序走自己的降级路径。
- `std::recursive_timed_mutex`（C++11）是两者的合体，实际工程出场率很低，咱们知道存在即可。
- 递归加锁通常在报一个设计问题：接口把“持锁才该调的逻辑”与“不持锁的内部实现”混在了同一层。更稳的改法是把无锁的内部实现分出来，外层接口负责加锁。
- 带超时的锁要与系统时钟打交道，开销通常高于普通 mutex。所以没有超时需求就用 mutex，别为“万一能用上”多付钱。

## 常见错误

- 把 `std::mutex` 换成 `recursive_mutex` 来“修”死锁：死锁是锁职责混乱的症状，换锁只让程序不再卡死，数据竞争与逻辑错误原样留在原地等您。
- unlock 少调一次，层数没归零，锁就一直不放，其他线程只能全部排队，您排查时看到的就是一群线程卡在同一把锁上。
- 对 `try_lock_for` 的返回值不做分支处理就当加锁成功：返回 false 时临界区根本不在您手里。
- 同一线程对 `recursive_mutex` 的 lock 与 unlock 必须严格配平，把持有权跨线程转移没有定义，您别指望标准替您兜住。

## 链回正文

正文表外专门交代过两个变体的去处，说好的配方就在这里，您对照着读：《用互斥保护共享状态》（[链接](../../chapter02-sharing-data/02-mutex-and-locks)）。
