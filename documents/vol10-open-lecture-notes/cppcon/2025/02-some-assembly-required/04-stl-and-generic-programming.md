---
chapter: 2
conference: cppcon
conference_year: 2025
cpp_standard:
- 17
- 20
description: 'CppCon 2025 演讲笔记 —— C++: Some Assembly Required by Matt Godbolt'
difficulty: intermediate
order: 4
platform: host
reading_time_minutes: 13
speaker: Matt Godbolt
tags:
- cpp-modern
- host
- intermediate
talk_title: 'C++: Some Assembly Required'
title: STL 与泛型编程的本质
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW?p=2
video_youtube: https://www.youtube.com/watch?v=zoYT7R94S3c
---
# 从 STL 的起源重新理解"泛型"到底在干什么

回顾自己的C++学习历程，笔者注意到，市场上不少的C++教程，仅仅将对 STL 的理解停留在"容器 + 算法 + 迭代器"这个三件套的层面，把它当作一个工具箱：需要什么容器就 `#include` 什么，需要排序就调 `std::sort`，用起来确实方便，也确实担当得起"标准库"这三个字（大家干活都是直接用，我猜测除非出问题了，不会有人一边写代码还要一边默念底下的模板实现！）。但很少有人想过它为什么被设计成这样。顺着 Stepanov<RefLink :id="1" preview="Matt Godbolt, C++: Some Assembly Required, CppCon 2025" /> 的历史往下挖，我们会发现一件事——STL 从一开始就不是为了"提供容器"而存在的，它的终极目标是写出一个**一劳永逸的排序算法**。

这个说法乍一听有点奇怪，排序算法有什么好一劳永逸的？学数据结构的时候，快排、归排、堆排，哪个不是针对数组写的？但如果写了一个只能对 `int[]` 排序的快排，那 `double[]` 怎么办？`std::string` 数组怎么办？自定义结构体数组怎么办？常见的做法是复制粘贴，把 `int` 换成 `T`，然后套个 `template`。但 Stepanov 在八十年代初就在想一个更极端的问题：能不能写一个排序，**完全不知道它要排序的是什么东西**，但它就是能工作？

这个想法在今天看来好像就是模板，没什么稀奇。但放到当时的语境里看就不一样了。同样是处理"泛型算法"这个问题，Knuth 在《计算机程序设计艺术》<RefLink :id="2" preview="Donald Knuth, The Art of Computer Programming, 1968" /> 里的做法是自己发明了一台**假设计算机**<RefLink :id="6" preview="Wikipedia, MIX (abstract machine)" />（叫 MIX）及其汇编语言 MIXAL，然后用这套机器语言来精确实现和分析所有算法的运行时间与内存占用<RefLink :id="7" preview="Knuth, MMIX page, purpose of machine language in TAOCP" />。这条路的核心思想是：设计一个足够抽象的机器模型，算法在这个模型上跑，从而能够精确衡量每一个操作的成本。而 Stepanov 走了完全相反的路——他不需要抽象机器，他需要抽象的是**算法所依赖的那些操作本身**。排序不需要知道你在排什么，它只需要知道：能比较大小、能交换位置。只要这两件事能做，排序就能工作。

理解了这个区别，很多之前模糊的概念就清晰了。比如迭代器到底为什么存在——迭代器根本不是什么"泛型指针"，它是 Stepanov 用来**把算法和数据结构解耦的契约**。算法不直接操作容器，它操作迭代器；迭代器提供哪些操作，算法就只依赖哪些操作。这样算法就真的做到了"一劳永逸"。

更有意思的是，Stepanov 最早实现这些想法的时候，连 C++ 都没用。他在 1981 年的第一篇论文里用的是一门叫 **Tecton**<RefLink :id="3" preview="Kapur, Musser, Stepanov, Tecton language, 1981" /> 的语言——这是他与 Deepak Kapur、David Musser 合作设计的，目的纯粹是为了表达泛型编程的概念。这个细节说明"泛型编程"这个思想是先于语言存在的。不是 C++ 有了模板所以才有泛型编程，而是 Stepanov 先有了这个想法，然后他需要一门语言来表达它，先是 Tecton，后来是 Scheme、Ada，最后才到了 C++。模板作为 C++ 的核心特性，确实难用——SFINAE 和 concepts 的报错让很多人头疼——但换个角度想，模板只是 Stepanov 用来实现"一劳永逸算法"梦想的工具而已。理解它为什么被设计成这样，就不会那么抵触了。

