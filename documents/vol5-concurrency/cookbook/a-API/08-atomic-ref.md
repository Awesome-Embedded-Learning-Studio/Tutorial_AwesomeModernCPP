---
title: "atomic_ref：给非原子变量套原子操作"
description: "atomic_ref 不拥有数据，引用期内对象必须排他地走原子路径：生命周期要盖过引用，对齐要达标，普通读写不能混入。"
chapter: 5
order: 8
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

# atomic_ref：给非原子变量套原子操作

变量的类型已经定死（第三方库的数组、要对接 C 接口的结构体），改不成 `std::atomic`，并发访问又确实需要原子操作。C++20 的 `std::atomic_ref` 正是为此准备的：对象不动，把访问路径临时换成原子的。咱们用四个计数器并发自增验证。

## 最小代码

```cpp
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    std::vector<int> counters(4, 0);               // 普通 int，运行时才决定要原子加

    std::vector<std::thread> ts;
    for (int i = 0; i < 4; ++i) {
        ts.emplace_back([&counters, i] {
            std::atomic_ref<int> ref(counters[i]);  // 不拥有数据，只是一个原子视图
            for (int n = 0; n < 100000; ++n) {
                ref.fetch_add(1, std::memory_order_relaxed);
            }
        });                                         // ref 在线程函数结束时销毁
    }
    for (auto& t : ts) t.join();

    for (int v : counters) std::cout << v << " ";   // 100000 100000 100000 100000
    std::cout << "\n";
}
```

咱们把代码放进临时目录实测（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，2026-11-18），输出如下：

```text
100000 100000 100000 100000
```

## 要点

- `std::atomic_ref<T>`（C++20，`<atomic>`）要求 T 是 trivially copyable。接口与 `std::atomic<T>` 基本一致：load、store、exchange、compare_exchange、fetch_add，内存序参数同款。
- 原子性在引用期内成立：指向同一对象的多个 atomic_ref 上的原子操作互为原子。对象生命周期必须盖过所有 atomic_ref 实例，您别让引用活得比对象久。
- 两条硬约束，违反即未定义行为：atomic_ref 存在期间，对象必须排他地经由 atomic_ref 实例访问，普通读写与原子路径不能混。同一对象内部，两个 atomic_ref 也不许并发引用重叠的子对象。
- 对齐：静态常量 `std::atomic_ref<T>::required_alignment` 给出最低对齐要求。连续数组按 sizeof(T) 排布的整型元素天然满足，裸内存里 cast 出来的对象要您自己保证。
- 它适合统计数组按槽位原子加、事后才决定某成员要原子访问的场面。一个变量从头就要原子且类型可改，那咱们直接声明 `std::atomic` 更省心。

## 常见错误

- 一个线程走 atomic_ref，另一个线程直接读原始变量：混路径是数据竞争，原子性只在“全走原子”时成立，您别留普通读写的后门。
- 对齐不达标：某些平台上 64 位原子操作需要 8 字节对齐，packed 结构体的成员要 `alignas` 补齐再建 atomic_ref。
- atomic_ref 活得比对象久：比如把引用缓存进更长命的容器，对象一销毁就是悬垂引用，您的程序会在释放后的随机位置出事。
- 期望 const atomic_ref 只读：constness 是浅层的，const atomic_ref 照样能改被引用的值，您别被类型骗了。

## 链回正文

原子类型的操作面，浮点与自定义类型的边界，正文《原子操作解决到哪一步》都铺好了，您移步：[《原子操作解决到哪一步》](../../chapter05-atomic-memory-model/01-what-atomics-solve)。
