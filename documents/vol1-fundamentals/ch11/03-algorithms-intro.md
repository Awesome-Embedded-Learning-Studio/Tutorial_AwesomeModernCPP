---
title: "算法库初见"
description: "快速上手 algorithm 中的常用算法，配合 lambda 表达式实现灵活的数据处理"
chapter: 11
order: 3
difficulty: beginner
reading_time_minutes: 15
platform: host
prerequisites:
  - "关联容器快速上手"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# 算法库初见

前面两章，我们把 `vector` 和关联容器的基本操作都过了一遍。现在问题来了——当你需要对一堆数据做排序、查找、过滤、统计的时候，第一反应是不是写一个 for 循环？

说实话，很多人的直觉确实是手写循环。但 C++ 标准库的 `<algorithm>` 头文件里躺着上百个经过反复优化和测试的通用算法，用 STL 算法替代手写循环，代码更短、bug 更少、意图更清晰，很多时候性能还更好。（毕竟久经考验）

这一章我们从实际需求出发，把最常用的一批算法全部上手实操一遍。过程中会频繁用到 lambda 表达式——它是配合 STL 算法的最佳搭档，所以我们先花一点时间把它搞明白。

> **学习目标**
>
> 完成本章后，你将能够：
>
> - [ ] 理解 lambda 表达式的基本语法和捕获方式
> - [ ] 使用 `std::sort`、`std::stable_sort` 对数据排序
> - [ ] 使用 `std::find`、`std::find_if`、`std::binary_search`、`std::lower_bound` 查找元素
> - [ ] 使用 `std::copy`、`std::transform`、`std::replace`、`std::remove` 修改数据
> - [ ] 使用 `std::accumulate`、`std::count`、`std::min_element`、`std::max_element` 做统计

## 先认识我们的搭档——lambda 表达式

STL 算法经常需要一个"判断条件"或者"操作方式"作为参数——比如"按什么规则排序"、"什么条件的元素要找出来"。在 C++11 之前，这个角色由函数指针或函数对象来承担，写起来啰嗦又不直观。lambda 表达式彻底改变了这个局面。

lambda 的完整语法是 `[capture](parameters) -> return_type { body }`，其中返回类型可以省略（编译器自动推导），所以最常见的写法就是 `[capture](params) { body }`。方括号里的 `capture` 决定了 lambda 如何访问外部变量，这是最容易出错的地方。

`[=]` 表示以值捕获的方式把所有用到的外部变量拷贝一份进来——修改它们不会影响外部。`[&]` 表示以引用捕获的方式拿进来——操作的就是外部变量本身。`[x, &y]` 是混合捕获——`x` 按值拷贝，`y` 按引用传递。实际开发中，最推荐的做法是显式列出要捕获的变量，而不是用 `[=]` 或 `[&]` 一把梭，代码意图更清晰，也不容易意外修改外部状态。

```cpp
std::vector<int> data = {5, 3, 1, 4, 2};
int threshold = 3;

// 按值捕获 threshold
auto is_above = [threshold](int x) { return x > threshold; };
int count = std::count_if(data.begin(), data.end(), is_above);
// count == 2

// 引用捕获，累加到外部变量
int sum = 0;
std::for_each(data.begin(), data.end(), [&sum](int x) { sum += x; });
// sum == 15
```

> **踩坑预警**：当 lambda 以引用方式捕获局部变量时，如果 lambda 的生命周期超过了该局部变量，就会产生悬垂引用——引用指向的内存已经被释放了。这种情况在异步回调和存储 lambda 的场景中尤其常见。如果你的 lambda 需要被存储或者传递到其他线程，优先用值捕获或者显式列出要按值捕获的变量。

## 排个序——std::sort 与 std::stable_sort

排序大概是算法库里出场率最高的操作了。`std::sort` 接受两个迭代器（从 C++20 开始可以直接传容器），默认按升序排列。底层是 Introsort——结合了快排、堆排和插入排序的优点，平均和最坏时间复杂度都是 O(n log n)：

