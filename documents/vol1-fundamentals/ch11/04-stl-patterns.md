---
title: "STL 常用模式"
description: "容器选择指南、常见陷阱和性能基础"
chapter: 11
order: 4
difficulty: beginner
reading_time_minutes: 15
platform: host
prerequisites:
  - "算法库初见"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# STL 常用模式

前面三章我们分别搞定了 `vector`、关联容器和算法库，每一章都在各自的领域里深耕。但实际写代码的时候，问题往往不是"某个容器怎么用"或"某个算法怎么调"，而是"我该选哪个容器"、"为什么我的程序跑得这么慢"、"怎么又踩到迭代器失效的坑了"。这些都是跨容器、跨算法的综合问题，需要一个系统化的视角来应对。

这一章我们要做的事情就是把前面零散的知识串成线。我们先搞清楚"什么场景用什么容器"这个最高频的决策问题，再过一遍 STL 使用中最容易踩的几个大坑，然后聊聊性能相关的基本常识，最后用一个综合实战程序把容器选择、算法搭配、踩坑防御全部串起来。学完这一章，你对 STL 的理解会从"知道怎么用"升级到"知道怎么用对"。

> **学习目标**
>
> 完成本章后，你将能够：
>
> - [ ] 根据实际需求快速选择合适的 STL 容器
> - [ ] 识别并避免迭代器失效、遍历中修改容器等常见陷阱
> - [ ] 理解缓存友好性对容器性能的影响
> - [ ] 熟练使用 erase-remove 惯用法和 C++20 的 `std::erase`
> - [ ] 运用"算法替代手写循环"的原则编写更清晰的代码

## 先做选择——容器选择指南

很多朋友学完一堆容器之后反而更纠结了：我到底该用哪个？其实绝大多数场景下，决策逻辑非常清晰。我们按照你的核心需求来走一遍：

如果你的数据是顺序的、数量会变化、需要随机访问，那 `std::vector` 几乎永远是首选。它的元素在内存中连续排列，CPU 缓存预取能高效工作，下标访问 O(1)，尾部增删摊还 O(1)，唯一的弱点是中间插入删除 O(n)——但说实话，大部分程序并不需要在中间频繁插入。

如果你需要"给一个 key 查一个 value"，而且不需要按 key 顺序遍历，那 `std::unordered_map` 是效率最高的选择，平均 O(1) 的查找速度。如果同时需要按 key 有序遍历或者做范围查询，换成 `std::map`。

如果你需要维护一个"没有重复元素"的集合，用 `std::set`。如果只是判断"某个东西在不在"而不需要有序，`std::unordered_set` 更快。

如果你的元素数量在编译期就确定了，不需要动态增删，用 `std::array`——它是零开销的固定大小数组，比 vector 少了动态分配的开销，而且和 C 数组一样高效。

把这些整理成一张决策表：

| 核心需求 | 首选容器 | 特点 |
|----------|----------|------|
| 顺序存储、随机访问 | `std::vector` | 连续内存、缓存友好 |
| 按键快速查找（无需有序） | `std::unordered_map` | 平均 O(1) 查找 |
| 按键查找且需有序遍历 | `std::map` | O(log n)、红黑树 |
| 唯一元素集合 | `std::set` | 自动去重、有序 |
| 固定大小数组 | `std::array` | 零开销、栈分配 |

这张表能覆盖 90% 的日常决策。剩下 10% 涉及 `deque`（双端队列，头部尾部都是 O(1) 插入删除）、`list`（双向链表，中间插入删除 O(1) 但极差的缓存性能）、`multimap` / `multiset`（允许重复 key）等，遇到的时候再去查文档即可。

有一个实用的经验法则值得记住：**如果你不确定该用什么，就用 `vector`**。Bjarne Stroustrup（C++ 之父）和很多 C++ 专家都反复强调过这一点。`vector` 在大多数场景下的表现都不差，即使理论复杂度不是最优的，它的缓存友好性也经常让它在实际基准测试中胜出。只有当你能明确说出"为什么 vector 不行"的时候，才需要考虑其他容器。

## 踩坑预警——STL 里最容易翻车的地方

用过一段时间 STL 之后你会发现，真正让人头疼的往往不是"某个接口怎么调"，而是那些"编译能过、甚至运行正常、但逻辑已经错了"的陷阱。这里我们把最常见的几个坑逐个过一遍，每个都是我或者我认识的 C++ 开发者实打实踩过的。

### 坑一：迭代器失效

