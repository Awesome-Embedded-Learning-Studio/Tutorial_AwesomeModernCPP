---
chapter: 2
conference: cppcon
conference_year: 2025
cpp_standard:
- 17
- 20
description: 'CppCon 2025 Talk Notes — C++: Some Assembly Required by Matt Godbolt'
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
title: The Essence of STL and Generic Programming
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW?p=2
video_youtube: https://www.youtube.com/watch?v=zoYT7R94S3c
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/02-some-assembly-required/04-stl-and-generic-programming.md
  source_hash: 99d501e7b41b8b7daf1d6e330f2ddef65dd738d0b2152b402b223035978b4057
  translated_at: '2026-06-24T00:31:21.403632+00:00'
  engine: anthropic
  token_count: 2313
---
# Rethinking "Generic Programming" from the Origins of the STL

Looking back on my journey of learning C++, I have noticed that many tutorials on the market treat the STL merely as a "containers + algorithms + iterators" trio. It is viewed as a toolbox: `#include` whatever container you need, call `std::sort` when you need to sort. It is convenient and certainly lives up to the name "Standard Library" (everyone uses it directly, and I suspect unless something breaks, no one mutters the underlying template implementation while coding!). However, few people consider *why* it was designed this way. Digging into the history with Stepanov<RefLink :id="1" preview="Matt Godbolt, C++: Some Assembly Required, CppCon 2025" />, we discover a fact—the STL was never created just to "provide containers." Its ultimate goal was to write a **sorting algorithm that works once and for all**.

This statement might sound a bit strange at first. What is so "once and for all" about a sorting algorithm? When learning data structures, quicksort, mergesort, and heapsort are all written for arrays, aren't they? But if you write a quicksort that only sorts `int[]`, what about `double[]`? What about arrays of `std::string`? What about arrays of custom structs? The common approach is to copy and paste, replace `int` with `T`, and wrap it in a `template`. But in the early 1980s, Stepanov was thinking about a more extreme question: could we write a sort that **doesn't know what it is sorting at all**, yet still works?

This idea seems like just templates today, nothing special. But in the context of that era, it was different. Facing the same problem of "generic algorithms," Knuth's approach in *The Art of Computer Programming*<RefLink :id="2" preview="Donald Knuth, The Art of Computer Programming, 1968" /> was to invent a **hypothetical computer**<RefLink :id="6" preview="Wikipedia, MIX (abstract machine)" /> (called MIX) and its assembly language, MIXAL, to precisely implement and analyze the running time and memory usage of all algorithms<RefLink :id="7" preview="Knuth, MMIX page, purpose of machine language in TAOCP" />. The core idea of this path is: design an abstract enough machine model, run algorithms on this model, and thus accurately measure the cost of every operation. Stepanov took a completely opposite path—he didn't need an abstract machine; he needed to abstract **the operations themselves that the algorithm relies on**. Sorting doesn't need to know what it is sorting; it only needs to know: it can compare and it can swap. As long as these two things can be done, sorting works.

Understanding this difference clarifies many previously vague concepts. For example, why do iterators exist at all—iterators are not "generic pointers"; they are the **contract Stepanov used to decouple algorithms from data structures**. Algorithms do not operate on containers directly; they operate on iterators. Iterators provide certain operations, and the algorithm relies only on those operations. This way, the algorithm truly achieves the "once and for all" goal.

Even more interestingly, when Stepanov first implemented these ideas, he didn't even use C++. In his first paper in 1981, he used a language called **Tecton**<RefLink :id="3" preview="Kapur, Musser, Stepanov, Tecton language, 1981" />—designed in collaboration with Deepak Kapur and David Musser, purely to express the concepts of generic programming. This detail shows that the idea of "generic programming" predates the language. It's not that C++ had templates and therefore had generic programming; rather, Stepanov had the idea first, then needed a language to express it—first Tecton, then Scheme, then Ada, and finally C++. Templates, as a core feature of C++, are indeed difficult to use—SFINAE and concepts errors give many people a headache—but looking at it from another angle, templates are just the tool Stepanov used to realize his dream of "once and for all algorithms." Understanding why it was designed this way makes it less repulsive.

Following this line of thought, we can do an experiment to verify what "algorithms rely only on operation contracts" actually means. The code below uses no STL containers, purely raw arrays to run `std::sort`:

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

This might seem unremarkable at first glance, but think about it carefully—there isn't a single line of code in the `std::sort` implementation that knows `arr` is an array. It only sees two pointers (in this scenario, iterators are pointers), and it needs to perform operations like `++`, `--`, `+=`, `-=`, `*`, and `<` on them. This actually constitutes the complete set of requirements for a **RandomAccessIterator**<RefLink :id="5" preview="cppreference, std::sort, RandomAccessIterator requirements" /> (random access + dereference + comparison), plus `swap` and move semantics for the value type, for the sort to function. This is exactly what Stepanov envisioned.

