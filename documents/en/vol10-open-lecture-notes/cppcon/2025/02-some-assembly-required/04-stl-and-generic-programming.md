---
chapter: 2
conference: cppcon
conference_year: 2025
cpp_standard:
- 17
- 20
description: 'CppCon 2025 talk notes — C++: Some Assembly Required by Matt Godbolt'
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
  translated_at: '2026-09-26T15:35:13+00:00'
  engine: anthropic
  token_count: 3200
---
# Rethinking What "Generic" Actually Means from the Origins of the STL

Looking back on how we learned C++, we've noticed that plenty of tutorials on the market leave their understanding of the STL at the level of the "containers + algorithms + iterators" trio, treating it as a toolbox: `#include` whichever container you need, call `std::sort` when you need sorting. It is genuinely convenient to use, and it does live up to the words "standard library" (everyone uses it directly in day-to-day work—I'd wager that unless something breaks, nobody recites the underlying template implementation to themselves while writing code!). But very few people ask why it was designed this way. Following Stepanov's<RefLink :id="1" preview="Matt Godbolt, C++: Some Assembly Required, CppCon 2025" /> history and digging deeper, we discover one thing—the STL never existed to "provide containers" in the first place. Its ultimate goal was to write a **sorting algorithm that works once and for all**.

That claim sounds odd at first—what could possibly be "once and for all" about a sorting algorithm? When we learn data structures, quicksort, merge sort, and heap sort are all written against arrays, right? But once you have written a quicksort that only sorts `int[]`, what about `double[]`? What about an array of `std::string`? What about an array of custom structs? The common practice is to copy-paste, replace `int` with `T`, and wrap it in a `template`. But back in the early 1980s, Stepanov was pondering a far more radical question: could you write a sort that **has absolutely no idea what it is sorting**, and yet simply works?

Today this idea looks like nothing more than templates—nothing exotic. Put it back into the context of the time, though, and it reads differently. Facing the same problem of "generic algorithms," Knuth's approach in *The Art of Computer Programming*<RefLink :id="2" preview="Donald Knuth, The Art of Computer Programming, 1968" /> was to invent a **hypothetical computer**<RefLink :id="6" preview="Wikipedia, MIX (abstract machine)" /> (called MIX) along with its assembly language MIXAL, and then use that machine language to implement every algorithm precisely and to analyze its running time and memory usage<RefLink :id="7" preview="Knuth, MMIX page, purpose of machine language in TAOCP" />. The core idea of that path is: design a machine model that is abstract enough, run algorithms on that model, and thereby measure the cost of every single operation exactly. Stepanov took the exact opposite path—he didn't need an abstract machine; what he needed to abstract were **the very operations the algorithm depends on**. Sorting doesn't need to know what you are sorting; it only needs to know: things can be compared, and positions can be swapped. As long as those two things can be done, sorting works.

Once this difference clicks, a lot of previously fuzzy concepts become clear. Take why iterators exist at all—an iterator is not some "generic pointer"; it is the **contract Stepanov used to decouple algorithms from data structures**. Algorithms don't touch containers directly; they operate on iterators. Whatever operations the iterator provides are exactly the operations the algorithm depends on—and nothing more. That is how the algorithm genuinely becomes "once and for all."

Even more interesting: when Stepanov first implemented these ideas, he didn't use C++ at all. His first paper, in 1981, used a language called **Tecton**<RefLink :id="3" preview="Kapur, Musser, Stepanov, Tecton language, 1981" />—designed together with Deepak Kapur and David Musser, purely for the purpose of expressing the concepts of generic programming. This detail shows that the idea of "generic programming" predates the language. It's not that C++ gained templates and therefore generic programming came into being; rather, Stepanov had the idea first and then needed a language to express it—first Tecton, then Scheme and Ada, and finally C++. Templates, as a core C++ feature, are genuinely hard to use—SFINAE and concepts error messages give plenty of people headaches—but looked at from another angle, templates are merely the tool Stepanov used to realize his dream of "once-and-for-all algorithms." Once you understand why it was designed this way, it stops feeling so hostile.