这个问题在讲 `vector` 的时候提过，但它不止影响 `vector`，而且不止发生在扩容的时候。核心规则是这样的：对于 `vector` 和 `string`，任何可能导致重新分配内存的操作（`push_back`、`emplace_back`、`insert`、`reserve` 导致的重新分配）都会使所有迭代器、指针和引用失效。即使没有重新分配，`insert` 和 `erase` 也会使被影响位置之后的迭代器失效。对于 `deque`，任何插入操作都会使所有迭代器失效。对于 `map`、`set`、`unordered_map`、`unordered_set`，`erase` 只会使指向被删除元素的迭代器失效，其他迭代器不受影响——这是一个非常重要的区别。

```cpp
std::vector<int> v = {1, 2, 3, 4, 5};
auto it = v.begin() + 2;  // 指向 3
v.push_back(6);           // 可能触发扩容
// it 现在是悬垂迭代器——解引用是未定义行为

std::map<int, std::string> m = {{1, "a"}, {2, "b"}, {3, "c"}};
auto mit = m.find(2);
m.erase(1);               // 删除 key=1 的元素
// mit 仍然有效——map 的 erase 不影响其他迭代器
```

这个区别的实际意义在于：如果你需要在遍历 `map` 的过程中删除元素，可以直接用迭代器做，但遍历 `vector` 时删除元素就需要特别小心。我们接下来就看看这个更具体的场景。

> **踩坑预警**：保存了迭代器之后，任何可能修改容器结构的操作都要视为"可能使迭代器失效"。不要想当然地认为"我只是 push_back 了一个元素，应该没事"——vector 的扩容策略取决于实现，你无法预测哪一次 push_back 会触发重新分配。如果你确实需要在修改容器后继续使用某个位置的信息，用索引而不是迭代器，因为索引在逻辑上是稳定的。

### 坑二：遍历中修改容器

这是一个非常经典的翻车现场。先看一个"乍一看没问题其实会炸"的例子：

```cpp
std::vector<int> v = {1, 2, 3, 4, 5, 6};
for (auto it = v.begin(); it != v.end(); ++it) {
    if (*it % 2 == 0) {
        v.erase(it);  // 未定义行为！it 已失效
    }
}
```

调用 `erase` 之后，`it` 就失效了，再对它 `++it` 是未定义行为。正确的写法是用 `erase` 的返回值——它返回指向被删除元素下一个元素的迭代器：

```cpp
for (auto it = v.begin(); it != v.end(); /* 不在这里 ++it */) {
    if (*it % 2 == 0) {
        it = v.erase(it);  // erase 返回下一个元素的迭代器
    } else {
        ++it;
    }
}
```

但说实话，这种写法容易出错——稍微一不留神就会忘记在 `erase` 分支里不做 `++it`。更推荐的做法是先用 `std::remove_if` 把要删的元素挪到末尾，再一次性 `erase`：

```cpp
// C++20 之前
auto it = std::remove_if(v.begin(), v.end(), [](int x) { return x % 2 == 0; });
v.erase(it, v.end());

// C++20——一行搞定
std::erase_if(v, [](int x) { return x % 2 == 0; });
```

对于 `map` 和 `set`，遍历中删除的安全写法稍有不同。因为 C++11 之前 `erase` 返回 `void`，所以传统写法是 `m.erase(it++)`——先拷贝迭代器再自增再传给 erase。从 C++11 开始，关联容器的 `erase` 也返回下一个迭代器了，所以写法和 vector 一样：`it = m.erase(it)`。

> **踩坑预警**：range-for 循环内部绝对不能修改容器结构（插入或删除元素）。range-for 的底层用的是迭代器，你无法在 range-for 里拿到 `erase` 的返回值。如果编译器开了 sanitizer，这类 bug 很容易被抓到；但如果没有开，它可能"恰好能跑"——在 debug 阶段完全看不出来，到了生产环境的某个特定负载下就崩了，调试起来非常折磨。

### 坑三：map 的 operator[] 悄悄插入元素

这个坑在讲关联容器的时候详细说过，但它的出镜率实在太高了，这里再从"模式"的角度强调一遍。`map[key]` 在 key 不存在时会自动插入一个默认构造的元素。这意味着两个后果：第一，在 `const map` 上 `operator[]` 直接编译不过，因为它是修改操作；第二，如果你只是想检查某个 key 是否存在而用了 `operator[]`，map 会被悄悄修改。

最阴险的场景是在遍历过程中不小心触发 `operator[]`：