Let's take this a step further and try a custom type:

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

`std::sort` still doesn't know what `Person` is. It only knows that the expression `*it < *it` compiles. If you provide `<`, it sorts; if you don't, the compiler complains. The error message might be ugly, but the behavior itself is very clean. (A small fraction of modern C++ abstractions are dedicated to fixing these unreadable error messages!)

At this point, we can understand why the STL is called a "generic library" rather than a "container library." Containers are just carriers; the core lies in the algorithms. The algorithms are generic because they are designed to rely only on a minimal set of operations. This idea isn't unique to C++. Stepanov verified it in Tecton, then again in Scheme and Ada, and finally found that C++'s template system could express this idea most directly, leading to the STL we see today. When learning the STL, we can spend time on how to use `vector`, `map`, or `unordered_map`, but we shouldn't stop there. It is far more worthwhile to understand the layer of algorithms. Containers can be swapped—or even replaced with custom data structures—but the design philosophy of the algorithms is the soul of the entire STL.

---

# From Explicit to Implicit Instantiation: The Story of How the STL Almost Didn't Make It into C++

Reading this history really struck a chord. We write templates and enjoy the convenience of implicit instantiation every day, but few people have considered this: if Bjarne hadn't stuck to his intuition back then, the C++ we write today might look completely different.

## First, Let's Clarify What "Explicit Instantiation" Actually Looks Like

Before telling this story, it is necessary to clarify what the "explicit instantiation" Stepanov saw in Ada actually meant—many people have a fuzzy understanding of this concept.

Explicit instantiation means that before using a generic function, you must tell the compiler in advance: "I need an `int` version, I need a `double` version." The compiler won't deduce it for you; if you don't say it, it won't generate code. In contrast, the templates we write in C++ today? We write a function with `template<typename T>`, pass an `int` when calling, and the compiler automatically replaces `T` with `int` and generates the corresponding code. This is implicit instantiation.

To intuitively feel the difference, let's look at a comparison. First is a simulated "explicit instantiation" style—of course, this isn't real Ada syntax, but it uses C++ concepts to express the idea:

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

Next, let's look at implicit instantiation, which is the standard approach in C++:

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

You see, in the second approach, there is absolutely no need to declare in advance "I need an `int` version, a `double` version, or a `long` version." At each call site, the compiler deduces what `T` is on its own and generates the corresponding function body on the spot. This is the power of implicit instantiation.

## Why Stepanov Initially Thought Explicit Was Better

At first glance, explicit instantiation seems more cumbersome. Why would a genius algorithm designer think this was better?

It makes sense from Stepanov's perspective. He came from the more "mathematical" environments of Ada and Scheme. In mathematics, when you define a function, you are very clear about the set on which it operates. `accumulate` acting on a sequence of integers is an integer version; acting on a sequence of real numbers, it is a real number version. These are two different things and should be stated explicitly. Furthermore, from an engineering standpoint, explicit instantiation gives you complete control over "exactly what code is generated," avoiding issues like template instantiation explosions.

This idea isn't silly at all. In fact, even today, C++ retains the syntax for explicit instantiation (the `template int func<int>(...)` style mentioned above). In large projects where compile time is sensitive, centralizing template instantiations in a single `.cpp` file is a common optimization technique. So, Stepanov's intuition had its merits.

## Why Bjarne Insisted on Implicit

But Bjarne saw something Stepanov didn't.

The key lies in the core design philosophy of the STL: algorithms should not be bound to specific types, but to the "concepts satisfied by iterators." `accumulate` doesn't care if you are summing `int`, `double`, or some custom `BigNum`; it only cares that the iterator can be dereferenced and the value type supports `+` and `=`.

With explicit instantiation, every time you want to support a new type, you have to go back and add an explicit instantiation declaration. This means the algorithm author must know all possible types in advance—**which completely violates the original intent of generic programming!** The significance of generic programming is "write once, use everywhere, regardless of your type, as long as you meet my requirements." Generic programming is *a posteriori* regarding the program's implementation; the compiler instantiates whatever code it deems necessary. Explicit declaration takes a step backward here!

Implicit instantiation makes this a reality: algorithm authors write templates, type authors write types, and the two sides are completely decoupled, with the compiler acting as the bridge. Without this mechanism, the STL's three-layer decoupled architecture of "algorithms + iterators + types" could never have been built.

## Looking Back, It Wasn't That Hard

Looking back today at the debate of "explicit vs. implicit instantiation," the answer seems obvious. But this was the late 80s and early 90s. C++ templates were still rough. No one had written a large-scale template library like STL before. No one knew if implicit instantiation would scale. Bjarne made this judgment without any precedent, and he was right. When learning C++, it's easy to feel that "these designs are taken for granted," but in reality, behind every line of standard library code, there may be a story of "we almost took a different path." Understanding this history is far more interesting than simply memorizing syntax, and it helps us better understand "why C++ is the way it is."

<ReferenceCard title="References">
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
