---
chapter: 10
cpp_standard:
- 11
- 14
- 17
- 20
description: 您已经用了两章的 vector 和 string，这一篇正式介绍它们的出身 STL：容器存数据、迭代器做接口、算法干活，末尾看一眼 at() 与 operator[] 越界时的两种反应
difficulty: beginner
order: 0
platform: host
prerequisites:
- 模板特化初步
reading_time_minutes: 10
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: STL 是什么：容器、算法，和中间的迭代器
---
# STL 是什么：容器、算法，和中间的迭代器

为什么需要模板那一篇的结尾留了一句话：第 10 章的 STL，全靠那一章打底。现在咱们到了。而且您不是空着手来的：OOP 实战里 `vector<unique_ptr<Shape>>` 给画布装了一整章图形，模板篇手写的 `Stack` 底下垫着的也是 `vector`，`std::string` 更是从第一卷跟到现在。这些东西有个共同出身，全都是 STL 的成员。名字天天见，正式介绍却一直欠着，这一篇补上：STL 是哪批东西，里面的角色怎么配合。

## STL 指的是哪批东西

STL 全称 Standard Template Library，标准模板库。来历为什么需要模板那一篇讲过：Stepanov 和 Meng Lee 在惠普实验室用模板写出来的容器和算法库，1994 年 7 月投票收进标准草案，1998 年随第一个正式标准落地。这里补一个名字本身的细节：严格说，STL 指的是 1994 年被收编的那一批，也就是容器、迭代器、算法、函数对象这几族模板；`std::string`、`iostream` 这些虽然也住在标准库里，但不算那批的原始成员。口语里大家常把整个 C++ 标准库都叫 STL，您知道有这层区分就行，分不清也完全不影响写代码。

STL 给咱们的东西，核心是三样角色：**容器**管存（`vector`、`map`、`set` 这些），**算法**管算（`sort`、`find`、`count` 这些），中间靠**迭代器**把两者接起来。另外还有函数对象和适配器两族配件，算法篇会见着。三样里最没有存在感的是迭代器，因为它总是躲在 `begin()` 和 `end()` 里头当配角——咱们直接跑一段代码，它一出场，作用就清楚了。

## 跑一遍，看三样角色怎么配合

场景随便挑一个：记了一周的每日最高气温，想看排序、看 30 度以上的天数。按老习惯，您可能要开个数组、手写两轮循环。现在换 STL 的写法：

```cpp
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

int main() {
    std::vector<int> temps = {28, 31, 26, 33, 29, 30, 7};  // 手滑记错了一天

    std::sort(temps.begin(), temps.end());    // 算法：排序

    std::cout << "sorted: ";
    for (int t : temps) {
        std::cout << t << ' ';
    }
    std::cout << "\n";

    int hot = std::count_if(temps.begin(), temps.end(),
                            [](int t) { return t >= 30; });    // 算法：按条件计数
    std::cout << "days >= 30: " << hot << "\n";

    return 0;
}
```

输出：

```text
sorted: 7 26 28 29 30 31 33
days >= 30: 3
```

三行关键调用，咱们一行一行看。

`std::vector<int> temps = {...}` 是容器在干活：动态数组，自动管理内存，您往里塞多少它管多少。这块您已经熟了。

`std::sort(temps.begin(), temps.end())` 是算法在干活，但请您注意看参数：sort 收的不是 `temps` 这个容器，是两个迭代器。`begin()` 指向第一个元素，`end()` 指向最后一个元素的下一格（对，下一格，那是个占位哨兵，不代表任何元素）。迭代器这个东西，您可以把它当成指针的推广：指向容器里的某个位置，能解引用拿到元素，能 `++` 挪到下一个位置。裸指针对数组本来就能干这些事，STL 把这套动作规范化成"迭代器"，让每种容器都交出一对同样写法的 `begin()`/`end()`。

这么设计的好处马上就来了：算法根本不需要认识容器。`sort` 拿到一段范围，就把这段范围排好，至于数据住在 `vector` 里还是 `deque` 里、装的是 `int` 还是 `std::string`，它一概不关心。不信咱们把同一份 `sort` 用到字符串上：

```cpp
std::vector<std::string> cities = {"Beijing", "Chengdu", "Anshan", "Dali"};
std::sort(cities.begin(), cities.end());

std::cout << "cities: ";
for (const auto& c : cities) {
    std::cout << c << ' ';
}
std::cout << "\n";
```

输出：

```text
cities: Anshan Beijing Chengdu Dali
```

按字典序排好了。您可能觉得这没什么稀奇，但用为什么需要模板那一篇的话说一遍就清楚了：`sort` 对元素类型提的要求只有一条"能比大小"，`int` 满足，`std::string` 也满足，把要求留下、把类型抽掉，一份算法就伺候了所有满足要求的类型。模板那章讲的是这件事怎么变成可能，STL 是官方替咱们把常用的那一大批全写好了。