Following this thread, we can run an experiment to see what "an algorithm depends only on an operation contract" really means. The code below uses no STL containers at all—it runs `std::sort` on a plain raw array:

```cpp
#include <algorithm>
#include <iostream>

int main() {
    int arr[] = {5, 3, 1, 4, 2};

    // std::sort doesn't care what kind of container you pass it
    // It only cares: whether the iterators are RandomAccessIterators (whether they support arithmetic and dereferencing)
    // whether the elements can be compared with operator<, and can be swapped and moved
    std::sort(std::begin(arr), std::end(arr));

    for (int x : arr) {
        std::cout << x << ' ';
    }
    // Output: 1 2 3 4 5
}
```

This looks unremarkable, but think it through—there is not a single line in the implementation of `std::sort` that knows `arr` is an array. All it sees are two pointers (in this scenario the iterators are pointers), on which it needs to perform `++`, `--`, `+=`, `-=`, `*`, and `<`—which is in fact the complete requirement set of a **RandomAccessIterator**<RefLink :id="5" preview="cppreference, std::sort, RandomAccessIterator requirements" /> (random access + dereference + comparison), plus `swap` and move semantics on the value type; only then can sorting run. This is exactly what Stepanov was after back then.

Then take it one step further and try a custom type:

```cpp
#include <algorithm>
#include <iostream>
#include <string>

struct Person {
    std::string name;
    int age;
};

// The algorithm doesn't care what Person is; it only cares whether it can be compared
// Here we tell the compiler: you can compare two Person objects—and to be more specific,
// the comparison is by age!
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
    // Output:
    // Bob: 25
    // Alice: 30
    // Charlie: 35
}
```

`std::sort` still has no idea what `Person` is. All it knows is that the expression `*it < *it` compiles. Provide `<`, and it can sort; don't provide it, and the compiler errors out—the error message is admittedly ugly, but the behavior itself is beautifully clean. (A small part of the later work on modern C++ abstractions has been devoted precisely to this problem of unreadable error messages!)

At this point it becomes clear why the STL is called a "generic library" rather than a "container library." Containers are just the carrier; the core is the algorithms. And the reason the algorithms can be generic is that they were designed to depend on a minimal set of operations. This idea is not specific to C++: Stepanov validated it in Tecton, then validated it again in Scheme and in Ada, and finally found that C++'s template system happened to express the idea most directly—and that is how we got the STL we see today. When learning the STL, we can spend our energy on how to use `vector`, `map`, and `unordered_map`—but honestly, don't stop there; what deserves more of your time is the algorithm layer. Containers can be swapped out—you can even plug in your own data structures—but the design philosophy of the algorithms is the soul of the entire STL.

---

# From Explicit to Implicit Instantiation: How the STL Almost Didn't Make It into C++

Reading this piece of history resonated deeply with us. We write templates every day and enjoy the convenience of implicit instantiation every day, yet few of us have ever wondered—if Bjarne hadn't held to his intuition back then, the C++ we write today might look completely different.

## First, Let's Get Clear on What "Explicit Instantiation" Actually Looks Like

Before telling this story, we need to pin down what Stepanov meant by "explicit instantiation" in Ada—many people's understanding of this concept has always been fuzzy.

Explicit instantiation means that before using a generic function, you must tell the compiler in advance: "I want an int version, and I want a double version." The compiler won't deduce anything for you automatically; if you don't say it, it doesn't get generated. And the templates we write in C++ today? You write a `template<typename T>` function, pass in an `int` at the call site, and the compiler replaces `T` with `int` and generates the corresponding code all by itself—that is implicit instantiation.

To feel the difference concretely, let's look at a side-by-side. First, a style that mimics "explicit instantiation"—this is not real Ada syntax, of course, but it expresses the idea using C++ concepts:

