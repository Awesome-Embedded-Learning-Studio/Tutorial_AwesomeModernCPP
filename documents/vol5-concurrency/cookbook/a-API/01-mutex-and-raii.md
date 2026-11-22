---
title: "mutex 与 RAII 锁的选择：lock_guard、unique_lock、scoped_lock"
description: "按两个问题选 RAII 守卫：锁要不要中途放开，一次要拿几把。默认 lock_guard，中途解锁或配条件变量换 unique_lock，多锁同取交 scoped_lock。"
chapter: 5
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
  - mutex
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# mutex 与 RAII 锁的选择：lock_guard、unique_lock、scoped_lock

手里一块共享数据要加锁，三个 RAII 守卫摆在面前，您拿哪一个？选型只看两件事：锁要不要在中途放开，以及一次要拿几把。咱们把三种选择放进一段程序里编译跑一遍，再补要点与常见错误。

## 最小代码

```cpp
#include <iostream>
#include <mutex>
#include <vector>

std::mutex counter_mutex;
int counter = 0;

std::mutex buffer_mutex;
std::vector<int> buffer;

std::mutex account_a, account_b;

void bump() {
    std::lock_guard<std::mutex> lk(counter_mutex);  // 单锁、持到作用域结束
    ++counter;
}

void snapshot() {
    std::unique_lock<std::mutex> lk(buffer_mutex);  // 需要中途解锁
    std::vector<int> copy = buffer;
    lk.unlock();                                    // 临界区到此为止
    std::cout << "snapshot size " << copy.size() << "\n";
}

void move_funds() {
    std::scoped_lock lk(account_a, account_b);      // 一次安全拿两把（C++17）
    std::cout << "moved\n";
}

int main() {
    bump(); bump(); bump();
    {
        std::lock_guard<std::mutex> lk(buffer_mutex);
        buffer = {10, 20, 30};
    }
    snapshot();
    move_funds();
    std::cout << "counter " << counter << "\n";
}
```

咱们把代码放进临时目录实测（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，2026-11-18），输出如下：

```text
snapshot size 3
moved
counter 3
```

## 要点

- 默认选 `std::lock_guard`（C++11）。构造加锁、析构解锁，没有多余接口，您也就没有用错的空间。
- 锁要中途放开（缩小临界区）、要延迟加锁，或者要配 `std::condition_variable::wait`，那就换 `std::unique_lock`（C++11）。wait 系列的签名只收 `unique_lock<std::mutex>`，传 lock_guard 连编译都过不去。
- 一次要同时持有两把以上锁，咱们交给 `std::scoped_lock`（C++17）。构造函数内部跑死锁避免算法，参数顺序随意都不会死锁。
- 单锁场景建议留在 lock_guard。看到它您就知道只有一把锁，看到 scoped_lock 就得多想一步有没有第二把，读代码的人靠该差异省心。
- 三者都不可复制。不过 `unique_lock` 可移动，锁所有权能跨函数转移，lock_guard 与 scoped_lock 做不到。

## 常见错误

- 少写变量名有两种写法，麻烦各有各的。圆括号形态 `std::lock_guard<std::mutex>(mtx);` 会被解析成一条声明，GCC 16.2.1 直接编译报错，您连无锁运行都到不了。真静默的是花括号形态 `std::lock_guard<std::mutex>{mtx};`：无名临时对象构造完立刻析构解锁，后面的代码在无锁状态下跑，GCC 16.2.1 默认给 -Wunused-result，别的实现不一定。所以给锁对象起名字的习惯，咱们要养起来。
- 同一线程对已持有的 `std::mutex` 再调 `lock()` 是未定义行为（[thread.mutex.requirements.mutex]），多数实现表现为卡死。您真有重入需求，另见[recursive_mutex 与 timed_mutex 配方](02-recursive-and-timed-mutex)。
- 手写 `lock()` 与 `unlock()` 配对几乎必然漏路径，异常与提前 return 都会跳过 unlock。所以 C++ Core Guidelines 的 CP.20 与正文持同一立场：永远走 RAII。
- 给 `unique_lock` 传了 `std::defer_lock` 却忘了调 `lock()` 就进临界区，锁根本没加上，您等于在无锁状态下跑临界区。

## 链回正文

mutex 家族、锁与数据的对应关系、锁粒度，正文《丢掉的数怎么收回来？——std::mutex 与三种作用域锁》都替您铺好了：[《丢掉的数怎么收回来？——std::mutex 与三种作用域锁》](../../chapter02-sharing-data/02-mutex-and-locks)。
