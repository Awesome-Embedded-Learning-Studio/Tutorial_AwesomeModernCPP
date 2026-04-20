---
title: "关联容器快速上手"
description: "掌握 std::map、std::set 和 std::unordered_map 的核心操作，学会按键查找和维护有序集合"
chapter: 11
order: 2
difficulty: beginner
reading_time_minutes: 15
platform: host
prerequisites:
  - "std::vector 快速上手"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# 关联容器快速上手

上一章我们把 `std::vector` 从头到尾过了一遍——动态数组、连续存储、下标随机访问 O(1)，处理有序序列的时候它就是主力。但很多场景下我们关心的不是"第几个元素是什么"，而是"某个 key 对应的 value 是什么"。比如统计一段文本里每个单词出现了几次，或者检查某个单词是否在拼写词典里——这种"给一个 key，查一个结果"的需求，用 vector 来做的话要么排序后二分查找，要么线性扫描，写起来费劲性能也差。C++ 标准库为我们准备了一组专门解决这类问题的容器，叫做**关联容器**（associative container）。

这一章我们要搞明白的是三兄弟：`std::map`（有序键值对）、`std::set`（有序唯一元素集合）、`std::unordered_map`（哈希键值对）。它们的共同特点是：查找、插入、删除操作都很快，不需要我们把整个容器遍历一遍。区别在于 `map` 和 `set` 内部用红黑树实现，元素始终有序，操作复杂度 O(log n)；而 `unordered_map` 用哈希表实现，平均 O(1) 但不保证顺序。

> **学习目标**
>
> 完成本章后，你将能够：
>
> - [ ] 使用 `std::map` 的插入、查找、删除操作
> - [ ] 理解 `operator[]` 的默认插入陷阱并知道何时该用 `at` 或 `find`
> - [ ] 使用 `std::set` 维护有序唯一元素集合
> - [ ] 用结构化绑定遍历 map：`for (auto& [k, v] : map)`
> - [ ] 理解 `unordered_map` 与 `map` 的性能差异并做出合理选择
> - [ ] 用 map 和 set 编写词频统计和拼写检查的实战程序

## 上号——std::map 基本操作

`std::map` 是一个有序的键值对容器，声明在 `<map>` 头文件中。它的每个元素是一个 `std::pair<const Key, Value>`，其中 Key 是键的类型，Value 是值的类型。内部用红黑树（一种自平衡二叉搜索树）存储，所以元素始终按 key 升序排列，查找、插入、删除都是 O(log n)。

先来看怎么往里面塞东西：

```cpp
#include <iostream>
#include <map>
#include <string>

int main()
{
    std::map<std::string, int> scores;

    // 方式一：用 operator[] 赋值
    scores["Alice"] = 95;
    scores["Bob"] = 87;

    // 方式二：用 insert 插入 pair
    scores.insert({"Charlie", 72});

    // 方式三：用 emplace 原地构造（推荐）
    scores.emplace("Diana", 91);

    // 方式四：初始化列表
    std::map<std::string, int> ages = {
        {"Alice", 22}, {"Bob", 25}, {"Charlie", 20}
    };

    return 0;
}
```

这几种插入方式各有适用场景。`operator[]` 最直观，但它有一个非常阴险的行为——如果 key 不存在，它会自动插入一个值初始化的元素（对于 `int` 就是 0，对于类类型会调用默认构造函数）。也就是说 `scores["Eve"]` 即使你只是想看一下值，也会往 map 里塞一个 `{"Eve", 0}`。这一点后面踩坑预警会详细说。

接下来是查找。`find` 返回一个迭代器，指向找到的元素；找不到则返回 `end()`。`count` 返回匹配元素的个数（对于 map 来说要么是 0 要么是 1）。C++20 新增了 `contains`，语义更直观：

```cpp
// C++11 起所有版本都能用的方式
auto it = scores.find("Alice");
if (it != scores.end()) {
    std::cout << "Alice: " << it->second << "\n";
}

// count 也可以判断存在性
if (scores.count("Bob")) {
    std::cout << "Bob exists\n";
}

// C++20 引入 contains，语义最清晰
if (scores.contains("Diana")) {
    std::cout << "Diana exists\n";
}
```

删除用 `erase`，可以按 key 删除也可以按迭代器删除：

```cpp
scores.erase("Bob");            // 按 key 删除
scores.erase(scores.begin());   // 删除第一个元素（key 最小的）
scores.clear();                 // 清空整个 map
```

