---
title: "unordered_map 与 unordered_set 深入：哈希表、桶与自定义 hash"
description: "从哈希表底层讲透 std::unordered_map/set：桶与链地址法、装填因子与 rehash、平均 O(1) 与最坏 O(n)、自定义 hash 的写法、C++14 起 rehash 不失效引用，以及与 map 的选择决策"
chapter: 7
order: 7
tags:
  - host
  - cpp-modern
  - intermediate
  - unordered_map
  - 容器
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
reading_time_minutes: 25
prerequisites:
  - "map 与 set 深入：红黑树、异构查找与节点句柄"
related:
  - "容器选择指南"
---

# unordered_map 与 unordered_set 深入：哈希表、桶与自定义 hash

## 和 map 是亲戚，但底层换了个世界

上一篇我们讲 map，它底下一棵红黑树，查找是对数 O(log n)。这一篇的 `unordered_map`，名字里带个 "unordered"——它不排序，代价换来了更狠的东西：平均 O(1) 的查找。但天底下没有免费的午餐，O(1) 的代价是底层从一棵树换成了一张哈希表，引出桶、装填因子、rehash、自定义 hash 这一整套新的机制。我们这一篇就把 `unordered_map` 和 `unordered_set` 从哈希表底层到工程用法讲透。

先把它和 map 放一起看，差异一眼就清楚：

| | `map` / `set` | `unordered_map` / `unordered_set` |
|---|---|---|
| 底层 | 红黑树 | 哈希表 |
| 有序 | 是（按键排序） | 否 |
| 查找/插入/删除 | O(log n) | 平均 O(1)，最坏 O(n) |
| 自定义键需要 | `operator<` | hash + `operator==` |
| 插入是否失效迭代器 | 否 | 可能（触发 rehash 时） |

一句话：你要有序遍历、或需要「前驱/后继」这类范围操作，就留在 map；你要的就是纯查找、插入、删除，不在乎顺序，`unordered` 多半更快。这个选择不是绝对的，后面我们会细说。

## 底层是一张哈希表：桶、链表与装填因子

`unordered_map` 底下是一张哈希表，绝大多数实现用的是**链地址法（separate chaining）**：一个 bucket 数组，每个桶挂一条链表（或类似结构）。插入一个元素时，先用 hash 函数算出 key 的哈希值，再对桶数量取模，决定它落到哪个桶；这个桶里已经有元素就挂在链表后面，查找时就在这条短链上线性扫。

```cpp
// 链地址法哈希表的简化骨架（标准库内部，各厂细节不同）
struct HashTable {
    std::vector<Bucket> buckets;   // bucket 数组，每个桶内部是同 hash 元素的链表
};
// 插入/查找定位：bucket_index = hash(key) % buckets.size();
```

这里有个关键概念：**装填因子（load factor）**。它等于 `size() / bucket_count()`，也就是平均每个桶挂了多少个元素。桶越挤，链表越长，查找就越慢。标准库设了一个上限 `max_load_factor()`，默认是 1.0——当装填因子超过这个上限，容器就 **rehash**：分配一个更大的 bucket 数组（通常扩到大约两倍），把所有元素重新 hash、重新落桶。

rehash 是 `unordered_map` 最贵的操作：它要把全部元素重新搬一遍，复杂度 O(n)。虽然均摊到每次插入上仍是常数，但单次 rehash 那一下会有明显的停顿。这也是为什么工程里如果你能预估元素数量，最好在插入之前先 `reserve(n)`——它一次性把 bucket 开够，避免后续反复 rehash。

```cpp
std::unordered_map<int, std::string> m;
m.reserve(10000);   // 提前开好桶，避免逐个插入时的多次 rehash
```

我们跑一下，看 load_factor 怎么触发 rehash：

```cpp
#include <iostream>
#include <unordered_map>

int main()
{
    std::unordered_map<int, int> m;
    std::size_t prev = m.bucket_count();
    std::cout << "初始 bucket_count = " << prev << "\n";
    for (int i = 0; i < 100; ++i) {
        m[i] = i;
        if (m.bucket_count() != prev) {
            std::cout << "size=" << m.size()
                      << " rehash: " << prev << " -> " << m.bucket_count()
                      << " (load_factor=" << m.load_factor() << ")\n";
            prev = m.bucket_count();
        }
    }
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/lf_rehash /tmp/lf_rehash.cpp && /tmp/lf_rehash
```