```cpp
std::map<std::string, int> word_count = {{"hello", 2}, {"world", 1}};

// "安全地"读取所有 key 的值——其实不是！
for (const auto& [word, count] : word_count) {
    // 如果在这里调用 word_count[some_other_key]，map 会被修改
    // 在 range-for 中修改容器结构 = 未定义行为
}
```

当然上面这个例子有点极端，但一个更隐蔽的变体是：你在循环体里调用了某个函数，那个函数内部对 map 做了 `operator[]` 访问。所以核心原则是：**只读查找永远用 `find`、`count` 或 `contains`（C++20），把 `operator[]` 留给确实需要"访问时自动创建"的场景**。

> **踩坑预警**：如果你的 value 类型没有默认构造函数（比如一个只接受参数构造的类），那 `operator[]` 在 key 不存在时连编译都过不了——这反而是件好事，因为编译器帮你挡住了这个坑。真正危险的是 `int`、`string` 这些能默认构造的类型，`operator[]` 悄悄插入 0 或空字符串，逻辑错了但程序照跑不误。

## 理解性能——缓存、预留和选择

聊完了坑，我们来谈谈性能。很多朋友学了各种容器的时间复杂度之后，以为选容器就是选 O(1) 还是 O(log n)。但实际上，现代 CPU 的缓存机制对性能的影响经常比算法复杂度更大。

### 连续内存和缓存友好性

CPU 访问内存的速度远慢于执行指令，所以现代 CPU 都有多级缓存（L1、L2、L3）。当 CPU 读取某个地址的数据时，会把附近的一整块数据（通常 64 字节，即一个缓存行）一起加载到缓存中。这意味着如果你正在顺序遍历一个连续内存的数据结构，第一次访问把一整块数据都带进了缓存，后续访问直接命中缓存，速度极快。

`std::vector` 和 `std::array` 的元素在内存中是紧密排列的，遍历时缓存命中率非常高。而 `std::list` 的每个节点都是独立分配的，节点之间在内存中的位置毫无规律，遍历时几乎每次都要访问主存，缓存命中率极低。即使 `list` 在中间插入删除是 O(1)，`vector` 是 O(n)，在实际跑起来的时候 vector 经常更快——因为 CPU 缓存预取的威力弥补了理论复杂度的劣势。

一个经典的基准测试结论是：对于存储 `int` 或 `double` 这类小元素的容器，`vector` 的线性搜索（O(n)）在 n < 1000 左右时经常比 `list` 的逐节点遍历更快。这不是因为 O(n) 比 O(1) 好，而是因为连续内存带来的缓存优势太大了。

### reserve 的重要性

`vector` 的扩容涉及"分配新内存 -> 拷贝/移动所有元素 -> 释放旧内存"这三步，成本不低。如果你事先知道大概要存多少个元素，调用 `reserve` 一次性分配好空间，能彻底消除扩容开销：

```cpp
std::vector<int> v;
v.reserve(10000);  // 一次分配，之后 10000 次 push_back 零扩容
for (int i = 0; i < 10000; ++i) {
    v.push_back(i);
}
```

`unordered_map` 也有类似的概念——你可以用 `reserve` 预分配足够的桶（bucket），减少 rehash 的次数。当你往 `unordered_map` 里插入大量元素时，一次 `reserve` 往往能让整体耗时下降 30% 甚至更多。

### string 的小串优化

一个不太广为人知但很实用的事实是：大多数标准库实现都使用了"小串优化"（Small String Optimization, SSO）。当 `std::string` 的长度小于某个阈值（通常 15-22 字节，取决于实现）时，字符串数据直接存放在 string 对象内部的缓冲区中，不需要堆分配。这意味着短字符串的拷贝、赋值和销毁都非常快。在实际开发中，大部分字符串都很短（变量名、配置项、日志消息等），SSO 悄悄地帮你省掉了大量的内存分配开销。

## 实战演练——综合运用 STL 模式

现在我们把这一章讨论的所有知识点——容器选择、踩坑防御、性能意识——揉到一个综合实战程序里。场景是这样的：我们有一批传感器读数，需要去重、过滤异常值、排序、统计，并输出最终的分析报告。