```cpp
std::vector<int> v = {5, 2, 8, 1, 9, 3};

// 默认升序
std::sort(v.begin(), v.end());
// v: {1, 2, 3, 5, 8, 9}

// 降序——传第三个参数，一个比较 lambda
std::sort(v.begin(), v.end(), [](int a, int b) { return a > b; });
// v: {9, 8, 5, 3, 2, 1}
```

第三个参数就是一个 lambda——它接收两个元素，返回 `true` 表示第一个参数应该排在第二个参数前面。这就是"自定义排序规则"的标准写法，后面你会反复看到这个模式。

`std::stable_sort` 和 `sort` 的区别在于"稳定性"——当两个元素比较结果相等时，`stable_sort` 保证它们保持原来的相对顺序。比如你先按成绩排序，再按班级排序，第二次排序时同一个班级内的学生仍然保持成绩从高到低的顺序。`stable_sort` 的代价是时间和空间开销略大，但对于需要保持排序稳定性的场景来说无可替代。

> **踩坑预警**：给 `sort` 传入的比较函数必须满足"严格弱序"（strict weak ordering）。简单说就是：`comp(a, a)` 必须返回 `false`，如果 `comp(a, b)` 为 `true` 则 `comp(b, a)` 必须为 `false`，传递性也必须成立。如果你写了 `<=` 而不是 `<`，在某些标准库实现上会导致未定义行为——可能死循环，可能崩溃，也可能只是排序结果不对。所以比较函数永远用 `<`（升序）或 `>`（降序），不要用 `<=` 或 `>=`。

## 找东西——std::find 家族与二分查找

### 线性查找

`std::find` 在范围内线性搜索等于指定值的第一个元素，返回指向它的迭代器；找不到就返回 `end()`。`std::find_if` 类似，但判断条件由一个 lambda 决定：

```cpp
std::vector<std::string> names = {"Alice", "Bob", "Charlie", "David"};

// find：查找等于指定值的元素
auto it1 = std::find(names.begin(), names.end(), "Charlie");
// it1 指向 "Charlie"

// find_if：查找满足条件的第一个元素
auto it2 = std::find_if(names.begin(), names.end(),
    [](const std::string& s) { return s.size() > 4; });
// it2 指向 "Alice"
```

线性查找的时间复杂度是 O(n)，不管数据有没有排序都能用。

### 二分查找

如果你手上的数据已经排好序了，用二分查找效率高得多——O(log n)。`std::binary_search` 返回一个 `bool`，告诉你这个值存不存在，但不会告诉你它在哪。如果你需要知道具体位置，用 `std::lower_bound`，它返回指向第一个大于等于目标值的元素的迭代器：

```cpp
std::vector<int> v = {1, 3, 5, 7, 9, 11};

bool found = std::binary_search(v.begin(), v.end(), 7);  // true
auto it = std::lower_bound(v.begin(), v.end(), 6);
// *it == 7，即第一个 >= 6 的元素
```

在无序数据上调用 `lower_bound` 或 `binary_search` 不会报错，但结果是未定义的——属于那种"编译能过、运行不崩、但结果不可信"的 bug，调试起来格外折磨人。

## 改一改——拷贝、变换、替换、删除

`std::copy` 把一个范围内的元素拷贝到目标位置。`std::transform` 更强大——它在拷贝的同时对每个元素应用一个变换函数。`std::replace` 把范围内等于某个值的元素替换成另一个值：

```cpp
std::vector<int> src = {1, 2, 3, 4, 5};

// copy
std::vector<int> dst;
std::copy(src.begin(), src.end(), std::back_inserter(dst));
// dst: {1, 2, 3, 4, 5}

// transform：每个元素乘以 10
std::vector<int> multiplied;
std::transform(src.begin(), src.end(), std::back_inserter(multiplied),
    [](int x) { return x * 10; });
// multiplied: {10, 20, 30, 40, 50}

// replace：把所有 3 替换成 99
std::vector<int> v = {1, 3, 5, 3, 7};
std::replace(v.begin(), v.end(), 3, 99);
// v: {1, 99, 5, 99, 7}
```