```text
初始 bucket_count = 1
size=1 rehash: 1 -> 13 (load_factor=0.0769231)
size=14 rehash: 13 -> 29 (load_factor=0.482759)
size=30 rehash: 29 -> 59 (load_factor=0.508475)
size=60 rehash: 59 -> 127 (load_factor=0.472441)
```

注意看 bucket_count 的跳变序列：1 → 13 → 29 → 59 → 127，**全是质数**——这正是 libstdc++ 的选择（用质数桶能让 `hash % bucket_count` 的分布更均匀）。而每次跳变都发生在 `size` 刚刚超过 `bucket_count`（也就是 load_factor 突破 1.0）那一刻：size 到 14 时 14/13 > 1.0 触发扩到 29，size 到 30 时 30/29 > 1.0 触发扩到 59，依此类推。这就是「装填因子超限 → rehash 扩桶」的直观过程。

## 复杂度与迭代器失效：和 map 又不一样了

复杂度先说清楚：`unordered_map` 的查找、插入、删除在**平均情况**下是 O(1)，**最坏情况**是 O(n)。最坏情况什么时候发生？当大量 key 哈希冲突（都落到同一个桶），哈希表退化成一条长链表，查找变成线性扫描。好的 hash 函数加上合理的 load factor 能让冲突概率极低，所以实践中几乎总是 O(1)；但标准诚实地标注了最坏 O(n)，因为理论上它确实可能。

迭代器失效这块，`unordered_map` 和 map 又不一样，而且比 map 更「凶」一点。规则是：

- **rehash**（插入触发，或手动 `reserve` / `rehash`）：**失效所有迭代器**；但 C++14 起，**指向元素的引用和指针不被 rehash 失效**
- **erase**：只失效被删元素本身的迭代器/引用，其他不受影响

这条要特别留意。上一篇我们说过 map 插入绝不失效迭代器；`unordered_map` 因为插入可能 rehash，迭代器会失效。但有意思的是，C++14 之后标准额外保证了 rehash 不动指向元素的引用和指针——也就是说，你持有的 `value_type&` 和元素指针在 rehash 后仍然有效，只有迭代器会废。这是个实用的保证：你可以安全地长期持有 `unordered_map` 元素的引用，即便中间发生了 rehash。

```cpp
#include <iostream>
#include <unordered_map>
#include <string>

int main()
{
    std::unordered_map<int, std::string> m;
    m[1] = "alpha";
    std::string& ref = m.at(1);   // 持有元素引用

    m.reserve(1000);              // 触发 rehash，迭代器全失效
    for (int i = 100; i < 200; ++i) {
        m[i] = "x";               // 大量插入可能再次 rehash
    }

    std::cout << ref << '\n';     // C++14 起，引用仍然有效
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/umap_ref /tmp/umap_ref.cpp && /tmp/umap_ref
```

```text
alpha
```

## 自定义 hash：让自定义类型也能当 key

默认情况下，`std::hash<T>` 只对内置类型和标准库常用类型（string、整数类型等）有定义。你想拿自定义类型当 `unordered_map` 的 key，就得告诉它两件事：**怎么算 hash**、**怎么判等**。

判等默认用 `operator==`（通过 `std::equal_to`）。hash 有两种给法：特化 `std::hash`，或者直接把一个自定义 Hash 类型作为模板参数传给 `unordered_map`。我们看一个把二维点当 key 的例子，这里用特化 `std::hash` 的写法：

```cpp
#include <iostream>
#include <unordered_map>

struct Point {
    int x, y;
    bool operator==(Point const& o) const { return x == o.x && y == o.y; }
};

// 特化 std::hash<Point>
namespace std {
template <>
struct hash<Point> {
    std::size_t operator()(Point const& p) const noexcept
    {
        // 把两个 int 组合成一个 size_t；这是简化版，生产里用更好的混合
        return static_cast<std::size_t>(p.x) * 31 + static_cast<std::size_t>(p.y);
    }
};
}  // namespace std

int main()
{
    std::unordered_map<Point, std::string> grid;
    grid[{1, 2}] = "A";
    grid[{3, 4}] = "B";

    auto it = grid.find({1, 2});
    std::cout << (it != grid.end() ? it->second : "not found") << '\n';
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/custom_hash /tmp/custom_hash.cpp && /tmp/custom_hash
```

```text
A
```

