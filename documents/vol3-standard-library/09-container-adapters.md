---
title: "容器适配器：stack、queue、priority_queue 是怎么「包」出来的"
description: "讲透三个容器适配器：它们不是新容器，而是给底层容器套上受限接口拼出 LIFO/FIFO/堆语义；priority_queue 的本质是底层容器加 std::push_heap/pop_heap，默认最大堆、换比较器变最小堆，外加 C++23 的 push_range"
chapter: 7
order: 9
tags:
  - host
  - cpp-modern
  - intermediate
  - 容器
difficulty: intermediate
platform: host
cpp_standard: [11, 20, 23]
reading_time_minutes: 13
prerequisites:
  - "vector 深入：三指针、扩容与迭代器失效"
  - "deque、list 与 forward_list：vector 之外的三个选择"
related:
  - "容器选择指南：按操作、内存与失效规则挑对容器"
---

# 容器适配器：stack、queue、priority_queue 是怎么「包」出来的

## 适配器不是容器：是给底层容器套个受限外壳

`stack`、`queue`、`priority_queue` 这三个，标准里叫**容器适配器（container adaptor）**，不是独立容器。区别在于：一个真正的容器（比如 `vector`、`deque`）自己持有数据、自己决定存储方式；而适配器自己不发明存储，它**持有一个底层容器（underlying container）**，然后在它外面套一层受限的接口，只让你按某一种特定方式（栈、队列、优先队列）去访问数据。

这个「受限」是关键，也是适配器存在的理由。`std::stack` 只暴露 `top`/`push`/`pop`，全部发生在同一端，物理上不可能从中间偷一个元素出来——这就把「后进先出」从约定变成了结构保证，编译器层面就帮你拦住了误用。同理 `queue` 保证先进先出、`priority_queue` 保证你永远拿到当前最优先的那个。代价是你失去了随意访问的能力，但换回来的是「拿到的元素类型可预测、接口不会被滥用」。所以选不选用适配器，本质是问自己：**我是不是只想用这一种访问模式，并希望类型系统替我挡住其它操作？**

## stack 与 queue：用末端的几个操作拼出 LIFO/FIFO

适配器的接口就是底层容器几个操作的重新命名。`std::stack` 是后进先出，`push` 把元素压到末端、`top` 看末端、`pop` 弹末端——三个动作全发生在容器的 `back` 这一端，所以它对底层容器的要求是 `back()` / `push_back()` / `pop_back()`。`std::queue` 是先进先出，`push` 从 `back` 进、`front()`/`pop` 从 `front` 出，于是它额外要求底层容器有 `front()` 和 `pop_front()`。

| 适配器 | 语义 | 要求底层容器支持 | 默认底层 |
|--------|------|----------------|---------|
| `stack` | LIFO | `back`、`push_back`、`pop_back` | `deque` |
| `queue` | FIFO | `front`、`back`、`push_back`、`pop_front` | `deque` |
| `priority_queue` | 优先级 | `front`、`push_back`、`pop_back` + **随机访问迭代器** | `vector` |

默认底层为什么是 `deque`？因为它两端插删都是 O(1)，对 stack（只用 back）和 queue（用 front+back）都刚好满足，而且 `deque` 没有 vector 那种扩容时整块搬移的代价。这里有个反直觉的点值得记一下：**`std::queue` 没法用 `vector` 当底层**，因为 vector 没有 `pop_front`——要从 vector 头部弹出只能 erase(begin())，那是 O(n) 且标准库压根没提供这个成员，硬塞会编译失败。要给 queue 换底层，合法选择只有 `deque` 和 `list`。`stack` 就宽裕得多，`vector`/`deque`/`list` 都行，因为三个要求它都满足。

## priority_queue：底层容器加堆算法，这才是重点

三个适配器里 `priority_queue` 最值得拆，因为它的实现最能体现「适配器 = 底层容器 + 标准库算法」这个套路。它根本不是什么神秘数据结构，本质就是「一个连续容器 + `<algorithm>` 里的几个堆函数」——具体说，`push` 等价于 `c.push_back(x)` 然后 `std::push_heap(c.begin(), c.end(), cmp)`；`pop` 等价于 `std::pop_heap(c.begin(), c.end(), cmp)` 然后 `c.pop_back()`；`top` 就是返回 `c.front()`。堆算法维护的「堆序」保证 `c.front()` 永远是当前最优先的元素。

复杂度全可以从这个实现推出来。`top()` 直接读首元素，O(1)。`push()` 末尾追加是常数，`push_heap` 把新元素往上浮，最多爬树高 `log n` 层，所以是 O(log n)。`pop()` 里 `pop_heap` 先把首元素和末尾交换、再把新的首元素往下沉，同样最多 `log n` 层，加上一次 `pop_back`，整体 O(log n)。这也解释了为什么 `priority_queue` 的底层**必须是随机访问迭代器**的容器——堆的下沉上浮要在数组里按下标跳着访问（父节点 `i`、孩子 `2i+1`/`2i+2`），链表做不了这种 O(1) 定位，所以底层只能选 `vector` 或 `deque`，默认是 `vector`（连续内存，cache 友好，堆操作更快）。

默认比较器是 `std::less`，结果是个**最大堆**——`top()` 返回的是当前最大值。想要最小堆，把比较器换成 `std::greater` 就行。这个「换比较器改变堆方向」的特性，是 priority_queue 最常用的玩法。

## 跑跑看：默认最大堆，换个比较器变最小堆

光说「默认最大堆」不够实在，我们跑一下看 `top` 到底是谁。

