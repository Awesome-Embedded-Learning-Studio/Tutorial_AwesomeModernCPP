---
title: "range-for 循环"
description: "掌握 C++11 引入的 range-for 循环，用最简洁的方式遍历数组和容器"
chapter: 2
order: 3
difficulty: beginner
reading_time_minutes: 10
platform: host
prerequisites:
  - "循环语句"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# range-for 循环

写传统的 for 循环遍历数组时，我们总要做一件事——管好那个索引变量。`for (int i = 0; i < n; ++i)`，这行代码我们写过无数遍，但也写错过无数遍：`<` 写成 `<=` 导致越界访问，`i` 忘了自增导致死循环，数组长度改了但循环条件忘了跟着改......说实话，这种因为手滑引入的 bug 最让人头疼，因为它不是逻辑错误，纯粹是体力活没干好。

C++11 给了我们一个优雅的解决方案：**range-for 循环**。它的核心思路很简单——别让程序员去管索引了，直接告诉编译器"把这个集合里的每个元素给我过一遍"就行。这一章我们就来把 range-for 的用法彻底搞清楚。

## 第一步——认识 range-for 的基本语法

range-for 的语法长这样：

```cpp
for (类型 变量名 : 集合) {
    // 使用变量
}
```

我们用一个最简单的例子来对比一下。假设我们有一个数组，想把每个元素打印出来：

```cpp
#include <iostream>

int main()
{
    int scores[] = {90, 85, 78, 92, 88};

    // 传统 for 循环
    for (int i = 0; i < 5; ++i) {
        std::cout << scores[i] << " ";
    }
    std::cout << std::endl;

    // range-for 循环
    for (int score : scores) {
        std::cout << score << " ";
    }
    std::cout << std::endl;

    return 0;
}
```

运行结果：

```text
90 85 78 92 88
90 85 78 92 88
```

两种写法的输出完全一样，但 range-for 版本少了索引变量 `i`，少了数组长度 `5`，少了 `scores[i]` 的下标访问——也就是说，少了所有可能手滑出错的地方。编译器全帮你算好了。range-for 不挑食，C 风格数组、`std::array`、`std::vector`、`std::string`、花括号初始化列表——基本上所有你能"从头到尾走一遍"的东西它都支持。

## 第二步——搭配 auto 的三种姿势

`auto` 关键字能帮我们省去手写类型的麻烦，但在 range-for 里有三种写法，行为截然不同——搞清楚它们是理解 C++ 值语义与引用语义的一块重要拼图。

**按值访问** `for (auto x : arr)` 每次迭代复制一份元素给 `x`，修改 `x` 不影响原集合。对 `int` 这种小类型无所谓，但遍历大对象时就有性能浪费了。

**按引用访问** `for (auto& x : arr)` 让 `x` 成为原元素的引用，没有复制开销，还能直接修改原元素。

**按 const 引用访问** `for (const auto& x : arr)` 是只读引用，既避免复制又防止意外修改。遍历大对象时的最佳实践，也是泛型代码中的推荐默认选择。

用一个简短的例子来感受三者差异：

```cpp
int nums[] = {1, 2, 3};

// 按值：改副本，原数组不变
for (auto x : nums) { x *= 2; }
// nums 仍是 {1, 2, 3}

// 按引用：直接改原数组
for (auto& x : nums) { x *= 2; }
// nums 变成 {2, 4, 6}

// const 引用：只读遍历，编译器会阻止修改
for (const auto& x : nums) {
    std::cout << x << " ";  // 2 4 6
}
```

> ⚠️ **踩坑预警**
> 千万别在需要修改元素的时候用 `for (auto x : arr)`，否则你改的只是一份副本，原数组纹丝不动。这种 bug 的特点是"编译通过、运行不报错、但结果不对"，属于最难排查的那一类。如果需要在循环里修改元素，一定要用 `auto&`。

## 第三步——range-for 与 C 风格数组的陷阱

range-for 对 C 风格数组原生支持，但有一个重要限制：当数组作为函数参数传递时会退化为指针，此时 range-for 就失效了。

```cpp
void print_array(int arr[])  // arr 在这里其实是指针
{
    // 编译错误！编译器不知道 arr 指向多少个元素
    // for (int x : arr) { ... }
}
```

原因在于 range-for 需要知道集合的起点和终点。数组退化成指针后，编译器丢失了"元素个数"这个信息，没法确定终点在哪。

> ⚠️ **踩坑预警**
> range-for 不能用于裸指针。如果你拿到的是 `int*` 加长度 `size_t n`，只能用传统 for 循环。后续学到 `std::span`（C++20）后会有更优雅的方案。

我们推荐用 `std::array` 替代 C 风格数组——它和 C 数组性能一样，但有标准的 `begin()`/`end()` 接口，和 range-for 配合得天衣无缝：

```cpp
std::array<int, 5> scores = {90, 85, 78, 92, 88};
for (const auto& s : scores) {
    std::cout << s << " ";
}
```

## 第四步——range-for 遍历字符串

`std::string` 也能用 range-for 遍历，每次迭代拿到一个字符。比如统计元音字母：

```cpp
std::string text = "Hello C++ World";
int vowel_count = 0;
for (char c : text) {
    char lower = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
    if (lower == 'a' || lower == 'e' || lower == 'i'
        || lower == 'o' || lower == 'u') {
        ++vowel_count;
    }
}
std::cout << "元音字母个数: " << vowel_count << std::endl;
// 输出: 元音字母个数: 3
```

