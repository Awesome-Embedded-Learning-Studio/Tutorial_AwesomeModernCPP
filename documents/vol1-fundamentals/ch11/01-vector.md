---
title: "std::vector 快速上手"
description: "掌握 vector 的增删改查和容量管理，学会使用最常用的 C++ 动态容器"
chapter: 11
order: 1
difficulty: beginner
reading_time_minutes: 15
platform: host
prerequisites:
  - "错误处理方式对比"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# std::vector 快速上手

前面几章我们把 C++ 的语言核心——类型系统、控制流、函数、类与继承——基本过了一遍。从现在开始，我们要进入一个全新的领域：标准模板库（STL）。STL 提供了一大批现成的容器、算法和迭代器，能让我们少造很多轮子。而在所有容器中，`std::vector` 绝对是出场率最高的那个——动态数组，自动扩容，元素连续存储，随机访问 O(1)。说实话，如果你不确定该用什么容器，用 `vector` 就对了，其他容器都是在特定场景下才有优势。

这一章我们从零开始，把 `vector` 的构造、增删改查、容量管理、遍历方式全部过一遍，最后用一个任务管理器的实战程序把所有知识串起来。

> **学习目标**
>
> 完成本章后，你将能够：
>
> - [ ] 使用多种方式构造 `std::vector`
> - [ ] 掌握 `push_back`、`emplace_back`、`insert`、`erase` 等增删操作
> - [ ] 理解 `size` 与 `capacity` 的区别，合理使用 `reserve` 优化性能
> - [ ] 用 range-for、索引和迭代器三种方式遍历 vector
> - [ ] 运用 remove-erase 惯用法删除满足条件的元素

## 从零开始——构造一个 vector

`std::vector` 有好几种构造方式，我们逐一来看：

```cpp
#include <vector>
#include <string>

std::vector<int> v1;                    // 空 vector
std::vector<int> v2(10);                // 10 个元素，每个为 0
std::vector<int> v3(10, 42);            // 10 个元素，每个为 42
std::vector<int> v4 = {1, 2, 3, 4, 5};  // 初始化列表
std::vector<int> v5(v4);                // 拷贝构造
std::vector<int> v6(std::move(v5));     // 移动构造，接管资源
```

这里有一个值得注意的点：`v2(10)` 创建了 10 个元素，每个值都是 `int()` 也就是 0。这不是"预留 10 个位置但没有元素"，而是真的有 10 个元素在里面。预留空间和实际元素是两个概念，后面讲到 `reserve` 的时候会深入讨论。

> **踩坑预警**：`vector<bool>` 是 `vector` 的一个特化版本，它为了节省空间把每个 `bool` 压缩成了 1 bit。这导致 `vector<bool>` 的很多行为和普通 `vector<T>` 不一样——比如 `operator[]` 返回的不是 `bool&` 而是一个代理对象。如果你需要一个真正的 bool 数组，用 `vector<char>` 或者 `deque<bool>` 更安全。

## 往里面塞东西——添加元素

`vector` 最常用的添加操作是 `push_back`，它在末尾追加一个元素。从 C++11 开始，我们还有了 `emplace_back`，它比 `push_back` 更高效——区别在于 `push_back` 接收一个已经构造好的对象，而 `emplace_back` 接收构造参数，直接在 vector 的内存中原地构造对象，省去了一次移动或拷贝：

```cpp
struct Task {
    std::string name;
    int priority;
    Task(std::string n, int p) : name(std::move(n)), priority(p) {}
};

std::vector<Task> tasks;
tasks.push_back(Task("写代码", 1));   // 先构造临时对象，再移动
tasks.emplace_back("测试", 2);         // 原地构造，无需临时对象
```

对于 `int`、`double` 这样的简单类型，两者性能几乎没区别。但对于含有 `std::string` 或其他需要动态分配内存的成员的类，`emplace_back` 能省掉一次不必要的构造和移动。养成习惯，优先用 `emplace_back`。

如果你需要在中间某个位置插入元素，用 `insert`：

