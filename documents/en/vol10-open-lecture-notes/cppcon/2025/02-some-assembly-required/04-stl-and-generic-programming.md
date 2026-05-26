---
title: The Essence of the STL and Generic Programming
description: 'CppCon 2025 Talk Notes — C++: Some Assembly Required by Matt Godbolt'
conference: cppcon
conference_year: 2025
talk_title: 'C++: Some Assembly Required'
speaker: Matt Godbolt
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW?p=2
video_youtube: https://www.youtube.com/watch?v=zoYT7R94S3c
tags:
- cpp-modern
- host
- intermediate
difficulty: intermediate
platform: host
cpp_standard:
- 17
- 20
chapter: 2
order: 4
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/02-some-assembly-required/04-stl-and-generic-programming.md
  source_hash: a0f6377dc58c22b431f842d4b65bd4dfec2d19587321e9e75c7fccb4d0918996
  translated_at: '2026-05-26T11:11:30.028767+00:00'
  engine: anthropic
  token_count: 2315
---
# Rethinking "Generic" Through the Origins of the STL

Looking back on my own C++ learning journey, I've noticed that many C++ tutorials on the market treat the STL simply as the "containers + algorithms + iterators" trio, using it as a toolbox: grab whatever container you need `#include`, call `std::sort` when you need to sort. It's certainly convenient, and it truly lives up to the name "standard library" (everyone just uses it directly—I suspect unless something breaks, nobody is silently reciting the underlying template implementation while writing code!). But few people stop to ask *why* it was designed this way. Digging into the history alongside Stepanov<RefLink :id="1" preview="Matt Godbolt, C++: Some Assembly Required, CppCon 2025" />, we discover something: the STL was never created to "provide containers." Its ultimate goal was to write a **once-and-for-all sorting algorithm**.

This might sound strange at first—what's so "once-and-for-all" about a sorting algorithm? When we study data structures, aren't quicksort, merge sort, and heap sort all written for arrays? But if you write a quicksort that only sorts `int[]`, what about `double[]`? What about `std::string` arrays? What about arrays of custom structs? The common approach is copy-paste: change `int` to `T`, wrap it in a `template`, and call it a day. But in the early 1980s, Stepanov was pondering a far more radical question: could we write a sort that **has absolutely no idea what it is sorting**, yet works anyway?

Today, this idea just sounds like templates—nothing special. But in the context of that era, it was entirely different. Faced with the same challenge of "generic algorithms," Knuth's approach in *The Art of Computer Programming*<RefLink :id="2" preview="Donald Knuth, The Art of Computer Programming, 1968" /> was to invent a **hypothetical computer**<RefLink :id="6" preview="Wikipedia, MIX (abstract machine)" /> (called MIX) along with its assembly language, MIXAL. He then used this machine language to precisely implement and analyze the running time and memory footprint of all algorithms<RefLink :id="7" preview="Knuth, MMIX page, purpose of machine language in TAOCP" />. The core idea behind this path was to design a sufficiently abstract machine model, run algorithms on it, and thereby accurately measure the cost of every single operation. Stepanov went in the exact opposite direction—he didn't need an abstract machine; what he needed to abstract were **the operations themselves that the algorithm relies on**. Sorting doesn't need to know *what* it's sorting; it only needs to know that items can be compared and swapped. As long as those two things are possible, the sort works.

Once we grasp this distinction, many previously fuzzy concepts become clear. For example, why do iterators even exist? Iterators are not "generic pointers" at all—they are the **contract Stepanov used to decouple algorithms from data structures**. Algorithms don't operate on containers directly; they operate on iterators. The algorithm only depends on whatever operations the iterator provides. This is how algorithms truly achieve the "once-and-for-all" ideal.

What's even more interesting is that when Stepanov first implemented these ideas, he didn't even use C++. In his first paper in 1981, he used a language called **Tecton**<RefLink :id="3" preview="Kapur, Musser, Stepanov, Tecton language, 1981" />—designed in collaboration with Deepak Kapur and David Musser, purely to express the concepts of generic programming. This detail proves that the idea of "generic programming" preceded the language. It wasn't that C++ had templates and therefore had generic programming; rather, Stepanov had the idea first, and then he needed a language to express it—first Tecton, then Scheme, then Ada, and finally C++. Templates, as a core C++ feature, are admittedly difficult to use—SFINAE and concepts error messages give many people headaches—but looking at it from another angle, templates are merely the tool Stepanov used to realize his dream of "once-and-for-all algorithms." If we understand *why* they were designed this way, we become much less resistant to them.

Following this line of thought, we can run an experiment to verify what "algorithms depending only on operation contracts" actually means. The code below doesn't use any STL containers; it purely uses raw arrays to run `std::sort`:

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

This looks unremarkable, but think about it carefully—not a single line inside the implementation of `std::sort` knows that `arr` is an array. All it sees are two pointers (in this scenario, the iterators *are* pointers). It needs to perform `++`, `--`, `+=`, `-=`, `*`, and `<` on these pointers—this is actually the complete requirement set for a **RandomAccessIterator**<RefLink :id="5" preview="cppreference, std::sort, RandomAccessIterator requirements" /> (random access + dereference + comparison), plus the `swap` and move semantics of the value type, for the sort to work. This is exactly what Stepanov wanted back then.

