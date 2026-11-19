---
title: "shared_mutex 读写锁：什么时候值得，什么时候更慢"
description: "读用 shared_lock 写用 unique_lock。读者多、读得久、写得少才值得，低竞争短临界区反而更慢，标准不保证写者优先。"
chapter: 5
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
  - mutex
difficulty: intermediate
platform: host
cpp_standard: [17]
---

# shared_mutex 读写锁：什么时候值得，什么时候更慢

一份数据读的人多、写的人少。普通 mutex 让读和读互相排队，读写锁让读与读并行。`std::shared_mutex`（C++17）什么时候赚、什么时候亏，咱们用一段读写并发的程序交代，再给两条判断条件。

## 最小代码

```cpp
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class Config {
public:
    std::string get(const std::string& key) const {
        std::shared_lock lk(mutex_);              // 共享锁：多个读同时进来
        auto it = data_.find(key);
        return it == data_.end() ? std::string{} : it->second;
    }
    void set(const std::string& key, std::string value) {
        std::unique_lock lk(mutex_);              // 独占锁：一个写，等所有读退场
        data_[key] = std::move(value);
    }
private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> data_;
};

int main() {
    Config cfg;
    cfg.set("mode", "prod");

    std::vector<std::thread> ts;
    for (int i = 0; i < 8; ++i) {
        ts.emplace_back([&cfg] {                  // 8 个读线程并发查
            for (int r = 0; r < 10000; ++r) cfg.get("mode");
        });
    }
    ts.emplace_back([&cfg] {                      // 1 个写线程偶尔更新
        for (int r = 0; r < 100; ++r) cfg.set("mode", "prod");
    });
    for (auto& t : ts) t.join();

    std::cout << "mode = " << cfg.get("mode") << "\n";   // mode = prod
}
```

咱们把代码放进临时目录实测（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，2026-11-18），输出如下：

```text
mode = prod
```

咱们跑下来，8 个读线程并发查询期间写线程正常完成更新，无报错无死锁。

## 要点

- 读路径用 `std::shared_lock`（C++14），写路径用 `std::unique_lock` 或 lock_guard。多个 shared_lock 可以并存，独占锁在场上时其余请求不论读写都得等。
- `shared_mutex` 是 C++17 的。C++14 只有带超时接口的 `std::shared_timed_mutex`。不需要超时就选 shared_mutex，实现更轻，您付的开销也更小。
- 赚的条件：读者多、读的临界区长、写得少。读的临界区拉长，并读的收益才盖得过读写锁自身的管理开销（读者计数在多核之间弹跳）。
- 亏的条件：线程少、临界区短。锁内只有一次 load 的场合，普通 mutex 通常更快，所以切换之前拿真实负载测一轮。
- 标准不保证写者优先，也不保证调度公平：写者可能被连续到来的读者饿住（写饥饿）。写的实时性要紧的场景，您别把唯一的写路径押在 shared_mutex 上。

## 常见错误

- 同一线程在已持锁时再次加锁（含共享锁递归）：shared_mutex 在同一线程同一时间只允许持有一把，shared 或 exclusive 二选一，违反是未定义行为，标准不会替您兜住。
- 想把 shared_lock 原地升级成独占锁：两个持共享锁的线程同时等对方放锁，互不放行，死锁成型。标准做法是放掉共享锁再拿独占锁，中间的变化靠重新检查兜住。
- 读到的数据是指针或引用，出了 shared_lock 作用域还在用：写线程随后一改，您手里就是悬空的。
- 拿“读多写少”四个字当充分条件：读者只有两个、读的动作一眨眼就完的场合，换锁只慢不加，咱们切换之前还是得测。

## 链回正文

shared_lock 配条件变量的等待，正文讲过 condition_variable_any。您翻到正文：《等待一个条件成立》（[链接](../../chapter03-wait-and-coordinate/01-wait-for-a-condition)）。