```cpp
#include <cstdio>
#include <functional>
#include <queue>
#include <vector>

int main()
{
    // 默认：vector + less = 最大堆，top() 返回最大值
    std::priority_queue<int> pq;
    for (int x : {5, 1, 9, 3, 7}) {
        pq.push(x);
    }
    std::printf("默认（最大堆）依次 pop: ");
    while (!pq.empty()) {
        std::printf("%d ", pq.top());
        pq.pop();
    }
    std::printf("\n");

    // 换 greater = 最小堆，top() 返回最小值
    std::priority_queue<int, std::vector<int>, std::greater<int>> min_pq;
    for (int x : {5, 1, 9, 3, 7}) {
        min_pq.push(x);
    }
    std::printf("greater（最小堆）依次 pop: ");
    while (!min_pq.empty()) {
        std::printf("%d ", min_pq.top());
        min_pq.pop();
    }
    std::printf("\n");
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/pq_demo /tmp/pq_demo.cpp && /tmp/pq_demo
```

```text
默认（最大堆）依次 pop: 9 7 5 3 1
greater（最小堆）依次 pop: 1 3 5 7 9
```

同一个数据集，默认把最大的 9 顶到堆顶，换 `greater` 后最小的 1 顶上来。注意 pop 出来的顺序是**有序的**——这其实就是堆排序的过程，priority_queue 每次 pop 吐出当前最值，连续 pop 到空就得到一个有序序列。也正因为底层就是堆，priority_queue 经常被当成「在线堆排序」用：边 push 边能随时拿到当前最值，`top()` O(1)、增删 O(log n)，是很多算法（Dijkstra、合并 k 个有序序列、Top-K）的主力结构。

## C++23 的小升级：push_range，一次压一整段

C++23 给三个适配器都加了 `push_range`，可以一次压入一个范围。对 `stack`/`queue` 它就是循环 `push` 的语法糖，但对 `priority_queue` 它有实打实的复杂度优势，值得单独说。

原因是 priority_queue 维护堆序是有代价的。如果你拿一个 N 元素的范围，循环 `push` N 次，每次 `push_heap` 是 O(log n)，总共是 O(n log n)；而 `push_range` 的做法是先把整个范围一次性追加到底层容器（`append_range`，O(n)），再对整体做一次 `make_heap`（也是 O(n)），总共只有 O(n)。元素量大的时候，这个差距很明显。

```cpp
#include <queue>
#include <vector>

int main()
{
    std::vector<int> data{5, 1, 9, 3, 7, 2, 8, 4, 6, 0};
    std::priority_queue<int> pq;

#if __cplusplus >= 202302L
    pq.push_range(data);   // C++23：整体 append_range + make_heap，O(n)
#else
    for (int x : data) {   // C++20 退路：循环 push，O(n log n)
        pq.push(x);
    }
#endif
    return 0;
}
```

需要 C++23 的库支持（较新的 libstdc++/libc++），编译时 `-std=c++23`。老环境退回到循环 push 即可，行为一致，只是量大时慢一些。

## 挑底层容器的门道

绝大多数时候用默认值就好——`stack`/`queue` 用 `deque`、`priority_queue` 用 `vector`，都是委员会挑过的最优默认。要换，通常是为了两个目的之一。一个是 `priority_queue` 想避免默认的 `vector` 扩容拷贝，可以预留给底层 vector——但适配器没直接暴露 `reserve`，得自己先构造好底层容器再 move 进去（`std::priority_queue<int> pq{less{}, my_reserved_vector}`）。另一个是元素类型对 `vector` 不友好（比如非常大、移动昂贵），那 `priority_queue` 可以换 `deque` 当底层。`stack`/`queue` 换底层的场景更少，除非你明确要省内存（用 `list` 避免预分配），否则 `deque` 默认就挺好。

```cpp
// 给 priority_queue 预留容量：先 reserve 底层 vector，再 move 进去
std::vector<int> buf;
buf.reserve(10'000);
std::priority_queue<int> pq{std::less<int>{}, std::move(buf)};
```

## 临了收几句

容器适配器的核心就一句：**底层容器 + 受限接口，受限换语义保证**。`stack`/`queue` 是把容器的一端或两端暴露成栈/队列；`priority_queue` 更进一步，用 `<algorithm>` 的堆函数把连续容器包成优先队列——`top` O(1)、增删 O(log n)、默认最大堆、换比较器变最小堆。两个使用上的坎要记牢：一是 `top()` 只是看、要真正取出元素得紧跟 `pop()`；二是 `priority_queue` 没有「删任意元素」「按值查找」的接口，如果你需要这些（比如要中途撤销某个元素），那该用的是 `set` 或 `multiset`，不是 priority_queue。下一篇我们把目光从经典容器移开，看看 C++23/26 给容器家族加的新成员——`flat_map`、`inplace_vector`、`mdspan`。

想直接上手运行看看效果？点开下面的在线示例（能运行、也能看汇编）：

<OnlineCompilerDemo
  title="stack / queue / priority_queue：默认最大堆、greater 变最小堆"
  source-path="code/examples/vol3/09_container_adapters.cpp"
  description="三个适配器的语义、priority_queue 换比较器改堆方向、push/pop 背后的堆算法"
  allow-run
  allow-x86-asm
/>

## 参考资源

- [std::stack — cppreference](https://en.cppreference.com/w/cpp/container/stack)
- [std::queue — cppreference](https://en.cppreference.com/w/cpp/container/queue)
- [std::priority_queue — cppreference](https://en.cppreference.com/w/cpp/container/priority_queue)
- [std::priority_queue::push_range（C++23）— cppreference](https://en.cppreference.com/w/cpp/container/priority_queue/push_range)
- [std::push_heap / std::make_heap（堆算法）— cppreference](https://en.cppreference.com/w/cpp/algorithm/push_heap)