Taking it a step further, let's try a custom type:

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

`std::sort` still has no idea what `Person` is. It only knows that the expression `*it < *it` compiles. If you provide `<`, it can sort; if you don't, the compiler throws an error—the error message is admittedly ugly, but the behavior itself is incredibly clean. (A small part of the work in subsequent modern C++ abstractions has been trying to fix these unreadable error messages!)

At this point, we can understand why the STL is called a "generic library" rather than a "container library." Containers are merely the vehicles; the core is the algorithms. And the reason algorithms can be generic is that they are designed to depend only on a minimal set of operations. This idea isn't unique to C++; Stepanov validated it in Tecton, then again in Scheme and Ada, and finally found that C++'s template system could express this idea most directly, leading to the STL we see today. When learning the STL, we can spend our energy on how to use `vector`, `map`, and `unordered_map`, but we really shouldn't just stop there. It's even more worthwhile to spend time understanding the algorithm layer. Containers can be swapped out—we can even use our own data structures—but the design philosophy of the algorithms is the true soul of the entire STL.

---

# From Explicit to Implicit Instantiation: The Story of How the STL Almost Didn't Make It Into C++

Reading about this part of the history really struck a chord. We write templates every day and enjoy the convenience of implicit instantiation, but few people stop to think—if Bjarne hadn't trusted his instincts back then, the C++ we write today might look completely different.

## First, Let's Clarify What "Explicit Instantiation" Actually Looks Like

Before telling this story, we need to clear up what "explicit instantiation" meant in the Ada that Stepanov was using—many people have always had a fuzzy understanding of this concept.

Explicit instantiation means that before you can use a generic function, you must tell the compiler in advance: "I need an int version, I need a double version." The compiler won't deduce it for you; if you don't say it, it won't generate the code. And the templates we write in C++ today? We write a function with `template<typename T>`, pass in a `int` when calling it, and the compiler automatically replaces `T` with `int` and generates the corresponding code—that is implicit instantiation.

To intuitively feel the difference, let's look at a comparison. First, a style simulating "explicit instantiation"—of course, this isn't real Ada syntax, but it uses C++ concepts to express the idea:

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

Then there's the implicit instantiation we're all used to, which is C++'s actual approach:

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

See? In the second approach, there's no advance declaration saying "I need an int version, a double version, a long version." The compiler deduces what `T` is at each call site and generates the corresponding function body on the spot. That is the power of implicit instantiation.

## Why Stepanov Initially Thought Explicit Was Better

At first glance, explicit instantiation is clearly more cumbersome—why would a genius algorithm designer think this was better?

Looking at it from Stepanov's perspective makes it clear. He was coming from the more "mathematical" environments of Ada and Scheme. In mathematics, when you define a function, you know exactly which set it operates on. `accumulate` acting on a sequence of integers is the integer version; acting on a sequence of reals is the real version—these are two different things and should be stated explicitly. Furthermore, from an engineering standpoint, explicit instantiation gives you complete control over "exactly which code gets generated," preventing issues like template instantiation explosion.

This idea isn't stupid at all. In fact, even today, C++ retains the syntax for explicit instantiation (the `template int func<int>(...)` syntax shown above). In large projects sensitive to compile times, centralizing template instantiations in a single `.cpp` file is a common optimization technique. So Stepanov's intuition made sense.

## Why Bjarne Insisted on Implicit

But Bjarne saw something Stepanov didn't.

The key lies in the STL's core design philosophy: algorithms should not be bound to specific types; they should be bound to the "concepts satisfied by iterators." `accumulate` doesn't care whether you're accumulating `int`, `double`, or some custom `BigNum`. It only cares that the iterator can be dereferenced, and that the value type supports `+` and `=`.

With explicit instantiation, every time you want to support a new type, you have to go back and add an explicit instantiation declaration. This means the algorithm author must know all possible types in advance—**but this completely violates the original intent of generic programming!** The whole point of generic programming is "I write it once, you take it and use it, regardless of your type, as long as you meet my requirements." Generic programming is *a posteriori* to the program's implementation—the compiler instantiates whatever code it deems necessary. Explicit declaration takes a step backward here!

Implicit instantiation made this a reality: algorithm authors write templates, type authors write types, the two sides are completely decoupled, and the compiler acts as the bridge in between. Without this mechanism, the STL's three-layer decoupled architecture of "algorithm + iterator + type" simply could not have been built.

## In Retrospect, It Doesn't Seem That Hard

Looking back today at the "explicit instantiation vs. implicit instantiation" debate, the answer seems obvious. But this was the late 1980s and early 1990s—C++ templates themselves were still rough, nobody had written a template library on the scale of the STL, and nobody knew whether implicit instantiation could actually scale. Bjarne made this judgment without any precedent, and he was right. When learning C++, it's easy to feel that "these designs are a matter of course," but the truth is that behind every line of standard library code, there might be a story of "it almost went down a completely different path." Understanding this backstory is far more interesting than simply memorizing syntax, and it helps us much more in understanding "why C++ is the way it is."

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