```cpp
std::vector<int> v = {10, 20, 30, 40};
v.insert(v.begin() + 1, 15);  // v: {10, 15, 20, 30, 40}
```

不过要注意，`insert` 在中间插入时需要把后面所有元素往后挪，时间复杂度是 O(n)。如果你发现自己频繁在 vector 的头部或中间插入元素，也许应该考虑换用 `std::deque` 或 `std::list`。

> **踩坑预警**：任何可能导致 vector 重新分配内存的操作（包括 `push_back`、`emplace_back`、`insert`）都会使之前保存的所有迭代器、指针和引用失效。看下面这段代码：

```cpp
std::vector<int> v = {1, 2, 3};
int* p = &v[0];       // 指向第一个元素
v.push_back(4);       // 可能触发扩容！
// *p 现在是未定义行为——p 指向的内存可能已经被释放了
```

如果你需要持有一个 vector 中元素的指针或引用，要么确保之后不会做任何可能触发扩容的操作，要么改用索引来间接访问。

## 取东西出来——访问元素

`vector` 提供了好几种访问元素的方式。用得最多的是 `operator[]`，它和 C 数组一样通过下标访问，不做边界检查。如果你想要边界检查（越界时抛出 `std::out_of_range` 异常），用 `at`：

```cpp
std::vector<int> v = {10, 20, 30, 40, 50};
v[0] = 100;          // 不检查边界
int y = v.at(10);    // 抛出 std::out_of_range
```

日常开发中 `operator[]` 用得更多，但在接收用户输入或外部数据作为索引的场景下，`at` 是一道安全防线。

另外还有几个便捷的访问函数：`front()` 返回第一个元素的引用（等价于 `v[0]`），`back()` 返回最后一个元素的引用（等价于 `v[v.size() - 1]`），`data()` 返回指向底层数组的指针——因为 vector 的元素是连续存储的，`v.data()` 可以直接当作 C 数组来用，在和 C 风格 API 交互的时候特别方便。

> **踩坑预警**：对空的 vector 调用 `front()`、`back()` 或 `operator[]` 都是未定义行为——不会抛异常，而是直接进入 UB 的深渊。`at()` 是唯一会对空 vector 做边界检查的方式。所以在调用 `front()` 或 `back()` 之前，要么确认 vector 不为空，要么先用 `empty()` 检查一下。

## 去掉不要的——删除元素

最简单的是 `pop_back`，它删除末尾的一个元素，返回 `void`——不返回被删除的值。如果你需要那个值，在 `pop_back` 之前先用 `back()` 拿到它。

删除中间的元素用 `erase`，它接受一个迭代器或一个范围：

```cpp
std::vector<int> v = {10, 20, 30, 40, 50};
v.erase(v.begin() + 2);                // v: {10, 20, 40, 50}
v.erase(v.begin() + 1, v.begin() + 3); // v: {10, 50}
```

一次性清空所有元素用 `clear()`——之后 `size` 变成 0 但 `capacity` 不变，也就是说内存没有被释放，只是元素被析构了。如果想把内存也释放掉，可以配合 `shrink_to_fit`。

### Remove-Erase 惯用法

现在问题来了：如果我们想删除 vector 中所有等于某个值的元素，怎么做？答案是 remove-erase 惯用法，这是 C++ 经典范式：

```cpp
#include <algorithm>

std::vector<int> v = {1, 2, 3, 2, 4, 2, 5};
v.erase(std::remove(v.begin(), v.end(), 2), v.end());
// v: {1, 3, 4, 5}
```

`std::remove` 并不是真的删除元素——它把所有不等于 2 的元素移到前面，然后返回一个指向"新逻辑末尾"的迭代器，等于 2 的元素被挤到了后面。然后 `erase` 把从新末尾到旧末尾之间的元素真正删掉。两步走的原因是 STL 的设计哲学："算法不应该直接操作容器的接口"，`std::remove` 只知道迭代器，不知道 vector 的 `erase` 方法。

从 C++20 开始，我们可以用 `std::erase` 一行搞定：`std::erase(v, 2);`。如果你用的是支持 C++20 的编译器，强烈建议用这个新写法。

## 理解 size 和 capacity