顺着这个思路，我们可以做一个实验来验证"算法只依赖操作契约"到底意味着什么。下面这段代码没有用任何 STL 容器，纯粹用原生数组来跑 `std::sort`：

```cpp
#include <algorithm>
#include <iostream>

int main() {
    int arr[] = {5, 3, 1, 4, 2};

    // std::sort 不关心你传的是什么容器
    // 它只关心：迭代器是不是 RandomAccessIterator（能不能做加减法、能不能解引用）
    // 元素能不能用 operator< 比较、能不能 swap 和移动
    std::sort(std::begin(arr), std::end(arr));

    for (int x : arr) {
        std::cout << x << ' ';
    }
    // 输出: 1 2 3 4 5
}
```

这看起来平平无奇，但仔细想——`std::sort` 的实现里没有任何一行代码知道 `arr` 是个数组。它看到的只是两个指针（在这个场景下迭代器就是指针），它需要对这些指针做 `++`、`--`、`+=`、`-=`、`*`、`<` 这些操作——这其实是 **RandomAccessIterator**<RefLink :id="5" preview="cppreference, std::sort, RandomAccessIterator requirements" /> 的完整要求集（随机访问 + 解引用 + 比较），外加值类型的 `swap` 和移动语义，排序才能跑。这就是 Stepanov 当初想要的东西。

然后再进一步，试试自定义类型：

```cpp
#include <algorithm>
#include <iostream>
#include <string>

struct Person {
    std::string name;
    int age;
};

// 算法不关心 Person 是什么，它只关心能不能比较
// 这里我们告诉编译器——你可以比较两个Person对象，而且可以更加具体的说
// 是根据年龄比较的！
bool operator<(const Person& a, const Person& b) {
    return a.age < b.age;
}

int main() {
    Person people[] = {
        {"Alice", 30},
        {"Bob", 25},
        {"Charlie", 35}
    };

    std::sort(std::begin(people), std::end(people));

    for (const auto& p : people) {
        std::cout << p.name << ": " << p.age << '\n';
    }
    // 输出:
    // Bob: 25
    // Alice: 30
    // Charlie: 35
}
```

`std::sort` 依然不知道 `Person` 是什么。它只知道 `*it < *it` 这个表达式能编译通过。你提供 `<`，它就能排；你不提供，编译器就报错——报错信息确实难看，但这个行为本身是非常干净的。（后续的现代C++抽象的一小部分工作，就是在试图解决这种报错没眼看的问题！）

到这里就能理解为什么 STL 被称为"泛型库"而不是"容器库"了。容器只是载体，核心是那些算法。而算法之所以能泛型，是因为它们被设计成只依赖最小化的操作集合。这个思想不是 C++ 特有的，Stepanov 在 Tecton 里就验证过了，后来又在 Scheme 和 Ada 里各验证了一遍，最后发现 C++ 的模板系统恰好能最直接地表达这个思想，才有了我们今天看到的 STL。我们学 STL 的时候，可以把精力花在 `vector`、`map`、`unordered_map` 怎么用上，但是真的别只是，更加值得花时间理解的是算法那一层。容器可以换——甚至可以用自己的数据结构——但算法的设计哲学才是整个 STL 的灵魂。

---

# 从显式实例化到隐式实例化：STL 差点没能进 C++ 的故事

看到这段历史的时候特别有感触。我们天天写模板、天天享受隐式实例化带来的便利，但很少有人想过——如果当年 Bjarne 没有坚持自己的直觉，我们今天写的 C++ 可能完全是另一个样子。

## 先搞清楚"显式实例化"到底长什么样