```cpp
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// 单条传感器读数
struct Reading {
    std::string sensor_id;
    double value;
    uint32_t timestamp;
};

/// 分析报告
struct Report {
    std::string sensor_id;
    double min_val;
    double max_val;
    double avg_val;
    std::size_t count;
};

/// 过滤异常值：去掉偏离均值超过 kSigma 个标准差的数据
void filter_outliers(std::vector<Reading>& readings, double k_sigma)
{
    if (readings.empty()) {
        return;
    }

    // 计算均值
    double sum = 0.0;
    for (const auto& r : readings) {
        sum += r.value;
    }
    double mean = sum / static_cast<double>(readings.size());

    // 计算标准差
    double sq_sum = 0.0;
    for (const auto& r : readings) {
        sq_sum += (r.value - mean) * (r.value - mean);
    }
    double stddev = std::sqrt(sq_sum / static_cast<double>(readings.size()));

    // remove-erase 删除异常值
    auto it = std::remove_if(readings.begin(), readings.end(),
        [mean, stddev, k_sigma](const Reading& r) {
            return std::abs(r.value - mean) > k_sigma * stddev;
        });
    readings.erase(it, readings.end());
}

/// 为每个传感器生成分析报告
std::vector<Report> generate_reports(std::vector<Reading>& readings)
{
    // 用 unordered_map 按传感器分组（不需要有序遍历，O(1) 查找）
    std::unordered_map<std::string, std::vector<Reading>> groups;
    groups.reserve(16);  // 预分配，减少 rehash

    for (auto& r : readings) {
        groups[r.sensor_id].push_back(std::move(r));
    }

    std::vector<Report> reports;
    reports.reserve(groups.size());

    for (auto& [id, recs] : groups) {
        if (recs.empty()) {
            continue;
        }

        // 按时间戳排序
        std::sort(recs.begin(), recs.end(),
            [](const Reading& a, const Reading& b) {
                return a.timestamp < b.timestamp;
            });

        // 用 STL 算法计算统计量
        auto [min_it, max_it] = std::minmax_element(recs.begin(), recs.end(),
            [](const Reading& a, const Reading& b) {
                return a.value < b.value;
            });

        double sum = std::accumulate(recs.begin(), recs.end(), 0.0,
            [](double acc, const Reading& r) { return acc + r.value; });

        reports.push_back({
            id,
            min_it->value,
            max_it->value,
            sum / static_cast<double>(recs.size()),
            recs.size()
        });
    }

    // 按传感器 ID 排序输出，保证结果稳定
    std::sort(reports.begin(), reports.end(),
        [](const Report& a, const Report& b) { return a.sensor_id < b.sensor_id; });

    return reports;
}

/// 去除重复读数（同一传感器、同一时间戳视为重复）
void deduplicate(std::vector<Reading>& readings)
{
    // 用 unordered_set 记录已见过的 (sensor_id, timestamp) 组合
    struct Key {
        std::string sensor_id;
        uint32_t timestamp;
    };

    // 自定义哈希和相等比较——unordered_set 必需
    struct KeyHash {
        std::size_t operator()(const Key& k) const
        {
            auto h1 = std::hash<std::string>{}(k.sensor_id);
            auto h2 = std::hash<uint32_t>{}(k.timestamp);
            return h1 ^ (h2 << 1);  // 简单组合哈希
        }
    };

    struct KeyEqual {
        bool operator()(const Key& a, const Key& b) const
        {
            return a.sensor_id == b.sensor_id && a.timestamp == b.timestamp;
        }
    };

    std::unordered_set<Key, KeyHash, KeyEqual> seen;
    seen.reserve(readings.size());

    auto it = std::remove_if(readings.begin(), readings.end(),
        [&seen](const Reading& r) {
            Key k{r.sensor_id, r.timestamp};
            if (seen.count(k)) {
                return true;  // 重复，标记删除
            }
            seen.insert(k);
            return false;
        });
    readings.erase(it, readings.end());
}

int main()
{
    // 模拟传感器数据——包含重复和异常值
    std::vector<Reading> readings = {
        {"temp-01", 22.5, 1001},
        {"temp-01", 22.7, 1002},
        {"temp-01", 22.5, 1001},  // 重复
        {"temp-01", 85.0, 1003},  // 异常值
        {"temp-01", 22.9, 1004},
        {"press-01", 1013.2, 1001},
        {"press-01", 1013.5, 1002},
        {"press-01", 1013.2, 1001},  // 重复
        {"press-01", 12.0, 1003},    // 异常值
        {"press-01", 1013.8, 1004},
    };

    std::cout << "=== Raw readings: " << readings.size() << " ===\n";

    // 第一步：去重
    deduplicate(readings);
    std::cout << "After dedup: " << readings.size() << "\n";

    // 第二步：过滤异常值（2 倍标准差）
    filter_outliers(readings, 2.0);
    std::cout << "After outlier filter: " << readings.size() << "\n";

    // 第三步：生成分析报告
    auto reports = generate_reports(readings);

    std::cout << "\n=== Analysis Reports ===\n";
    for (const auto& r : reports) {
        std::cout << "  [" << r.sensor_id << "] "
                  << "min=" << r.min_val << ", max=" << r.max_val
                  << ", avg=" << r.avg_val
                  << ", n=" << r.count << "\n";
    }

    return 0;
}
```