> **踩坑预警**：`map[key]` 在 key 不存在时会**自动插入一个默认值**。这意味着两个后果：第一，如果你只是想检查某个 key 是否存在，用 `operator[]` 会导致 map 被悄悄修改，这在逻辑上是 bug，而且如果你的 value 类型没有默认构造函数，直接编译不过；第二，在 `const map` 上 `operator[]` 根本不可用，因为它是修改操作。所以，只读查找请用 `find`、`count` 或 `contains`，需要带边界检查的访问请用 `at()`——它和 vector 的 `at` 一样，key 不存在时抛出 `std::out_of_range` 异常。

## 换个姿势——std::set 维护唯一有序集合

`std::set` 声明在 `<set>` 头文件中，可以理解为"只有 key 没有 value 的 map"。它的所有元素都是唯一的，并且始终有序。当我们需要去重、判断"某个东西是否属于一个集合"的时候，`set` 就派上用场了。

基本操作和 map 非常类似：

```cpp
#include <iostream>
#include <set>

int main()
{
    std::set<int> s = {5, 3, 1, 4, 2, 3, 1};

    // 重复元素被自动忽略，且元素已排序
    // s: {1, 2, 3, 4, 5}

    s.insert(6);        // 插入
    s.emplace(0);       // 原地构造插入
    s.erase(3);         // 按 key 删除

    // 查找
    if (s.contains(4)) {            // C++20
        std::cout << "4 is in the set\n";
    }

    if (s.count(2)) {               // 所有 C++ 版本通用
        std::cout << "2 is in the set\n";
    }

    auto it = s.find(1);
    if (it != s.end()) {
        std::cout << "Found: " << *it << "\n";
    }

    return 0;
}
```

你会发现 set 的接口和 map 几乎一模一样，只是没有 `operator[]` 和 `at`——因为 set 没有"值"可以访问，迭代器解引用直接拿到的是 key 本身。另一个小区别是 set 的 `insert` 返回一个 `pair<iterator, bool>`，其中 `bool` 告诉你这次插入是否真的发生了（如果元素已经存在则返回 `false`）。

一个容易被忽略的特性是 set 提供了 `lower_bound` 和 `upper_bound`，可以用来做范围查询。比如找到 set 中所有大于等于 3 且小于 7 的元素：

```cpp
std::set<int> s = {1, 3, 5, 7, 9};
auto lo = s.lower_bound(3);   // 指向 3
auto hi = s.upper_bound(7);   // 指向 9
for (auto it = lo; it != hi; ++it) {
    std::cout << *it << " ";   // 输出: 3 5 7
}
```

## 把键值对过一遍——遍历关联容器

关联容器的遍历和 vector 一样支持 range-for 循环。但 map 的元素类型是 `pair<const Key, Value>`，在 C++11 里你需要通过 `.first` 和 `.second` 来访问键和值：

```cpp
std::map<std::string, int> scores = {
    {"Alice", 95}, {"Bob", 87}, {"Charlie", 72}
};

// C++11 方式
for (const auto& p : scores) {
    std::cout << p.first << ": " << p.second << "\n";
}
```

C++17 引入了**结构化绑定**（structured binding），让我们可以给 pair 的两个成员各取一个名字，代码可读性大幅提升：

```cpp
// C++17 方式——推荐
for (const auto& [name, score] : scores) {
    std::cout << name << ": " << score << "\n";
}
```

`[name, score]` 就是结构化绑定的语法，`name` 绑定到 `pair.first`，`score` 绑定到 `pair.second`。注意这里用 `const auto&` 而不是 `auto`，和遍历 vector 时一样——避免不必要的拷贝。如果你需要在遍历中修改值（注意：key 是 `const` 的，不能修改），把 `const` 去掉即可：

```cpp
// 给所有人加分
for (auto& [name, score] : scores) {
    score += 5;
    // name += "x";  // 编译错误！key 是 const 的
}
```

set 的遍历更简单，因为它只有一个 key：

```cpp
std::set<int> s = {5, 3, 1, 4, 2};
for (const auto& elem : s) {
    std::cout << elem << " ";   // 输出: 1 2 3 4 5（有序）
}
```

## 换个引擎——std::unordered_map