在讲这个故事之前，有必要先弄明白 Stepanov 当初在 Ada 里看到的"显式实例化"是什么意思——很多人对这个概念的理解一直是模糊的。

所谓显式实例化，就是你在使用一个泛型函数之前，必须提前告诉编译器："我要一个 int 版本的，我要一个 double 版本的。"编译器不会帮你自动推导，你不说它就不生成。而我们现在在 C++ 里写的模板呢？写一个 `template<typename T>` 的函数，调用的时候传个 `int` 进去，编译器自己就帮你把 `T` 替换成 `int` 然后生成对应代码了，这就是隐式实例化。

为了直观感受这个差异，我们看个对比。首先是模拟"显式实例化"风格的写法——当然这不是真正的 Ada 语法，但用 C++ 的概念来表达这个意思：

```cpp
// 模拟 Ada 风格的显式实例化
// 你必须提前声明"我要哪些类型的版本"
template<typename T>
T my_accumulate(T* begin, T* end, T init) {
    for (T* p = begin; p != end; ++p) {
        init = init + *p;
    }
    return init;
}

// 显式实例化声明：告诉编译器"我需要这两个版本"
template int my_accumulate<int>(int*, int*, int);
template double my_accumulate<double>(double*, double*, double);

int main() {
    int arr[] = {1, 2, 3, 4, 5};
    // 编译器看到调用，发现已经有 int 版本的实例了，直接用
    int sum = my_accumulate(arr, arr + 5, 0);

    // double arr2[] = {1.0, 2.0, 3.0};
    // double sum2 = my_accumulate(arr2, arr2 + 3, 0.0);
    // 如果取消上面两行注释，但没有提前声明 double 版本，
    // 在纯显式实例化的模型下，这会直接报错
}
```

然后是我们现在习以为常的隐式实例化，也就是 C++ 实际的做法：

```cpp
#include <iostream>

template<typename T>
T my_accumulate(T* begin, T* end, T init) {
    for (T* p = begin; p != end; ++p) {
        init = init + *p;
    }
    return init;
}

int main() {
    int arr1[] = {1, 2, 3, 4, 5};
    int sum1 = my_accumulate(arr1, arr1 + 5, 0);
    std::cout << sum1 << "\n";  // 15

    double arr2[] = {1.5, 2.5, 3.5};
    double sum2 = my_accumulate(arr2, arr2 + 3, 0.0);
    std::cout << sum2 << "\n";  // 7.5

    // 你甚至可以传一个从来没提前声明过的类型，
    // 编译器在调用点自动推导、自动生成
    long arr3[] = {10L, 20L, 30L};
    long sum3 = my_accumulate(arr3, arr3 + 3, 0L);
    std::cout << sum3 << "\n";  // 60
}
```

你看，第二种写法里完全没有提前声明"我需要 int 版本、double 版本、long 版本"，编译器在每一个调用点自己推导 `T` 是什么，然后现场生成对应的函数体。这就是隐式实例化的力量。

## 为什么 Stepanov 一开始觉得显式更好

乍一看，显式实例化明明更麻烦，一个天才算法设计师为什么会觉得这样更好？

站在 Stepanov 的角度就清楚了。他当时是从 Ada 和 Scheme 那种更"数学化"的环境过来的。在数学里，定义一个函数的时候，你是很清楚它在什么集合上操作的。`accumulate` 作用于整数序列就是整数版本的，作用于实数序列就是实数版本的，这是两件不同的事情，应该明确地说出来。而且从工程角度，显式实例化让你对"到底生成了哪些代码"有完全的控制权，不会出现模板实例化爆炸这种问题。

这个想法其实一点都不蠢。事实上直到今天，C++ 里也保留了显式实例化的语法（就是上面那个 `template int func<int>(...)` 的写法），在大型的、对编译时间敏感的项目里，把模板实例化集中放在一个 `.cpp` 文件里是一种常见的优化手段。所以 Stepanov 的直觉有他的道理。

## Bjarne 为什么坚持隐式

但 Bjarne 看到了 Stepanov 没看到的东西。