用引用版本还能原地修改字符串，比如转大写：

```cpp
for (auto& c : text) {
    c = static_cast<char>(
        std::toupper(static_cast<unsigned char>(c)));
}
```

这里的 `static_cast<unsigned char>` 不是多此一举。`std::toupper` 的参数是 `int`，而 C++ 中 `char` 可能是 signed 的——直接传负值字符进去是未定义行为。先转 `unsigned char` 再提升为 `int`，这是处理字符函数时的标准写法。

> ⚠️ **踩坑预警**
> 直接对 `char` 调用 `std::toupper` 而不先转成 `unsigned char`，碰到扩展 ASCII 或中文字符时会产生未定义行为。编译器不会警告你，但结果可能完全不对。养成习惯，字符函数调用前总是先做这个转换。

## C++17 前瞻：结构化绑定

C++17 引入的结构化绑定和 range-for 配合极佳。虽然完整讲解要等到后面容器章节，但我们可以先看一眼：

```cpp
// C++17：遍历键值对容器时直接拆开 key 和 value
// for (const auto& [key, value] : my_map) {
//     std::cout << key << " -> " << value << std::endl;
// }
```

方括号里的 `[key, value]` 把一个包含多个字段的对象"解构"成独立变量，比手动写 `pair.first` 和 `pair.second` 直观得多。暂时看不懂没关系，知道有这个能力就好。

## 幕后机制——range-for 到底做了什么

为什么 range-for 既能用于数组，又能用于 `std::vector`、`std::string` 这些完全不同的类型？答案很简单：编译器会把 range-for 翻译成一个等价的传统循环。

```cpp
// for (auto x : coll) 大致等价于：
{
    auto&& __range = coll;
    for (auto __it = __range.begin(); __it != __range.end(); ++__it) {
        auto x = *__it;
        // 循环体
    }
}
```

编译器做的事情就是调用 `begin()` 拿到起点，调用 `end()` 拿到终点，然后一步步走过去。对于 C 风格数组，编译器知道长度，用首元素指针加长度来充当起止位置。这意味着任何提供了 `begin()` 和 `end()` 的类型都能用 range-for——这也解释了为什么 `std::array` 比 C 风格数组更好用。

## 实战演练——range_for.cpp

我们把前面的用法整合到一个完整的程序里，演示求和、计数、原地修改：

```cpp
// range_for.cpp
// Platform: host
// Standard: C++17

#include <array>
#include <cctype>
#include <iostream>
#include <string>

int main()
{
    // 求和
    std::array<int, 6> data = {3, 7, 1, 9, 4, 6};
    int sum = 0;
    for (const auto& x : data) {
        sum += x;
    }
    std::cout << "总和: " << sum << std::endl;

    // 计数
    int target = 6;
    int count = 0;
    for (const auto& x : data) {
        if (x == target) { ++count; }
    }
    std::cout << "值 " << target << " 出现了 " << count
              << " 次" << std::endl;

    // 原地修改：每个元素翻倍
    std::array<int, 6> doubled = data;
    for (auto& x : doubled) { x *= 2; }
    std::cout << "翻倍后: ";
    for (const auto& x : doubled) {
        std::cout << x << " ";
    }
    std::cout << std::endl;

    // 字符串转大写
    std::string message = "range-for is elegant";
    for (auto& c : message) {
        c = static_cast<char>(
            std::toupper(static_cast<unsigned char>(c)));
    }
    std::cout << "转大写: " << message << std::endl;

    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra -o range_for range_for.cpp
./range_for
```

运行结果：

```text
总和: 30
值 6 出现了 1 次
翻倍后: 6 14 2 18 8 12
转大写: RANGE-FOR IS ELEGANT
```

## 动手试试

### 练习一：找最大值

给定一个 `std::array<int, 8>`，用 range-for 找出最大值并打印。提示：声明 `max_val` 初始化为首元素，遍历比较即可。

```text
数组: 12 3 45 7 23 56 8 19
最大值: 56
```

### 练习二：统计元音

用 range-for 统计 `std::string` 中元音字母（a/e/i/o/u，不区分大小写）的个数。

```text
字符串: "Beautiful C++"
元音个数: 5
```

### 练习三：原地修改

用 range-for 的引用版本，把数组中所有负数取绝对值。

```text
修改前: 3 -7 1 -9 4 -6
修改后: 3 7 1 9 4 6
```

## 小结

这一章我们从传统 for 循环的痛点出发，学习了 range-for 这个 C++11 的语法糖。`for (类型 变量 : 集合)` 让编译器接管索引管理，我们不再需要手写边界条件。搭配 `auto` 时要区分三种形式：`auto` 做值拷贝、`auto&` 做可修改引用、`const auto&` 做只读引用。range-for 不能用于裸指针，因为指针丢失了元素个数信息。底层机制上它就是 `begin()` 和 `end()` 的包装，任何提供了这两个接口的类型都能用。

到这里，第二章的控制流部分就全部讲完了。if/else 分支、switch 多路选择、三种经典循环加上 range-for，这些工具组合起来已经足够让程序处理绝大多数执行流程。下一章我们进入函数的世界——把重复的代码封装起来，让程序结构变得更加清晰。