这里出现了一个新面孔 `std::back_inserter`——它是一个插入迭代器，对它赋值就等于调用容器的 `push_back`。这样 `copy` 和 `transform` 就不需要目标容器预先分配好空间了。

### 再谈 remove-erase

上一章讲 `vector` 的时候我们用过 remove-erase 惯用法。现在我们把原理讲得更透一些。`std::remove` 把范围内所有不等于目标值的元素挪到前面，然后返回一个迭代器指向"新逻辑末尾"——这个过程不改变容器的大小，也不调用析构函数，纯粹是在已知的内存上移动元素。之后你再用容器的 `erase` 把新末尾到旧末尾之间的元素真正删掉，两步走才算完成：

```cpp
std::vector<int> v = {1, 2, 3, 2, 4, 2, 5};

auto new_end = std::remove(v.begin(), v.end(), 2);
// v 的内容可能是: {1, 3, 4, 5, ?, ?, ?}
//                   ^new_end         ^v.end()

v.erase(new_end, v.end());
// v: {1, 3, 4, 5}
```

`std::remove_if` 是同样的套路，只是判断条件由 lambda 决定。从 C++20 开始，`std::erase(v, value)` 和 `std::erase_if(v, pred)` 一步到位，如果编译器支持 C++20，直接用新写法就好。

## 算一算——累积、计数、极值

最后一组常用算法做的是"把一堆数据归纳成一个值"。`std::accumulate`（需要 `<numeric>` 头文件）对范围内的元素逐个累加，初始值由你指定——它还可以接受一个自定义的二元操作来求乘积、拼接字符串等。`std::count` / `std::count_if` 统计等于指定值或满足条件的元素个数。`std::min_element` / `std::max_element` 分别返回指向最小和最大元素的迭代器：

```cpp
std::vector<int> v = {3, 1, 4, 1, 5, 9, 2, 6};

int sum = std::accumulate(v.begin(), v.end(), 0);          // 31
int product = std::accumulate(v.begin(), v.end(), 1,       // 6480
    std::multiplies<int>());
int ones = std::count(v.begin(), v.end(), 1);               // 2
int above_4 = std::count_if(v.begin(), v.end(),             // 3
    [](int x) { return x > 4; });

auto min_it = std::min_element(v.begin(), v.end());  // *min_it == 1
auto max_it = std::max_element(v.begin(), v.end());  // *max_it == 9
```

注意 `accumulate` 的初始值类型决定了整个计算的返回类型。传 `0` 得到 `int`，传 `0.0` 得到 `double`，传 `0LL` 得到 `long long`。如果你的 vector 里存的是大整数，初始值传 `0` 就有溢出风险——这是一个经典的踩坑点。

## 上号——综合实战：学生成绩处理

现在我们把这一章涉及的所有算法和 lambda 表达式揉到一个实战程序里。场景很简单：处理一批学生的成绩数据，完成排序、查找优秀学生、计算平均分、过滤不及格这几项操作。

```cpp
#include <algorithm>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

struct Student {
    std::string name;
    double score;
};

void print_student(const Student& s)
{
    std::cout << "  " << s.name << ": " << s.score << "\n";
}

int main()
{
    std::vector<Student> students = {
        {"Alice",   92.5},
        {"Bob",     58.0},
        {"Charlie", 76.0},
        {"Diana",   88.5},
        {"Eve",     45.0},
        {"Frank",   95.0},
        {"Grace",   71.5},
    };

    // --- 1. 按成绩从高到低排序 ---
    std::sort(students.begin(), students.end(),
        [](const Student& a, const Student& b) { return a.score > b.score; });

    std::cout << "=== Ranking (high to low) ===\n";
    for (const auto& s : students) { print_student(s); }

    // --- 2. 查找最高分的学生 ---
    auto top = std::max_element(students.begin(), students.end(),
        [](const Student& a, const Student& b) { return a.score < b.score; });
    std::cout << "\nTop student: " << top->name
              << " (" << top->score << ")\n";

    // --- 3. 计算平均分 ---
    double sum = std::accumulate(students.begin(), students.end(), 0.0,
        [](double acc, const Student& s) { return acc + s.score; });
    std::cout << "Average score: "
              << sum / static_cast<double>(students.size()) << "\n";

    // --- 4. 统计及格和不及格的人数 ---
    int passing = std::count_if(students.begin(), students.end(),
        [](const Student& s) { return s.score >= 60.0; });
    std::cout << "Passing: " << passing
              << ", Failing: " << static_cast<int>(students.size()) - passing
              << "\n";

    // --- 5. 过滤不及格的学生（remove-erase）---
    std::vector<Student> filtered = students;
    auto it = std::remove_if(filtered.begin(), filtered.end(),
        [](const Student& s) { return s.score < 60.0; });
    filtered.erase(it, filtered.end());

    std::cout << "\n=== Passing students ===\n";
    for (const auto& s : filtered) { print_student(s); }

    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra -o algo_demo algo_demo.cpp && ./algo_demo
```