`vector` 有两个容易混淆的概念：`size` 是当前实际存储的元素数量，`capacity` 是已经分配的内存能容纳的元素数量，`capacity` 永远大于等于 `size`。当我们不断 `push_back` 导致 `size` 即将超过 `capacity` 时，`vector` 会自动扩容——分配一块更大的内存，把所有元素搬过去，然后释放旧内存。大多数标准库实现的扩容策略是把 `capacity` 翻倍，所以你会看到 capacity 按这样的序列增长：1, 2, 4, 8, 16, 32 ... 每次扩容都涉及一次完整的内存分配和所有元素的拷贝/移动。

如果你事先知道大概要存多少个元素，用 `reserve` 提前分配好足够的空间，能避免多次扩容带来的开销：

```cpp
std::vector<int> v;
v.reserve(1000);  // 一次性分配，capacity 变成 1000，size 仍为 0

for (int i = 0; i < 1000; ++i) {
    v.push_back(i);  // 不会触发任何扩容
}
```

`reserve` 只影响 `capacity`，不影响 `size`。反过来，如果你想释放多余的容量，用 `shrink_to_fit`——不过这是一个非绑定的请求，标准并不保证一定释放内存，但主流实现都会这么做。

## 把元素过一遍——遍历 vector

遍历 vector 有三种常见方式。最推荐的是 range-for 循环（C++11 引入），简洁又安全：

```cpp
std::vector<int> v = {10, 20, 30, 40, 50};

// 只读遍历——养成习惯，只读一律用 const auto&
for (const auto& elem : v) {
    std::cout << elem << " ";
}

// 需要修改元素时去掉 const
for (auto& elem : v) {
    elem *= 2;
}
```

注意这里用 `const auto&` 而不是 `auto`。对于 `int` 来说区别不大，但遍历 `vector<std::string>` 时，`auto` 会触发拷贝，而 `const auto&` 只是引用。如果你需要索引，用传统循环：`for (std::size_t i = 0; i < v.size(); ++i)`。需要配合 STL 算法或做更精细控制时，用迭代器：`for (auto it = v.begin(); it != v.end(); ++it)`。日常开发中 range-for 能覆盖 90% 的遍历需求。

## 实战时间——用 vector 写一个任务管理器

兄弟们，我们把前面学的所有知识点揉到一个实战程序里——支持添加任务、标记完成并删除、列出所有任务、查看容量信息。

```cpp
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

struct Task {
    std::string description;
    bool done;
    Task(std::string desc) : description(std::move(desc)), done(false) {}
};

class TaskManager {
public:
    void add_task(const std::string& desc)
    {
        tasks_.emplace_back(desc);
        std::cout << "  Added: \"" << desc << "\"\n";
    }

    void complete_task(int index)
    {
        if (index < 0 || index >= static_cast<int>(tasks_.size())) {
            std::cout << "  Invalid index: " << index << "\n";
            return;
        }
        tasks_[index].done = true;
        std::cout << "  Completed: \"" << tasks_[index].description << "\"\n";
    }

    void remove_completed()
    {
        // remove-erase 惯用法：删除所有 done == true 的任务
        auto it = std::remove_if(tasks_.begin(), tasks_.end(),
            [](const Task& t) { return t.done; });
        int removed = static_cast<int>(tasks_.end() - it);
        tasks_.erase(it, tasks_.end());
        std::cout << "  Removed " << removed << " completed task(s)\n";
    }

    void list_all() const
    {
        if (tasks_.empty()) {
            std::cout << "  (no tasks)\n";
            return;
        }
        for (std::size_t i = 0; i < tasks_.size(); ++i) {
            std::cout << "  [" << i << "] "
                      << (tasks_[i].done ? "[x]" : "[ ]")
                      << " " << tasks_[i].description << "\n";
        }
    }

    void show_status() const
    {
        std::cout << "  size: " << tasks_.size()
                  << ", capacity: " << tasks_.capacity() << "\n";
    }

private:
    std::vector<Task> tasks_;
};

int main()
{
    TaskManager mgr;

    std::cout << "=== Adding tasks ===\n";
    mgr.add_task("Write vector tutorial");
    mgr.add_task("Review pull requests");
    mgr.add_task("Fix build warnings");
    mgr.add_task("Update documentation");

    std::cout << "\n=== All tasks ===\n";
    mgr.list_all();
    mgr.show_status();

    std::cout << "\n=== Completing tasks ===\n";
    mgr.complete_task(0);
    mgr.complete_task(2);

    std::cout << "\n=== Removing completed ===\n";
    mgr.remove_completed();

    std::cout << "\n=== Remaining tasks ===\n";
    mgr.list_all();
    mgr.show_status();

    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra -o task_manager task_manager.cpp && ./task_manager
```