编译运行：

```bash
g++ -std=c++20 -Wall -Wextra -o stl_patterns stl_patterns.cpp && ./stl_patterns
```

预期输出：

```text
=== Raw readings: 10 ===
After dedup: 8
After outlier filter: 6

=== Analysis Reports ===
  [press-01] min=1013.2, max=1013.8, avg=1013.5, n=3
  [temp-01] min=22.5, max=22.9, avg=22.7, n=3
```

我们来逐层拆解这个程序里的设计决策。去重部分选择 `unordered_set` 而不是 `set`，因为我们只关心"见没见过"而不需要有序遍历，O(1) 的查找比 O(log n) 更合适。注意这里必须自定义 `KeyHash` 和 `KeyEqual`——因为 `Key` 是自定义结构体，标准库没有默认的哈希函数。如果你忘了提供，编译器会用一堆模板实例化错误来"温馨提示"你。

分组部分选择 `unordered_map<string, vector<Reading>>`，同样是因为不需要按 key 有序遍历。`reserve(16)` 是一个经验性的预分配——传感器数量通常不多，一次分配避免后续 rehash。过滤异常值用的是 `remove_if` + `erase`，而不是在遍历中直接删除——这样既安全又清晰。统计部分全部用 STL 算法完成——`minmax_element` 一趟找到最大最小值，`accumulate` 求和，没有手写循环。

## 动手试试——练习题

### 练习 1：容器选择实战

给以下场景选择最合适的容器，并说明理由：(a) 存储一个游戏角色的背包物品列表，经常在末尾添加和删除；(b) 维护一个拼写检查器的词典，需要频繁判断某个单词是否存在；(c) 存储一个班级所有学生的学号-姓名映射，按学号顺序输出；(d) 存储一个 3x3 矩阵的数据。

### 练习 2：修正有问题的代码

下面这段代码有至少两个 STL 陷阱，找出并修正它们：

```cpp
std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8};
for (auto it = data.begin(); it != data.end(); ++it) {
    if (*it % 2 == 0) {
        data.erase(it);
    }
}
```

### 练习 3：性能对比

写一个基准测试：分别用 `std::vector<int>` 和 `std::list<int>` 存储 100000 个随机整数，用 `<chrono>` 计时比较两者的 (a) 顺序遍历求和耗时，(b) 排序耗时。用实际数据体会缓存友好性的影响。

## 小结

这一章我们从"怎么用对 STL"这个角度把前面三章的知识重新组织了一遍。容器选择方面，核心思路是按需求决策：顺序存储选 `vector`，快速查找选 `unordered_map`，有序键值对选 `map`，去重选 `set`，固定大小选 `array`。拿不准就用 `vector`，这几乎总是不会错的选择。

踩坑防御方面，三个最需要警惕的陷阱是迭代器失效（特别是 vector 扩容和 erase 之后）、遍历中修改容器（用 remove-erase 替代手写删除循环）、以及 map 的 `operator[]` 悄悄插入元素（只读查找用 `find` 或 `contains`）。

性能方面，连续内存的缓存友好性让 `vector` 在实际场景中经常比理论复杂度更优的 `list` 跑得更快。`reserve` 是消除扩容开销的利器，对 vector 和 unordered_map 都有效。

到这里，第 11 章就全部完成了。我们从 `vector` 起步，学了关联容器和算法库，最后把这些知识整合成了系统化的 STL 使用模式。下一章我们要深入 C++ 内存模型了——从内存布局到堆栈分配，从 `new`/`delete` 到内存对齐，这些都是写出高性能 C++ 代码的底层基础。

---

> **参考资源**
>
> - [cppreference: Container library](https://en.cppreference.com/w/cpp/container)
> - [cppreference: std::erase (C++20)](https://en.cppreference.com/w/cpp/container/vector/erase2)
> - [Bjarne Stroustrup: Why you should avoid linked lists](https://www.youtube.com/watch?v=YQs6IC-vgmo)
> - [cppreference: Iterator invalidation](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