预期输出：

```text
=== Ranking (high to low) ===
  Frank: 95
  Alice: 92.5
  Diana: 88.5
  Charlie: 76
  Grace: 71.5
  Bob: 58
  Eve: 45

Top student: Frank (95)
Average score: 75.2143
Passing: 5, Failing: 2

=== Passing students ===
  Frank: 95
  Alice: 92.5
  Diana: 88.5
  Charlie: 76
  Grace: 71.5
```

整个程序从排序到统计到过滤，全程没有出现手写的 for 循环做数据操作——这就是 STL 算法的力量。每个操作的意图一眼就能看出来：`sort` 就是排序，`max_element` 就是找最大，`count_if` 就是按条件计数，`remove_if` + `erase` 就是按条件删除。对比手写循环，意图表达要清晰得多。

## 动手试试——练习题

### 练习 1：多字段排序

定义一个结构体 `Employee`，包含 `name`（`std::string`）、`department`（`std::string`）和 `salary`（`int`）。创建一个包含若干员工的 vector，实现先按部门名字典序排序，同一个部门内按薪资降序排序。提示：lambda 里先比较部门，部门相同时再比较薪资。

```cpp
struct Employee {
    std::string name;
    std::string department;
    int salary;
};
```

### 练习 2：文本处理管道

给定一个 `std::vector<std::string>` 表示若干行文本，用 STL 算法实现一个简单的文本处理管道：把所有空行删掉（`remove_if`），把每一行转成全小写（`std::transform` 逐字符处理），然后按字典序排序并去重（`std::unique` + `erase`）。每一步都用一个独立的算法调用完成，不要写手动的 for 循环。

```cpp
std::vector<std::string> lines = {
    "Hello World", "", "hello world", "Goodbye", "GOODBYE", "", "Alice"
};
```

## 小结

这一章我们把 `<algorithm>` 和 `<numeric>` 里最常用的一批算法过了一遍。排序用 `std::sort`，需要稳定性时用 `std::stable_sort`。查找分两路走：无序数据用 `std::find` / `std::find_if` 做线性搜索，有序数据用 `std::binary_search` / `std::lower_bound` 做二分查找。修改序列靠 `std::copy`、`std::transform`、`std::replace`，删除元素用 remove-erase 惯用法。统计归纳则有 `std::accumulate`、`std::count` / `std::count_if`、`std::min_element` / `std::max_element`。

贯穿所有这些算法的是一个核心理念：不要手写循环来表达"做什么"，而是用算法的名字直接声明意图。配合 lambda 表达式，我们可以灵活定制比较规则、过滤条件、变换逻辑，同时保持代码的可读性。

下一章我们会继续深入 STL，看看更多容器与算法搭配使用的经典模式。

---

> **参考资源**
>
> - [cppreference: <algorithm>](https://en.cppreference.com/w/cpp/algorithm)
> - [cppreference: <numeric>](https://en.cppreference.com/w/cpp/header/numeric)
> - [cppreference: Lambda expressions](https://en.cppreference.com/w/cpp/language/lambda)
