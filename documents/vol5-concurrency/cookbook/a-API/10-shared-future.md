---
title: "shared_future"
description: "share() 转换，副本可拷贝，get 对类类型返回 const 引用可反复调用。多线程访问同一共享状态，安全前提是各自持有副本。"
chapter: 5
order: 10
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 14]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# shared_future

一个初始化任务的结果，好几个线程都在等。`std::future` 的 get 只能调一次，第一个调完其他人都没了着落。`std::shared_future`（C++11）把“等同一个结果”变成可拷贝、可重复取的形状。代码里的 `std::promise` 是 future 的写端，本篇只拿它当发令枪。咱们跑一个三线程同时取配置的广播程序。

## 最小代码

```cpp
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

int main() {
    std::promise<std::string> gate;
    std::shared_future<std::string> ready = gate.get_future().share();  // 原来的 future 随之失效

    std::vector<std::thread> ts;
    std::atomic<int> ok{0};
    for (int id = 0; id < 3; ++id) {
        ts.emplace_back([ready, &ok] {          // 每个线程带走一份自己的副本
            if (ready.get() == "mode=prod") {   // get 对类类型返回 const 引用，可反复调用
                ok.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::this_thread::sleep_for(100ms);
    gate.set_value("mode=prod");               // 一次 set_value，三个等待者全部放行

    for (auto& t : ts) t.join();
    std::cout << "workers got the value: " << ok.load() << "/3\n";
}
```

咱们把代码放进临时目录实测（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，2026-11-18），输出如下：

```text
workers got the value: 3/3
```

## 要点

- `std::future::share()` 把 future 转成 shared_future，原 future 失效。shared_future 可拷贝，每份副本指向同一个共享状态。
- get() 对类类型返回 const 引用且不消耗状态，同一副本可以反复调。异常同样成立：`set_exception` 存进去的异常在每个副本的 get 里原样重抛，您不用另设错误通道。
- cppreference 在 shared_future 页面写明的安全条款：多线程访问同一共享状态，安全的前提是每个线程通过自己的副本访问。所以咱们让每个线程持一份拷贝，别共享同一个对象。
- 它是一次性广播：set_value 只能一次。比条件变量省掉锁、谓词与虚假唤醒的处理，代价是第二把广播没有位置。
- 取舍参照：结果本身要传值，您用 shared_future。只是放行信号且场景会重复发生，latch 或条件变量更合适。

## 常见错误

- 多个线程并发调用同一个 shared_future 对象的 get：标准只担保各自持副本的访问，同对象并发访问不在担保内。咱们拷贝分发给各线程，一份一份带走。
- 生产者路径失败时不调 set_exception 就结束：等待者永远等不到结果，挂死。所以失败也要落到共享状态里，等待者才有个说法。
- 按 std::future 的习惯拷贝传参：std::future 只能移动，编译报错。要分发给多个线程，您 share() 之后再拷贝。
- 把 shared_future 当可反复 set 的通知器：写端第二次 set_value 直接抛 `std::future_error`，您得换一个 promise 重建结果链。

## 链回正文

shared_future 的正文落点与 future 的一次性语义，正文都写了。您去看它：《用 std::async 执行计算》（[链接](../../chapter04-organizing-tasks/01-launch-with-async)）。