`std::unordered_map` 声明在 `<unordered_map>` 头文件中，功能和 `std::map` 几乎一样——都是键值对容器，都支持 `insert`、`emplace`、`erase`、`find`、`count`、`contains`（C++20）、`operator[]`、`at` 这些操作。但底层数据结构完全不同：`map` 用红黑树，`unordered_map` 用哈希表。

这个区别带来了几个实际影响。查找性能上，`map` 是稳定的 O(log n)，`unordered_map` 平均 O(1) 但最坏情况 O(n)——当大量 key 发生哈希冲突时会退化。元素顺序上，`map` 始终按 key 有序，`unordered_map` 的元素顺序是不可预测的，每次插入或删除都可能导致顺序变化。内存占用上，哈希表通常比红黑树占用更多内存。

那么什么时候用哪个呢？简单的选择标准是这样的：如果你需要按 key 的顺序遍历元素，或者需要 `lower_bound`/`upper_bound` 这类范围查询，用 `map`；如果你只是频繁地做"给一个 key 查一个 value"的操作，不关心顺序，`unordered_map` 更快。绝大多数日常场景下 `unordered_map` 是更合适的选择——毕竟纯粹按键查找的场景远比需要有序遍历的场景多。

```cpp
#include <iostream>
#include <string>
#include <unordered_map>

int main()
{
    std::unordered_map<std::string, int> freq;
    freq["hello"] = 3;
    freq["world"] = 5;
    freq.emplace("cpp", 1);

    // 接口和 map 完全一致
    if (auto it = freq.find("hello"); it != freq.end()) {
        std::cout << it->first << ": " << it->second << "\n";
    }

    // 但遍历顺序不保证
    for (const auto& [word, count] : freq) {
        std::cout << word << " -> " << count << "\n";
    }

    return 0;
}
```

> **踩坑预警**：`unordered_map` 要求 key 类型要么有默认的 `std::hash` 特化，要么你手动提供哈希函数。标准库已经为内置类型（`int`、`double`、`std::string` 等）提供了 `std::hash` 特化，所以这些类型可以直接用作 key。但如果你想把自定义结构体当作 `unordered_map` 的 key，你需要自己实现 `std::hash` 特化和 `operator==`，否则编译直接报错。相比之下，`std::map` 只要求 key 支持 `operator<`（或自定义比较器），门槛更低。如果你发现自定义类型做 key 编译不过，先检查是不是用了 `unordered_map` 却忘了提供哈希函数。

## 实战时间——词频统计与拼写检查

现在我们把 map 和 set 揉到一起，写一个实战程序。第一个功能是词频统计：读入一段文本，用 `std::map` 统计每个单词出现的次数；第二个功能是拼写检查：用一个 `std::set` 存放词典，然后检查输入的单词是否在词典中。

```cpp
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

/// 将字符串按空格拆分成单词列表
std::vector<std::string> split_words(const std::string& text)
{
    std::vector<std::string> words;
    std::istringstream iss(text);
    std::string word;
    while (iss >> word) {
        words.push_back(word);
    }
    return words;
}

/// 使用 map 统计每个单词的出现频率
void word_frequency_demo()
{
    std::string text = "the cat sat on the mat and the cat slept";
    auto words = split_words(text);

    std::map<std::string, int> freq;
    for (const auto& w : words) {
        // operator[] 在这里正好合适：不存在则插入 0，然后 ++ 自增
        ++freq[w];
    }

    std::cout << "=== Word Frequency ===\n";
    for (const auto& [word, count] : freq) {
        std::cout << "  " << word << ": " << count << "\n";
    }
}

/// 使用 set 做简单的拼写检查
void spell_check_demo()
{
    // 构建一个小词典
    std::set<std::string> dictionary = {
        "the", "cat", "sat", "on", "mat", "and", "slept",
        "dog", "ran", "in", "park", "hello", "world"
    };

    std::string text = "the cat danced on the roof";
    auto words = split_words(text);

    std::cout << "\n=== Spell Check ===\n";
    std::cout << "Input: \"" << text << "\"\n";
    for (const auto& w : words) {
        if (!dictionary.contains(w)) {
            std::cout << "  Unknown word: \"" << w << "\"\n";
        }
    }
}

/// 对比 map 和 unordered_map 的遍历顺序
void map_order_demo()
{
    std::map<std::string, int> ordered = {
        {"delta", 4}, {"alpha", 1}, {"charlie", 3}, {"bravo", 2}
    };

    std::cout << "\n=== std::map (ordered) ===\n";
    for (const auto& [key, val] : ordered) {
        std::cout << "  " << key << ": " << val << "\n";
    }
}

int main()
{
    word_frequency_demo();
    spell_check_demo();
    map_order_demo();
    return 0;
}
```

