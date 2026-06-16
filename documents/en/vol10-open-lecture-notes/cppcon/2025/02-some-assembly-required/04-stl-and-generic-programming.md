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
title: The Essence of the STL and Generic Programming
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW?p=2
video_youtube: https://www.youtube.com/watch?v=zoYT7R94S3c
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/02-some-assembly-required/04-stl-and-generic-programming.md
  source_hash: 549b99fcc9d7cda24a897cb46f56a1021798c0255c4ccdd4a1a3b06bcee59266
  translated_at: '2026-06-16T03:51:00.258430+00:00'
  engine: anthropic
  token_count: 2313
---
# Rethinking "Generic" from the Origins of the STL

Reflecting on my journey of learning C++, I noticed that many tutorials on the market only understand the STL at the level of the "containers + algorithms + iterators" trio, treating it merely as a toolbox: you use whatever container you need, call `std::sort` when you need to sort, and it is indeed convenient. It certainly lives up to the name "Standard Library" (everyone uses it directly; I guess unless something breaks, no one silently recites the underlying template implementation while coding!). However, few people think about why it was designed this way. Digging into the history alongside Stepanov<RefLink :id="1" preview="Matt Godbolt, C++: Some Assembly Required, CppCon 2025" />, we discover a fact—the STL was never created just to "provide containers." Its ultimate goal was to write a **sorting algorithm that works once and for all**.

This statement sounds a bit strange at first. What's so "once and for all" about a sorting algorithm? When learning data structures, quicksort, mergesort, and heapsort are all written for arrays, aren't they? But if you write a quicksort that can only sort `int` arrays, what about `double`? What about `string` arrays? What about arrays of custom structs? The common approach is copy-paste: change `int` to `double`, and wrap it in a template. But in the early 1980s, Stepanov was thinking about a more extreme question: can we write a sort that **completely doesn't know what it is sorting**, but it just works?

This idea seems like just templates today, nothing special. But in the context of that time, it was different. Facing the same problem of "generic algorithms," Knuth's approach in *The Art of Computer Programming*<RefLink :id="2" preview="Donald Knuth, The Art of Computer Programming, 1968" /> was to invent a **hypothetical computer**<RefLink :id="6" preview="Wikipedia, MIX (abstract machine)" /> (called MIX) and its assembly language, MIXAL, to precisely implement and analyze the running time and memory usage of all algorithms<RefLink :id="7" preview="Knuth, MMIX page, purpose of machine language in TAOCP" />. The core idea of this path is: design an abstract machine model that is sufficient, run algorithms on this model, and thus accurately measure the cost of every operation. Stepanov took the completely opposite path—he didn't need an abstract machine; he needed to abstract **the operations themselves that the algorithm relies on**. Sorting doesn't need to know what it is sorting; it only needs to know: it can compare size and it can swap positions. As long as these two things can be done, sorting works.

Understanding this difference clarifies many previously vague concepts. For example, why do iterators exist—iterators are not "generic pointers" at all; they are the **contract Stepanov used to decouple algorithms from data structures**. Algorithms don't manipulate containers directly; they manipulate iterators. Iterators provide certain operations, and algorithms rely only on those operations. This way, algorithms truly achieve "write once, use forever."

More interestingly, when Stepanov first implemented these ideas, he didn't even use C++. In his first paper in 1981, he used a language called **Tecton**<RefLink :id="3" preview="Kapur, Musser, Stepanov, Tecton language, 1981" />—this was designed in collaboration with Deepak Kapur and David Musser, purely for the purpose of expressing generic programming concepts. This detail shows that the idea of "generic programming" existed before the language. It's not that C++ had templates so there was generic programming; rather, Stepanov had this idea first, then he needed a language to express it—first Tecton, then Scheme, then Ada, and finally C++. Templates, as a core feature of C++, are indeed difficult to use—SFINAE and concepts errors give many people a headache—but looking at it from another angle, templates are just the tool Stepanov used to realize his dream of "once-and-for-all algorithms." Understanding why it was designed this way makes it less repulsive.

Following this line of thought, we can do an experiment to verify what "algorithms rely only on operation contracts" actually means. The code below doesn't use any STL containers, purely using raw arrays to run `std::sort`:

```cpp
int arr[] = {5, 2, 9, 1, 5, 6};
std::sort(std::begin(arr), std::end(arr));
```

This looks plain, but think carefully—there isn't a single line of code in `std::sort`'s implementation that knows `arr` is an array. It only sees two pointers (in this scenario, iterators are pointers), and it needs to perform operations on these pointers like `*it`, `it++`, `it + n`, `it1 - it2`, `it1 < it2`, `it1 != it2`—this is actually the complete requirement set for **RandomAccessIterator**<RefLink :id="5" preview="cppreference, std::sort, RandomAccessIterator requirements" /> (random access + dereference + comparison), plus the value type's copyability and move semantics, for sorting to run. This is exactly what Stepanov wanted back then.

Then, going a step further, let's try a custom type:

```cpp
struct Point { int x, y; };
bool operator<(const Point& lhs, const Point& rhs) {
    return lhs.x < rhs.x; // Simple comparison
}

Point pts[] = {{3, 4}, {1, 2}, {5, 6}};
std::sort(std::begin(pts), std::end(pts));
```