```cpp
// Mimicking Ada-style explicit instantiation
// You must declare in advance "which typed versions I want"
template<typename T>
T my_accumulate(T* begin, T* end, T init) {
    for (T* p = begin; p != end; ++p) {
        init = init + *p;
    }
    return init;
}

// Explicit instantiation declarations: telling the compiler "I need these two versions"
template int my_accumulate<int>(int*, int*, int);
template double my_accumulate<double>(double*, double*, double);

int main() {
    int arr[] = {1, 2, 3, 4, 5};
    // The compiler sees the call, finds that an int-version instantiation already exists, and uses it directly
    int sum = my_accumulate(arr, arr + 5, 0);

    // double arr2[] = {1.0, 2.0, 3.0};
    // double sum2 = my_accumulate(arr2, arr2 + 3, 0.0);
    // If you uncomment the two lines above without declaring the double version in advance,
    // under a pure explicit-instantiation model this is a hard error
}
```

And then the implicit instantiation we now take for granted—what C++ actually does:

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

    // You can even pass a type that was never declared in advance—
    // the compiler deduces it at the call site and generates the code on the spot
    long arr3[] = {10L, 20L, 30L};
    long sum3 = my_accumulate(arr3, arr3 + 3, 0L);
    std::cout << sum3 << "\n";  // 60
}
```

See—in the second style there is no up-front declaration of "I need the int version, the double version, the long version." At every call site the compiler deduces what `T` is and generates the corresponding function body on the spot. That is the power of implicit instantiation.

## Why Stepanov Initially Thought Explicit Was Better

At first glance, explicit instantiation is plainly more troublesome—why would a genius algorithm designer consider it better?

It becomes clear once you stand in Stepanov's shoes. He came from the more "mathematical" environments of Ada and Scheme. In mathematics, when you define a function, you know exactly which set it operates on. `accumulate` acting on a sequence of integers is the integer version; acting on a sequence of reals is the real version—these are two different things, and they should be stated explicitly. And from an engineering standpoint, explicit instantiation gives you complete control over "exactly which code gets generated," which rules out problems such as template instantiation explosion.

This idea is not at all stupid. In fact, C++ retains the syntax for explicit instantiation to this day (the `template int func<int>(...)` form above), and in large, compile-time-sensitive projects, concentrating template instantiations into a single `.cpp` file is a common optimization technique. So Stepanov's intuition had its reasons.

## Why Bjarne Insisted on Implicit

But Bjarne saw something Stepanov didn't.

The key lies in the STL's core design philosophy: an algorithm should not be bound to specific types; it should be bound to "the concepts the iterators satisfy." `accumulate` doesn't care whether you are accumulating `int`, `double`, or some custom `BigNum`; it only cares that the iterator can be dereferenced and that the value type supports `+` and `=`.

With explicit instantiation, every time you want to support a new type you would have to go back and add another explicit instantiation declaration. That would mean the algorithm's author must know every possible type in advance—**but that is precisely contrary to the whole point of generic programming**! The meaning of generic programming is "I write it once, you take it and use it, whatever your type is, as long as it meets my requirements." Generic programming is a posteriori with respect to the program being implemented: the compiler instantiates whatever kind of code it decides is needed, while explicit declaration would take a step backward right here!

Implicit instantiation made this a reality: the algorithm author writes templates, the type author writes types, the two sides are completely decoupled, and the compiler acts as the bridge in between. Without this mechanism, the STL's three-layer decoupled architecture of "algorithms + iterators + types" could never have been built.

## Looking Back, It Wasn't That Hard a Call

Today, looking back at the "explicit instantiation vs. implicit instantiation" debate, the answer seems obvious. But this was the late 1980s and early 1990s: C++ templates themselves were still crude, nobody had ever written a template library at the scale of the STL, and nobody knew whether implicit instantiation could scale at all. Bjarne made this judgment with no precedent to draw on—and he was right. When learning C++, it is easy to assume "these designs are self-evident," but in fact behind every line of the standard library there may be a story of how things "almost went down another path." Figuring out these threads of history is far more interesting than merely memorizing syntax, and it helps us far more in understanding "why C++ is the way it is."

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
