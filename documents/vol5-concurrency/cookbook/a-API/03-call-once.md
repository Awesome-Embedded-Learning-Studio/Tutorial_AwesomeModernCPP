---
title: "call_once 与 once_flag，以及和 Meyers singleton 的取舍"
description: "call_once 保证可调用对象只执行一次且失败可重试。能写成块级 static 的初始化就选 Meyers singleton，控制粒度的事才交给 call_once。"
chapter: 5
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
  - mutex
difficulty: intermediate
platform: host
cpp_standard: [11]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# call_once 与 once_flag，以及和 Meyers singleton 的取舍

一段初始化代码，多线程并发达时必须只跑一次。写法有两种：`std::call_once` 配 `std::once_flag`，或者函数内 `static` 局部变量（Meyers singleton）。咱们看 call_once 的形状与失败重试的行为，然后定一个选择标准。

## 最小代码

```cpp
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

std::once_flag init_flag;
int attempts = 0;
const int* resource = nullptr;

void ensure_ready() {                       // 失败不置位，下一次调用会重试
    std::call_once(init_flag, [] {
        ++attempts;
        if (attempts < 2) throw std::runtime_error("config not ready");
        resource = new int(42);
    });
}

int main() {
    try {
        ensure_ready();                     // 第一次调用：抛异常，标志不置完成
    } catch (const std::exception& e) {
        std::cout << "failed: " << e.what() << "\n";
    }
    ensure_ready();                         // 第二次调用：重试成功
    std::cout << "attempts " << attempts << ", value " << *resource << "\n";

    std::vector<std::thread> ts;            // 已完成之后，并发的调用全部直接返回
    for (int i = 0; i < 4; ++i) {
        ts.emplace_back([] { ensure_ready(); });
    }
    for (auto& t : ts) t.join();
}
```

咱们把代码放进临时目录实测（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，2026-11-18），输出如下：

```text
failed: config not ready
attempts 2, value 42
```

## 要点

- Meyers singleton：函数内 `static` 局部变量的初始化从 C++11 起线程安全（[stmt.dcl]：并发首次经过声明时，其余线程等待初始化完成）。初始化简单、结果就是一个对象，您用它，代码最少。
- `std::call_once`（C++11）保证：多线程同时调用时，可调用对象只执行一次，其余线程等它结束。`once_flag` 记录完成状态，不可复制、不可移动。
- 初始化函数抛异常时，`once_flag` 不进入完成状态，异常传播给发起调用的线程，下一次 call_once 会重新尝试。函数内 static 的初始化抛异常同样视为未完成、下次进入声明再试，所以可重试并不构成两者的区别。
- 真正的取舍在控制粒度：初始化要在您指定的时机显式触发、要打包一组步骤、结果要放进任意位置而不必绑定在一个对象的构造上，就轮到 call_once 出场。
- 两者对“第一次经过的人”都收全额初始化时间。初始化贵又想预热，咱们用 call_once 在 main 里显式触发，比在首次调用时突然执行初始化更稳。

## 常见错误

- 初始化函数执行期间又对同一个 flag 调 call_once（嵌套进入）：嵌套调用要等外层执行完成，线程就此挂住，这是活性问题，不算未定义行为（[thread.once.callonce]）。函数内 static 的递归进入才是未定义行为（[stmt.dcl]）。所以初始化函数里别回头调自己。
- 把完成状态当成“初始化成功”：异常路径 flag 不置位，依赖初始化结果的代码要自己处理“还没成功”的分支，您别只等标志位。
- 以为两者会各自执行一次初始化：`once_flag` 与 static 变量各管各的完成状态。同一个初始化动作要是混用两种机制，就可能执行两遍，咱们选一种贯彻到底。

## 链回正文

Meyers singleton 与双重检查锁定的取舍，正文写得完整。您移步正文的双重检查锁定（DCLP）一节，就在[《内存序的四档：从顺序一致性到 relaxed》](../../chapter05-atomic-memory-model/04-choosing-memory-orders) 里。