`std::sort` still doesn't know what `Point` is. It only knows that the expression `*it < *it` can compile. You provide `operator<`, and it sorts; you don't provide it, and the compiler errors—though the error message is indeed ugly, the behavior itself is very clean. (A small part of the work of subsequent modern C++ abstractions is trying to solve the problem of unreadable error messages!)

At this point, we can understand why the STL is called a "generic library" rather than a "container library." Containers are just carriers; the core is those algorithms. And the reason algorithms can be generic is that they are designed to rely only on a minimized set of operations. This idea is not unique to C++; Stepanov verified it in Tecton, then again in Scheme and Ada, and finally found that C++'s template system could express this idea most directly, leading to the STL we see today. When we learn the STL, we can spend energy on how to use `vector`, `map`, `unordered_map`, but really don't just stop there; it is more worth spending time understanding the algorithm layer. Containers can be changed—or even use your own data structures—but the design philosophy of the algorithms is the soul of the entire STL.

---

# From Explicit to Implicit Instantiation: The Story of How the STL Almost Didn't Make It into C++

I was particularly touched when I saw this part of the history. We write templates every day and enjoy the convenience brought by implicit instantiation, but few people have thought about it—if Bjarne hadn't insisted on his intuition back then, the C++ we write today might look completely different.

## First, let's clarify what "Explicit Instantiation" actually looks like

Before telling this story, it is necessary to understand what "explicit instantiation" meant in Stepanov's Ada—many people's understanding of this concept has been vague.

Explicit instantiation means that before using a generic function, you must tell the compiler in advance: "I want an `int` version, I want a `double` version." The compiler won't deduce it for you; if you don't say it, it won't generate it. And the templates we write in C++ now? Write a `template <typename T>` function, pass a `double` in when calling, and the compiler automatically replaces `T` with `double` and generates the corresponding code. This is implicit instantiation.

To intuitively feel this difference, let's look at a comparison. First is the "explicit instantiation" style of writing—of course, this isn't real Ada syntax, but using C++ concepts to express this meaning:

```cpp
// Explicit instantiation style (simulated)
template <typename T>
void my_sort(T* begin, T* end);

// Must explicitly tell the compiler which versions are needed
template void my_sort<int>(int*, int*);
template void my_sort<double>(double*, double*);

// Usage
int arr[10];
my_sort<int>(arr, arr + 10); // Must specify <int>
```

Then there is the implicit instantiation we are accustomed to now, which is the actual C++ approach:

```cpp
// Implicit instantiation style (actual C++)
template <typename T>
void my_sort(T* begin, T* end);

// Usage
int arr[10];
std::sort(arr, arr + 10); // Compiler deduces T is int
```

You see, in the second way of writing, there is no advance declaration of "I need an int version, a double version, a long version" at all. The compiler deduces what `T` is at every call point and generates the corresponding function body on the spot. This is the power of implicit instantiation.

## Why Stepanov thought Explicit was better initially

At first glance, explicit instantiation is clearly more troublesome. Why would a genius algorithm designer think this was better?

Standing in Stepanov's shoes, it becomes clear. He came from the more "mathematical" environments of Ada and Scheme. In mathematics, when defining a function, you are very clear about the set on which it operates. `sort` acting on a sequence of integers is the integer version; acting on a sequence of real numbers is the real number version. These are two different things and should be stated clearly. Moreover, from an engineering perspective, explicit instantiation gives you complete control over "exactly which code is generated," avoiding problems like template instantiation explosions.

This idea isn't stupid at all. In fact, even today, C++ retains the syntax for explicit instantiation (the `template void my_sort<int>(...);` style above). In large projects sensitive to compilation time, concentrating template instantiation in a single `.cpp` file is a common optimization technique. So Stepanov's intuition had its merits.

## Why Bjarne insisted on Implicit

But Bjarne saw what Stepanov didn't.

The key lies in the core design philosophy of the STL: algorithms should not be bound to specific types, but to the "concepts satisfied by iterators." `std::accumulate` doesn't care if you are summing `int`, `double`, or some custom `BigInt`. It only cares that the iterator can be dereferenced and the value type can do `operator+` and copy construction.

With explicit instantiation, every time you want to support a new type, you have to go back and add an explicit instantiation declaration. This means the algorithm author must know all possible types in advance—**but this violates the original intention of generic programming!** The significance of generic programming lies in "I write it once, you take it and use it, regardless of what your type is, as long as it meets my requirements." Generic programming is a posteriori to the implementation of the program itself; the compiler determines what is needed and instantiates whatever code is necessary; explicit declaration takes a step back here!

Implicit instantiation makes this a reality: algorithm authors write templates, type authors write types, the two sides are completely decoupled, and the compiler acts as the bridge in between. Without this mechanism, the STL's three-layer decoupled architecture of "algorithm + iterator + type" couldn't be built at all.

## Looking back, it wasn't actually that hard

Looking back today at the debate of "explicit vs. implicit instantiation," the answer seems obvious. But that was in the late 80s and early 90s; C++ templates themselves were still rough, no one had written a template library on the scale of the STL, and no one knew if implicit instantiation could scale. Bjarne made this judgment without any precedent, and he was right. When learning C++, it's easy to feel that "these designs are taken for granted," but in fact, behind every line of standard library code, there may be stories like "almost took a different path." Understanding these ins and outs is much more interesting than simply memorizing syntax, and it helps us better understand "why C++ is the way it is."

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