编译运行：

```bash
g++ -std=c++20 -Wall -Wextra -o map_demo map_demo.cpp && ./map_demo
```

预期输出：

```text
=== Word Frequency ===
  and: 1
  cat: 2
  mat: 1
  on: 1
  sat: 1
  slept: 1
  the: 3

=== Spell Check ===
Input: "the cat danced on the roof"
  Unknown word: "danced"
  Unknown word: "roof"

=== std::map (ordered) ===
  alpha: 1
  bravo: 2
  charlie: 3
  delta: 4
```

看词频统计的输出——`map` 自动按 key 的字典序排列了结果，这就是红黑树带来的有序性。词频统计中我们用 `++freq[w]` 来计数，这里 `operator[]` 的"不存在就插入默认值 0"的行为恰好是我们想要的——第一次遇到某个单词时插入 0 然后自增到 1，之后遇到就继续自增。但一定要注意，这种用法只适用于你确实想要"访问时自动创建"的场景，在只读查找中就是坑了。

拼写检查部分，`set` 的 `contains` 方法（C++20）让代码非常清晰——只需一行就能判断某个单词是否在词典中。如果你的编译器不支持 C++20，用 `count` 替代即可：`dictionary.count(w) != 0`。

## 动手试试——练习题

### 练习 1：学生成绩管理

用 `std::map<std::string, int>` 实现一个简单的成绩管理程序：支持添加学生和成绩、按姓名查询成绩、删除学生、列出所有学生及其成绩（按姓名排序）。要求使用 `find` 来判断学生是否存在，而不是 `operator[]`。

```cpp
void add_student(std::map<std::string, int>& db,
                 const std::string& name, int score);
bool get_score(const std::map<std::string, int>& db,
               const std::string& name, int& out_score);
void list_all(const std::map<std::string, int>& db);
```

### 练习 2：用 unordered_map 重写词频统计

把上面实战程序中的 `std::map` 替换成 `std::unordered_map`，观察输出顺序的变化。然后用 `<chrono>` 计时，对比两种实现在处理一个包含 100000 个随机单词的文本时的性能差异。体会一下 O(1) 和 O(log n) 在数据量大时的实际差别。

### 练习 3：集合运算

用两个 `std::set<int>` 分别存储集合 A 和 B，手动实现交集、并集和差集运算。（提示：遍历其中一个 set，用 `contains` 或 `find` 在另一个 set 中查找。）

```cpp
std::set<int> set_union(const std::set<int>& a, const std::set<int>& b);
std::set<int> set_intersection(const std::set<int>& a, const std::set<int>& b);
std::set<int> set_difference(const std::set<int>& a, const std::set<int>& b);
```

## 小结

这一章我们把 C++ 的三个核心关联容器过了一遍。`std::map` 用红黑树存储有序键值对，查找插入删除都是 O(log n)，适合需要按 key 顺序遍历或做范围查询的场景。`std::set` 本质上是"只有 key 的 map"，用来维护有序唯一元素集合，接口和 map 几乎一致。`std::unordered_map` 用哈希表实现，平均 O(1) 的查找速度，适合纯粹的按键查找场景，代价是不保证元素顺序，而且自定义 key 类型需要手动提供哈希函数。

几个关键要点：遍历 map 时优先用 C++17 的结构化绑定 `for (auto& [k, v] : map)` 让代码更清晰；只读查找不要用 `operator[]`，用 `find`、`count` 或 `contains`；不确定用 map 还是 unordered_map 的时候，问问自己需不需要有序遍历——不需要就选 `unordered_map`。

下一章我们要进入 STL 算法库了——排序、查找、变换、统计，标准库提供了一大批通用算法等着我们去用。到时候你会发现，容器加上算法，才是 STL 真正的威力所在。

---

> **参考资源**
>
> - [cppreference: std::map](https://en.cppreference.com/w/cpp/container/map)
> - [cppreference: std::set](https://en.cppreference.com/w/cpp/container/set)
> - [cppreference: std::unordered_map](https://en.cppreference.com/w/cpp/container/unordered_map)
> - [cppreference: structured binding](https://en.cppreference.com/w/cpp/language/structured_binding)
