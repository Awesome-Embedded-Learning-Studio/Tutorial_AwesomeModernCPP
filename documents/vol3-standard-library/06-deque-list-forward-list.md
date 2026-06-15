---
title: "deque、list 与 forward_list：vector 之外的三个选择"
description: "讲透顺序容器里 vector 之外的三个选择：deque 的分段连续双端结构、list 的双向链表与 splice、forward_list 的极致省内存，以及遍历 cache 与头插复杂度的真实取舍"
chapter: 7
order: 5
tags:
  - host
  - cpp-modern
  - intermediate
  - 容器
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
reading_time_minutes: 22
prerequisites:
  - "vector 深入：三指针、扩容与迭代器失效"
related:
  - "容器选择指南"
---

# deque、list 与 forward_list：vector 之外的三个选择

## vector 已经够好，为什么还要这三兄弟

vector 我们在[那一篇](01-vector-deep-dive)讲过了，连续内存、随机访问 O(1)、尾插均摊 O(1)，大多数场景它就是最优解。但它有几个盲区：头部插入是 O(n)（整个往前挪）、中间插入也是 O(n)、扩容时所有元素搬迁、迭代器/引用会因扩容失效。当你碰上「频繁在头部加东西」「需要在已知位置频繁插删且不能让迭代器失效」这类需求，vector 就不合适了。`deque`、`list`、`forward_list` 这三个，就是来补这些盲区的——它们用不同的内存布局，换来了 vector 给不了的能力，代价是各自的短板。

一句话先记着：`deque` 是「能两头插的 vector」，`list` 是「能 O(1) 中间插删的链表」，`forward_list` 是「比 list 更省内存的单向链表」。

## deque：能两头 O(1) 插，还能随机访问

deque（读 "deck"，double-ended queue）最像 vector，但解决了 vector 头插 O(n) 的问题。它的底层不是一整块连续内存，而是**分段连续**：一个中控数组（一组指针），每个指针指向一个固定大小的块（chunk），元素就存在这些块里，每个块内部连续。

```cpp
// deque 分段连续的简化骨架（标准库内部，各厂细节不同）
struct Deque {
    std::vector<Block*> control;   // 中控数组，每项指向一个块
    // 每个 Block 是一段连续内存，装若干元素
};
// 随机访问：block = control[i / chunk_size]，元素 = block[i % chunk_size]
```

这个结构带来三个特点。第一，**头尾 push/pop 都是 O(1)**：尾部满了就加新块，头部满了就在前面加块（或块内从后往前填），都不挪动已有元素——这就是它相对 vector 最大的优势。第二，**随机访问还是 O(1)**，`d[i]` 算出元素在第几个块，再取块内偏移；只是比 vector 多一次「中控 → 块」的指针解引，所以稍微慢一点。第三，**扩容不搬全部元素**：deque 满了只需扩中控数组（一组指针，很小），再挂新块，已有元素地址不变——比 vector 扩容（搬全部、迭代器全失效）温和得多。

代价是：内存不是一整块（对需要把数据传给 C 接口、或要连续 buffer 的场景不友好），而且「中控 + 多块」的结构本身有一定空间开销。

## list：双向链表，O(1) 中间插删 + splice

`list` 是双向链表，每个节点存 `{前驱指针, 数据, 后继指针}`。它的核心卖点是：**已知位置（拿到迭代器）的插入删除是 O(1)**——只改几个指针，不挪动任何其他元素。而且**迭代器永不失效**（插入/删除只影响被删节点本身的迭代器），这点连 deque 和 vector 都做不到。

list 还有个独门绝技 **splice**：`l1.splice(pos, l2)` 能把 l2 的节点链直接「剪接」到 l1，整个过程 O(1)，不拷贝任何元素——这是链表特有的能力，连续容器给不了。适合「把一个链表的某段零成本搬到另一个」的场景。

但 list 的短板也很要命。第一，**不支持随机访问**，没有 `operator[]`，要找第 1000 个元素得从头走 1000 步（O(n)）。第二，**cache 极不友好**：节点分散在堆上各处，遍历时 CPU 预取失效、cache miss 频繁。后面我们会跑给你看，list 遍历比 vector 慢好几倍，就是这个原因。所以「中间插入 O(1)」这个优势，经常被「先要 O(n) 找到位置」加上「遍历慢」抵消——除非你真的拿着迭代器频繁插删，否则不一定划算。

## forward_list：省到极致的单向链表

`forward_list` 是单向链表，每个节点只存 `{后继指针, 数据}`，比 list 少一个前驱指针。它是 C++11 才加进来的，目标很明确：对标 C 手写单链表的「零开销」——当你只需要向前遍历、且内存敏感（比如嵌入式）时，没必要为用不到的反向能力多付一个指针的代价。

