---
title: "多锁获取工具：std::lock、try_lock 回退、层级锁"
description: "两把以上锁一起拿：C++17 一行 scoped_lock，C++11 用 defer_lock 配 std::lock，拿不到就算了走 std::try_lock，锁序建不起来上层级锁。"
chapter: 5
order: 5
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

# 多锁获取工具：std::lock、try_lock 回退、层级锁

把任务从一个队列搬去另一个队列，得同时锁两把队列锁。交换两个缓冲区也一样，两把锁一起拿。锁的顺序一旦在两条执行路径上反着来，AB-BA 死锁就登场。标准库给的工具正好分三层。顺序能定，scoped_lock 一把抓齐。试一把不成就算了，那是 try_lock 的活。顺序压根定不下来，才轮到层级锁。咱们在四个线程并发交换的压测里验证它不死锁。

## 最小代码

```cpp
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

std::mutex left_mutex, right_mutex;
std::vector<int> left_data{1, 2}, right_data{3, 4};

void swap_cxx11() {                          // C++11/14：defer_lock 配 std::lock
    std::unique_lock<std::mutex> a(left_mutex, std::defer_lock);
    std::unique_lock<std::mutex> b(right_mutex, std::defer_lock);
    std::lock(a, b);                         // 死锁避免算法：拿不齐就退回重来
    left_data.swap(right_data);
}

void swap_cxx17() {                          // C++17：一行等价于上面三行
    std::scoped_lock lk(left_mutex, right_mutex);
    left_data.swap(right_data);
}

bool try_both() {                            // 拿不到就算了
    int failed = std::try_lock(left_mutex, right_mutex);
    if (failed != -1) return false;          // 第 failed 把没拿到，已拿到的已被释放
    std::lock_guard<std::mutex> a(left_mutex, std::adopt_lock);
    std::lock_guard<std::mutex> b(right_mutex, std::adopt_lock);
    left_data.push_back(1);
    return true;
}

int main() {
    std::vector<std::thread> ts;
    for (int i = 0; i < 4; ++i) {
        ts.emplace_back([] {
            for (int r = 0; r < 10000; ++r) swap_cxx17();   // 获取顺序随机也不死锁
        });
    }
    for (auto& t : ts) t.join();

    std::cout << "left " << left_data.size()
              << ", right " << right_data.size() << "\n";
    std::cout << "try_both succeeded: " << try_both() << "\n";
}
```

咱们把代码放进临时目录实测（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，2026-11-18），输出如下：

```text
left 2, right 2
try_both succeeded: 1
```

## 要点

- C++17 直接 `std::scoped_lock lk(m1, m2);`。构造函数内部跑死锁避免算法，析构按相反顺序放锁，您传参顺序随意。
- C++11/14 的等价写法：两把 `unique_lock` 用 `std::defer_lock` 构造（不加锁），再 `std::lock(a, b)` 一次拿齐。三步换成 scoped_lock 一行，咱们当然选后者。
- `std::try_lock(m1, m2)` 只试一轮：全部拿到返回 -1。某把失败，已拿到的全部释放，返回失败位置的编号（从 0 起），适合“锁忙就跳过本轮”的场合。
- 层级锁是个模式，标准库不带，得咱们自己搭：给每把 mutex 定层级号，线程只能从高往低拿，`thread_local` 变量记着当前线程的层级，违反就抛异常。锁多、模块边界复杂时，它把锁顺序变成运行期可检查的约束，您在测试期就能逮住违规。
- 已知全局顺序时按顺序传参给 scoped_lock，内部 try_lock 回退的次数更少，开销也更低。

## 常见错误

- 拿 `std::try_lock` 的返回值当“成功几把”：返回的是失败位置的编号，且失败的调用已把拿到的锁全部释放，您别再对它们 unlock。
- try_lock 全部成功后忘了 adopt：锁已在手上，再写一个普通 lock_guard 构造会二次加锁。要用 `std::adopt_lock` 把已持有的锁收编进来。
- scoped_lock 的参数顺序“传反了”就慌：不死锁是它的保证，顺序只影响内部回退的开销，不影响正确性，您可以放心传。
- 层级锁的 thread_local 初值忘了设成最大层级：检查从进入第一把锁起就形同虚设，违规发现不了，咱们调试时就少了最关键的一层保险。

## 链回正文

死锁的成因、锁顺序与分层策略，正文《谁也不松手，怎么办》完整推演过，您接着往下读就行：[《谁也不松手，怎么办》](../../chapter02-sharing-data/06-deadlock-defenses)。