这里有个铁律：**hash 和 `==` 必须一致**。也就是说，如果 `a == b` 为真，那 `hash(a)` 必须等于 `hash(b)`——否则相等的元素会落到不同的桶，查找就找不到了。反过来不要求（`hash(a) == hash(b)` 时 `a` 不必等于 `b`，那只是冲突，正常现象）。上面这个 `x*31 + y` 是演示用的简单混合，生产环境可以用 `boost::hash_combine` 或更讲究的混合函数，进一步降低冲突概率。

## 哈希冲突与 DoS：libstdc++ 给你的 hash 为什么带了点随机

哈希表有个著名的攻击面叫 **hash flooding**：攻击者精心构造一大批哈希值相同的 key 喂给你的程序，所有元素挤进同一个桶，查找从 O(1) 退化成 O(n)，CPU 被打满——这是早年很多 web 服务被拖垮的原因之一。

libstdc++ 的应对是：它的 `std::hash<std::string>` 在每次程序启动时用一个随机种子做 hash（基于带种子的高质量 hash 函数）。这样一来，同一份输入在不同进程里落桶的位置不一样，攻击者没法预先构造出「刚好全冲突」的输入。这是 libstdc++ 的实现策略（libc++、MSVC STL 各有各的做法），标准并不强制——但实战里这是个值得知道的事：如果你用自定义类型 key，且 key 可能来自不可信输入，你写的 hash 函数质量就直接关系到抗 DoS 的能力。

## 上手跑一跑：unordered_map 比 map 快多少

光说「平均 O(1) 比 O(log n) 快」太抽象，我们直接量一下。准备十万元素的 map 和 unordered_map，各做一百万次查找：

```cpp
#include <iostream>
#include <map>
#include <unordered_map>
#include <chrono>

int main()
{
    std::map<int, int> om;
    std::unordered_map<int, int> um;
    for (int i = 0; i < 100000; ++i) {
        om[i] = i;
        um[i] = i;
    }
    volatile int sink = 0;

    auto bench = [&](auto& m) {
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 1000000; ++i) {
            sink += m.find(i % 100000)->second;
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(t1 - t0).count();
    };

    std::cout << "map:           " << bench(om) << " ms\n";
    std::cout << "unordered_map: " << bench(um) << " ms\n";
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/uvm /tmp/uvm.cpp && /tmp/uvm
```

```text
map:           48.4 ms
unordered_map: 2.2 ms
```

上面是 GCC 16.1.1 在本机跑的结果：map 约 48ms，unordered_map 约 2ms，**unordered 快了将近一个数量级**。具体毫秒数随你的机器变化，但这个量级差距是稳定的——十万元素下，map 一次查找要 log₂(100000) ≈ 17 次比较，unordered_map 平均 O(1) 直接命中，百万次查找累积出来的差距就是这么明显。这就是 `unordered_map` 存在的核心理由。

## 临了收几句：什么时候该选它

`unordered_map` 和 `unordered_set` 把「有序」这个属性丢掉，换来了平均 O(1) 的查找。它的底层是哈希表——bucket 数组加每桶一条链，靠装填因子控制何时 rehash 扩容。用它要记住几件事：插入可能触发 rehash，这会失效迭代器，但 C++14 起不失效指向元素的引用；自定义类型当 key 必须提供 hash 和 `==`，且两者必须一致；如果 key 来自不可信输入，hash 函数的质量关系到抗冲突 DoS 的能力。

至于什么时候选它而不是 map：不在乎顺序、且以查找/插入/删除为主，`unordered` 多半更快；需要有序遍历、范围查询、或稳定的迭代器顺序，就回到 map。下一篇我们离开关联容器，去看顺序容器里 vector 之外的选择——deque 和 list。

想直接上手运行看看效果？点开下面的在线示例（能运行、也能看汇编）：

<OnlineCompilerDemo
  title="unordered_map：哈希桶、rehash 质数序列、reserve"
  source-path="code/examples/vol3/07_unordered_map_set.cpp"
  description="观察 rehash 触发的 bucket_count 跳变、桶分布、reserve 预撑桶"
  allow-run
/>

## 参考资源

- [std::unordered_map — cppreference](https://en.cppreference.com/w/cpp/container/unordered_map)
- [std::unordered_set — cppreference](https://en.cppreference.com/w/cpp/container/unordered_set)
- [std::hash — cppreference](https://en.cppreference.com/w/cpp/utility/hash)
- [容器迭代器失效规则总表 — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
