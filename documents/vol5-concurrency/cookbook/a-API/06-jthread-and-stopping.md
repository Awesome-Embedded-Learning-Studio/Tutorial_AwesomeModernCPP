---
title: "jthread 与停止协议速查"
description: "jthread 析构自动 request_stop 加 join，线程函数第一个参数收 stop_token 才有取消能力，stop_source、stop_token、stop_callback 三件套各管一段。"
chapter: 5
order: 6
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [20]
---

# jthread 与停止协议速查

要启动一个能被礼貌叫停的线程，C++20 的 `std::jthread` 加 stop_token 三件套是标准答案。咱们按启动、请求、回调、收尾的顺序过一遍接口，一段代码跑完全程。

## 最小代码

```cpp
#include <chrono>
#include <iostream>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;

int main() {
    std::jthread worker([](std::stop_token token) {   // 第一个参数收 token，jthread 自动传入
        int rounds = 0;
        std::stop_callback cleanup(token, [] {        // 停止请求一到就触发
            std::cout << "cleanup ran\n";
        });
        while (!token.stop_requested()) {
            ++rounds;
            std::this_thread::sleep_for(200ms);
        }
        std::cout << "worker exits after " << rounds << " rounds\n";
    });

    std::this_thread::sleep_for(1100ms);
    worker.request_stop();   // 不写这一行，析构也会自动做：request_stop 再 join
}
```

咱们把代码放进临时目录实测（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，2026-11-18），输出如下：

```text
cleanup ran
worker exits after 6 rounds
```

轮数与两行的出现顺序随机器调度浮动，worker 的睡眠时长与调度精度都会影响本轮次数。咱们真正能押住的是形状：request_stop 触发回调，worker 在检查点发现停止请求后退出，main 的 join 不再需要手写。

## 要点

- `std::jthread`（C++20，`<thread>`）析构时若还 joinable：执行 `request_stop()` 再 `join()`。比 std::thread 多送的正是停止请求这一半。
- 线程函数的第一个参数写 `std::stop_token`，jthread 自动把内部 token 传进去。不收 token 的函数也能跑，只是没有取消能力，您要取消就得收。
- 三件套共享一个停止状态：`stop_source` 是写端（`request_stop`），`stop_token` 是读端（`stop_requested`、`stop_possible`），`stop_callback` 注册回调。`request_stop()` 只有第一次调用返回 true。
- `stop_callback` 的回调在调用 request_stop 的线程上同步执行。注册时停止已发生的话，回调立即在该线程执行，所以回调里别做耗时的事，卡住的是提出停止的人。
- 一个 stop_source 可以派生多个 token，一份请求同时停多条线程。`t.get_stop_source()` 取写端，`t.get_stop_token()` 取读端。
- 正阻塞在睡眠或条件变量上的线程不会因为 request_stop 而醒来，除非等的接口收 stop_token（例如 condition_variable_any 的带 token 重载，正文[《等待也需要结束路径》](../../chapter03-wait-and-coordinate/06-ending-a-wait)处理过）。

## 常见错误

- 手写的 RAII 线程包装只 join 不 request_stop：线程在长睡眠里，析构就卡到它自然醒。您换 jthread，或者让线程函数收 token。
- 假设 request_stop 会让线程立刻退出：停止是协作式的，线程检查到 stop_requested 才停。所以忙循环每次迭代查一次，阻塞调用换成支持 token 的版本。
- 回调里做重活或者再拿锁：回调跑在请求方的执行流上，重活把提出停止的线程也拖住了，您等于自己堵自己的路。
- stop_callback 对象若在 stop_source 之前消亡：注册关系悬空。把 callback 放在被停线程的作用域里，析构自动注销，顺序自然正确。

## 链回正文

三件套的注册竞态与退出路径检查表，正文备全了。对应篇是《让工作有序结束：std::jthread 与协作式停止》，您点[链接](../../chapter01-start-concurrency/06-jthread-and-stopping) 过去。