预期输出：

```text
=== Adding tasks ===
  Added: "Write vector tutorial"
  Added: "Review pull requests"
  Added: "Fix build warnings"
  Added: "Update documentation"

=== All tasks ===
  [0] [ ] Write vector tutorial
  [1] [ ] Review pull requests
  [2] [ ] Fix build warnings
  [3] [ ] Update documentation
  size: 4, capacity: 4

=== Completing tasks ===
  Completed: "Write vector tutorial"
  Completed: "Fix build warnings"

=== Removing completed ===
  Removed 2 completed task(s)

=== Remaining tasks ===
  [0] [ ] Review pull requests
  [1] [ ] Update documentation
  size: 2, capacity: 4
```

注意最后 `capacity` 仍然是 4——`erase` 不会释放内存。这个小细节在实际开发中经常被忽略。

> **踩坑预警**：在 range-for 循环中直接 `erase` 元素会导致未定义行为，因为 `erase` 会使迭代器失效。如果你需要在遍历中删除元素，要么用索引循环从后往前遍历，要么用迭代器循环配合 `erase` 的返回值。不过大多数情况下，先标记、再统一 remove-erase 是更清晰的做法，就像上面代码中 `remove_completed` 那样。

## 动手试试——练习题

### 练习 1：频率计数器

给定一个整数 vector，统计每个值出现的次数。提示：可以排序后遍历，也可以用双重循环（简单实现即可，不需要用到 map）。

```cpp
std::vector<int> data = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5};
// 预期输出：1 appears 2 times, 2 appears 1 times, ...
```

### 练习 2：去重

写一个函数，接收一个已排序的 vector，返回一个去重后的新 vector。要求不使用 `std::unique`，手动实现一遍。

```cpp
std::vector<int> deduplicate(const std::vector<int>& sorted);
// deduplicate({1, 1, 2, 3, 3, 3, 4}) -> {1, 2, 3, 4}
```

### 练习 3：感受 reserve 的威力

分别用"不调用 reserve"和"调用 reserve(100000)"两种方式向 vector 中插入 100000 个元素，用 `<chrono>` 计时并比较两者的耗时。体会一下提前分配内存的威力。

## 小结

这一章我们完整地过了一遍 `std::vector` 的核心操作。构造方式从默认构造到初始化列表到拷贝移动，增删操作从 `push_back`/`emplace_back` 到 `erase` 再到经典的 remove-erase 惯用法，访问方式从 `operator[]` 到 `at` 到 `data()`，容量管理从 `size`/`capacity` 的区别到 `reserve` 的性能优化。最后通过一个任务管理器的实战程序把这些知识点全部串了起来。

几个关键要点：优先使用 `emplace_back` 而非 `push_back`，注意扩容导致迭代器失效的问题，理解 `size` 和 `capacity` 的区别并在合适的时候调用 `reserve`，以及删除满足条件的元素时用 remove-erase 惯用法（或者 C++20 的 `std::erase`）。

下一章我们来看 `std::map` 和 `std::set`——当你需要按键查找或者维护有序集合时，它们就是主力选手了。

---

> **参考资源**
>
> - [cppreference: std::vector](https://en.cppreference.com/w/cpp/container/vector)
> - [cppreference: std::remove](https://en.cppreference.com/w/cpp/algorithm/remove)
> - [cppreference: std::erase (C++20)](https://en.cppreference.com/w/cpp/container/vector/erase2)