咱们塞给 `std::count_if` 的 `[](...){...}` 是 lambda 表达式，等于把"什么算 30 度以上"这个判断现场写成一小段代码交给算法。它是算法篇的主角，这里咱们先当黑盒用：给定范围和条件，返回满足条件的元素个数。

## at() 和 operator[]：越界时的两种反应

趁手上的例子还在，还有一个跟"出错"有关的行为值得现在就知道。咱们故意越界访问一次，用 `at()`：

```cpp
#include <stdexcept>

try {
    std::cout << temps.at(100) << "\n";    // temps 只有 7 个元素
} catch (const std::out_of_range& e) {
    std::cout << "caught: " << e.what() << "\n";
}
```

输出：

```text
caught: vector::_M_range_check: __n (which is 100) >= this->size() (which is 7)
```

程序没有崩，打印了一句说明，接着往下跑。这里发生的事叫"抛异常"：`at()` 越界时抛出一个叫 `std::out_of_range` 的对象，代码里那对 `try`/`catch` 负责把它接住。这套语法的细节您现在不用深究，扫一眼结构就行——异常是 C++ 的一整块内容，这一卷后面有专门一章拆开讲，到时候 `at()` 这个例子还会回来。眼下记住的是一条对比：`at()` 的行为是确定的，越界必抛 `std::out_of_range`，而且抛出的**类型**是标准白纸黑字保证的。`what()` 里的具体措辞是 libstdc++（GCC 家的标准库）自己写的，MSVC 和 libc++ 上是另外的英文句子，写代码时咱们该依赖的是类型，不是文案。

那咱们平时下标为什么敢用 `temps[100]` 这种写法？因为 `operator[]` 不做边界检查——越界了它是未定义行为，可能崩、可能吐垃圾值、也可能看起来没事。`at()` 等于多付一次边界检查，换回一个确定的行为。调试和写对外接口时这笔开销很值；确认不会越界的热路径里，咱们才敢用 `[]` 裸奔。

咱们上面三段代码拼起来就是一份完整可跑的程序，点开直接试：

<OnlineCompilerDemo
  title="STL 三样角色配合 stl_overview.cpp"
  source-path="code/examples/vol1/30_stl_overview.cpp"
  description="在线跑一遍容器、迭代器、算法的配合。您可以试着把 temps 里那个手滑记错的 7 改成 35，看两行输出各自怎么变。"
  run-options="-O2 -std=c++17"
  allow-run
/>

## 常用容器速览和这一章的路线

三样角色里，算法和迭代器都是配角，容器才是您天天要做的选择。先把这一章会碰面的容器摆出来，每个认个脸：

| 容器                                       | 干什么用的                                              |
| ------------------------------------------ | ------------------------------------------------------- |
| `vector`                                   | 动态数组，连续存储，随机访问 O(1)，不确定就用它         |
| `array`                                    | 定长数组，大小编译期定死，没有动态分配的开销            |
| `deque`                                    | 双端队列，两头增删都快，中间插入删除照样费劲            |
| `list`                                     | 双向链表，任意位置插删 O(1)，但随机访问不行             |
| `map` / `set`                              | 有序键值对 / 有序不重复集合，红黑树实现，操作 O(log n)  |
| `unordered_map` / `unordered_set`          | 哈希表实现，平均 O(1)，不保证元素顺序                   |

表里每个词后面都藏着取舍，这一章就按这个顺序把它们逐个拆开。先花一整篇吃透 `vector`，它是出场率最高的容器，也是"不确定就用它"这句话的本体。然后是 `map`、`set`、`unordered_map` 三兄弟，解决"给一个 key 查一个结果"的需求。接着轮到算法库和它的搭档 lambda。最后把容器选择、迭代器失效这些跨容器的问题收拢到一篇。您会发现这一章的代码量大、机制少，因为模板那一章把机制的地基已经打完了，现在更多是在认识工具、练习手感。

## 练习

### 练习 1：换个算法

请您把 `count_if` 换成 `count`（不带条件的版本），先想清楚它统计的是什么，再跑一遍验证您的预期。

### 练习 2：亲眼看一次未定义行为

把 `temps.at(100)` 改成 `temps[100]`，多跑几次，也可以换 `-O0` 和 `-O2` 各跑跑，记录您看到的行为。看完了记得改回 `at()`——这一趟没有标准答案，这正是未定义行为的含义。

### 练习 3：讲给朋友听

不写代码，口头回答：`std::sort` 为什么能对 `vector<int>` 和 `vector<std::string>` 都生效？请您用模板那章"把要求留下，把类型抽掉"的话头来说。