关键在于 STL 的核心设计理念：算法不应该绑定在特定的类型上，它应该绑定在"迭代器所满足的概念"上。`accumulate` 不关心你累加的是 `int` 还是 `double` 还是某种自定义的 `BigNum`，它只关心迭代器能解引用、值类型能做 `+` 和 `=`。

如果用显式实例化，你每想支持一种新类型，就得回去加一行显式实例化声明。这意味着算法的作者必须提前知道所有可能的类型——**但这恰恰违背了泛型编程的初衷啊**！泛型编程的意义就在于"我写一次，你拿去用，不管你是什么类型，只要满足我的要求就行"。泛型编程对于实现程序本身是后验的，编译器认为需要什么，实例化什么样的代码；显式声明却又在这里退一步了！

隐式实例化让这变成了现实：算法作者写模板，类型作者写类型，两边完全解耦，编译器在中间做桥梁。没有这个机制，STL 那种"算法 + 迭代器 + 类型"的三层解耦架构根本搭不起来。

## 回头看其实没那么难

今天我们回头看"显式实例化 vs 隐式实例化"这个争论，答案似乎显而易见。但那是在 80 年代末 90 年代初，C++ 模板本身都还很粗糙，没有人写过像 STL 这样大规模的模板库，没有人知道隐式实例化到底能不能 scale。Bjarne 是在没有任何先例的情况下做出了这个判断，而且他是对的。学 C++ 的时候很容易觉得"这些设计都是理所当然的"，但事实上每一行标准库代码背后都可能有这种"差点就走上了另一条路"的故事。搞清楚这些来龙去脉，比单纯记住语法要有趣得多，也更能帮我们理解"为什么 C++ 是这个样子"。

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="Matt Godbolt"
    title="C++: Some Assembly Required"
    publisher="CppCon 2025"
    :year="2025"
    url="https://www.youtube.com/watch?v=zoYT7R94S3c"
  />
  <ReferenceItem
    :id="2"
    author="Donald E. Knuth"
    title="The Art of Computer Programming, Volume 1: Fundamental Algorithms"
    publisher="Addison-Wesley"
    :year="1968"
    chapter="MIX hypothetical computer and MIXAL assembly language for algorithm analysis"
    url="https://www-cs-faculty.stanford.edu/~knuth/taocp.html"
  />
  <ReferenceItem
    :id="3"
    author="Deepak Kapur, David R. Musser, Alexander A. Stepanov"
    title="Tecton: A Language for Manipulating Generic Objects"
    publisher="Program Specification Workshop, Aarhus, Denmark"
    :year="1981"
    chapter="first implementation of generic programming concepts; co-authored with Kapur and Musser"
    url="https://www.stepanovpapers.com/Tecton.pdf"
  />
  <ReferenceItem
    :id="4"
    author="Alexander Stepanov & Meng Lee"
    title="The Standard Template Library"
    publisher="HP Laboratories Technical Report 95-11"
    :year="1995"
    chapter="original STL proposal; algorithms + iterators + containers"
    url="https://www.stepanovpapers.com/"
  />
  <ReferenceItem
    :id="5"
    author="cppreference.com"
    title="std::sort — Requirements: RandomAccessIterator, ValueSwappable, LessThanComparable"
    publisher="cppreference.com"
    :year="2024"
    url="https://en.cppreference.com/cpp/algorithm/sort"
  />
  <ReferenceItem
    :id="6"
    author="Wikipedia"
    title="MIX (abstract machine) — Knuth's hypothetical computer for TAOCP"
    publisher="Wikipedia"
    :year="2024"
    url="https://en.wikipedia.org/wiki/MIX_(abstract_machine)"
  />
  <ReferenceItem
    :id="7"
    author="Donald E. Knuth"
    title="MMIX — Knuth's official page on MIX/MMIX architecture"
    publisher="Stanford CS"
    :year="2024"
    chapter="purpose of machine language in TAOCP: precise analysis of algorithm speed and memory"
    url="https://cs.stanford.edu/~knuth/mmix.html"
  />
</ReferenceCard>