代价自然是不能反向遍历，而且**没有 O(1) 的 `push_back`**（得先 O(n) 走到尾），只有 `push_front` 是 O(1)。接口也比 list 精简：它**故意不提供 `size()`**——因为标准要求 `size()` 必须 O(1)，而单链表做不到 O(1) 维护，干脆不给，要用得自己数。

## 跑跑看：遍历 vs 头插，两副完全相反的面孔

光说 list 遍历慢、vector 头插慢太抽象，咱们直接跑。先看遍历：`vector`、`deque`、`list` 各装一百万个 int，遍历求和。

```cpp
#include <iostream>
#include <vector>
#include <deque>
#include <list>
#include <chrono>

int main()
{
    const int N = 1000000;
    std::vector<int> v(N);
    std::deque<int> d(N);
    std::list<int> l;
    for (int i = 0; i < N; ++i) {
        v[i] = i;
        d[i] = i;
        l.push_back(i);
    }

    volatile long long sink = 0;
    auto bench = [&](auto& c, const char* name) {
        auto t0 = std::chrono::high_resolution_clock::now();
        long long s = 0;
        for (auto x : c) {
            s += x;
        }
        sink = s;
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << name << ": "
                  << std::chrono::duration<double, std::milli>(t1 - t0).count() << " ms\n";
    };

    bench(v, "vector ");
    bench(d, "deque  ");
    bench(l, "list   ");
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/traversal /tmp/traversal.cpp && /tmp/traversal
```

```text
vector : 0.3 ms
deque  : 0.44 ms
list   : 1.9 ms
```

（GCC 16.1.1，本机；量级关系稳定。）list 比 vector 慢了六倍，比 deque 慢四倍——这就是节点分散、cache 不友好的真实代价。deque 因为分段连续，块内还是有局部性，所以比 list 快不少，但比一整块连续的 vector 还是慢一点。

再看一个反过来的场景：往头部插十万个元素。

```cpp
#include <iostream>
#include <vector>
#include <deque>
#include <list>
#include <chrono>

int main()
{
    const int N = 100000;
    volatile int sink = 0;

    {
        std::vector<int> v;
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < N; ++i) {
            v.insert(v.begin(), i);   // 每次 O(n)
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "vector front insert: "
                  << std::chrono::duration<double, std::milli>(t1 - t0).count() << " ms\n";
        sink = v.size();
    }
    {
        std::deque<int> d;
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < N; ++i) {
            d.push_front(i);   // O(1)
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "deque  front insert: "
                  << std::chrono::duration<double, std::milli>(t1 - t0).count() << " ms\n";
        sink = d.size();
    }
    {
        std::list<int> l;
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < N; ++i) {
            l.push_front(i);   // O(1)
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "list   front insert: "
                  << std::chrono::duration<double, std::milli>(t1 - t0).count() << " ms\n";
        sink = l.size();
    }
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/front_insert /tmp/front_insert.cpp && /tmp/front_insert
```

```text
vector front insert: 246 ms
deque  front insert: 0.2 ms
list   front insert: 4.8 ms
```

这下完全反过来：vector 头插要花 246ms，deque 只要 0.2ms——差了一千多倍。因为 vector 每次 `insert(begin)` 都要把所有元素往后挪一位，十万次下来是 O(n²)；deque 和 list 的头插都是 O(1)。注意 deque 比 list 还快（list 每次要 malloc 一个节点，deque 只在块内填、偶尔加块），这也是 deque 在「双端增删」场景胜过 list 的原因。

这两组数据放一起看就清楚了：**没有银弹**。遍历密集就用 vector/deque，头插/中间插频繁就上 deque/list，选错了就是数量级的性能差距。

## 临了收几句：怎么选

| 需求 | 选 |
|------|----|
| 随机访问 + 尾部增删为主 | `vector` |
| 两头都要增删（队列 / 双端） | `deque` |
| 频繁在已知位置插删 / 需要 splice / 迭代器不能失效 | `list` |
| 极致省内存 + 只向前遍历（嵌入式） | `forward_list` |

一句口诀：能用 vector 就用 vector，真要双端就 deque，真要链表特性才上 list / forward_list。顺序容器里，vector 几乎永远是默认答案，另外三个是「有明确需求时才换上去」的专项工具。关联容器我们前面讲完了 map 和 unordered_map，下一篇我们离开容器，去看标准库的迭代器与算法体系。

## 参考资源

- [std::deque — cppreference](https://en.cppreference.com/w/cpp/container/deque)
- [std::list — cppreference](https://en.cppreference.com/w/cpp/container/list)
- [std::forward_list — cppreference](https://en.cppreference.com/w/cpp/container/forward_list)
- [容器迭代器失效规则总表 — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
