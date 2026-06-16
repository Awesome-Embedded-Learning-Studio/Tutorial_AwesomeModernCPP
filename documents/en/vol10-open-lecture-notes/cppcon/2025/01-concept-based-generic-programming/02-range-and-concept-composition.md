---
chapter: 1
conference: cppcon
conference_year: 2025
cpp_standard:
- 20
- 23
description: CppCon 2025 Talk Notes — From iterator pair problems to Range abstractions,
  then to Concept composition and requires expressions
difficulty: intermediate
order: 2
platform: host
reading_time_minutes: 37
speaker: Bjarne Stroustrup
tags:
- cpp-modern
- host
- intermediate
talk_title: Concept-based Generic Programming
title: Range, Iterators, and Concepts
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW
video_youtube: https://www.youtube.com/watch?v=VMGB75hsDQo
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/01-concept-based-generic-programming/02-range-and-concept-composition.md
  source_hash: 81d63705108f30756d72ebf9d55f4497b516113acdd8ea33ebeb4e89e954b674
  translated_at: '2026-06-16T06:00:20.188319+00:00'
  engine: anthropic
  token_count: 6359
---
# Unchecked Pointers and the Boundaries of Generic Programming

When writing C++, I often encountered a typical problem: receiving a pointer and wanting to create a sub-view consisting of the first 10 elements, but the resulting code always felt awkward. For instance, if you have a `double*`, you intend to say "I want the first 10 elements pointed to by this pointer." However, looking strictly at that code, neither you nor the compiler can know how many elements the pointer actually points to, or whether 10 is out of bounds. This is completely unchecked. I used to think there was no solution; pointers are just like that, right? But later, I realized that if your code review doesn't flag this pattern as a potential issue, the review process itself isn't strict enough.

Of course, in reality, we often receive raw pointers from external systems, C interfaces, or legacy code. We can't simply say "I don't do pointers," so we must support this capability. The key question is: can you wrap that pointer into something that carries boundary information and type safety checks as soon as you receive it? This is what I hadn't figured out previously—I assumed "using pointers" and "type safety" were contradictory, but they aren't; they belong to different stages.

## Solving an Annoying Little Problem First

Before diving into deeper topics, I want to address a problem that frustrated me enough to wear out my keyboard. Previously, when working with type-safe numbers, I had to write things like `number_of<double>`, explicitly specifying `double` every time. It was too tedious. I'm not a fast typist, and honestly, the people who designed C and Unix probably weren't either—which is why names like `int`, `double`, and `ptr` are ridiculously short. But we have type deduction now, so why should we still type it out?

My approach is: if `number` has an initializer, we directly deduce the base type of `number` from that initializer. For example, writing `number_of{1}` deduces `number_of<int>`; writing `number_of{3u}` deduces `number_of<unsigned>`; writing `number_of{1.0}` deduces `number_of<double>`. You only need to write `number_of<double>{1}` explicitly when it's strictly necessary—for example, when initializing with an integer but intending to have `double` precision. This way, we rarely need to type extra characters in daily use, without sacrificing any type safety.

```cpp
#include <iostream>
#include <type_traits>

// number 的基础定义：携带一个值，类型由模板参数决定
template<typename T>
struct number {
    T value;
    // 禁止隐式转换到 T，防止你把它当普通数值用
    explicit operator T() const { return value; }
};

// CTAD（类模板参数推导）指引：从初始化器推导类型
template<typename T>
number(T) -> number<T>;

// 当你需要显式指定类型时，用这个别名简化书写
template<typename T>
using number_of = number<T>;

int main() {
    // 自动推导：number_of<int>
    number_of a{42};
    static_assert(std::is_same_v<decltype(a), number<int>>);

    // 自动推导：number_of<unsigned>
    number_of b{3u};
    static_assert(std::is_same_v<decltype(b), number<unsigned int>>);

    // 自动推导：number_of<double>
    number_of c{2.718};
    static_assert(std::is_same_v<decltype(c), number<double>>);

    // 需要显式指定的情况：用整数初始化，但想要 double
    number_of d = number_of<double>{1};
    static_assert(std::is_same_v<decltype(d), number<double>>);

    std::cout << a.value << "\n";  // 42
    std::cout << b.value << "\n";  // 3
    std::cout << c.value << "\n";  // 2.718
    std::cout << d.value << "\n";  // 1

    // 下面这行编译不过，因为 explicit 阻止了隐式转换
    // int x = a;
    // 但这样是可以的：
    int x = static_cast<int>(a);
    std::cout << x << "\n";  // 42
}
```

Look, it compiles and runs, and all `static_assert` checks pass. I used to think Class Template Argument Deduction (CTAD) was just syntactic sugar, but in this scenario, it truly makes writing type-safe code as smooth as writing ordinary code.

## Does this count as generic programming?

At this point, you might ask: Does this count as generic programming? Isn't it just writing a template class with some CTAD?

I hesitated too, but now I believe it is. It uses generic programming techniques to solve a fundamental problem caused by C++'s history: implicit conversions between numeric types lead to subtle, hard-to-detect bugs. You could design a new language without this baggage, but we don't have that option. We have to work within C++ and use a small library to eliminate these issues. Plus, if you look closely, the core logic for the type-safe `number` is only about 37 lines; the bounds-checking `span` is under 100 lines. That's shorter than the specification documents describing the language's behavior. Solving a systemic problem with minimal code—isn't that exactly what generic programming should do? (Broadly speaking, describing what a system should do without worrying about the vast majority of common details is the essence of generic programming.)

## The classic problem that really gave me a headache: `std::sort` error messages

Alright, warm-up over. Let's talk about a problem I struggled with for a long time and finally started to understand.

You've definitely used `std::sort`. Its signature looks something like this: it takes two random access iterators, `first` and `last`, plus an optional comparison function. The C++ standard documentation states clearly: these iterators must satisfy the `LegacyRandomAccessIterator` requirements, the iterator's value type must be `MoveAssignable` and `MoveConstructible`, and the comparison function must satisfy `StrictWeakOrdering`...

But here is the problem: these requirements are never directly checked.

They exist only in the documentation and in the minds of the committee members. When the compiler instantiates `std::sort`, it doesn't first verify if your iterator is a random access iterator. It just blindly instantiates. Then, deep in the template expansion process, if your type doesn't meet the requirements, it throws a multi-hundred-line error in a completely unrelated place. You might pass in a `std::list` iterator, and the error tells you some `__move_assign` failed or some `__gap` variable is problematic. When you see that error message, you are just completely lost.

### Reproducing the error that made me lose my mind

First, a quick note on the environment: I'm using GCC 16.1.1 with `-std=c++20` on Arch Linux WSL. The compilation command is the standard `g++ -std=c++20 -Wall -Wextra`.

Let's write some code that looks perfectly fine:

```cpp
#include <list>
#include <algorithm>
#include <iostream>

int main() {
    std::list<int> lst = {5, 3, 1, 4, 2};
    std::sort(lst.begin(), lst.end());
    for (int x : lst) {
        std::cout << x << " ";
    }
    std::cout << "\n";
    return 0;
}
```

Guess what? The build blew up immediately. Here is a relatively "readable" snippet from the error log:

```text
/usr/include/c++/16/bits/stl_algo.h: In instantiation of 'void std::sort(_RandomAccessIterator, _RandomAccessIterator, _Compare) [with _RandomAccessIterator = std::_List_iterator<int>; _Compare = __gnu_cxx::__ops::_Iter_less_iter]':
...
error: no match for 'operator-' (operand types are: 'std::_List_iterator<int>' and 'std::_List_iterator<int>')
```

When I see this error, I know the iterator type is wrong because I know that `list` is a doubly linked list that does not support random access. But what if you are a beginner with less than six months of experience? You will see `no match for 'operator-'` and start wondering: Did I forget to overload some operator? Did I miss an include file? This error message tells you absolutely nothing about the real problem—**you used an iterator that does not support random access with an algorithm that requires it**.

I used to think "ugly template errors" were an overrated complaint; I figured you just get used to them after seeing them a few times. But this time, I thought about it seriously, and that's not it. The problem isn't that the error is "long," but that the error message describes the **symptom** (cannot find `operator-`), not the **cause** (iterator category does not satisfy requirements). The gap between these two is a massive chasm for those unfamiliar with template metaprogramming.

## What about now?

Now we have concepts.

Concepts were introduced in C++20, but their intellectual roots can be traced back to Alex Stepanov (the father of the STL) and his original vision for generic programming<RefLink :id="4" preview="Stepanov & Lee, The Standard Template Library, 1995" />. He believed from the very beginning that generic algorithms should have explicit, checkable requirements for their arguments. This isn't an optional cherry on top; it is the infrastructure of generic programming. It just took C++ more than thirty years to build this infrastructure.

Looking back now, it feels like a room was missing a wall. Everyone got used to the draft, even learned how to live in the wind, until one day someone finally built the wall, and you realize: it can actually be this comfortable.

Next, I want to write some code to see how concepts actually change the way we write generic code. Not the textbook `template<std::integral T>` examples, but usages that solve real problems. Let's start with the simplest scenario: writing a constraint for our own `sort`, then intentionally passing the wrong type to see just how good the error messages can get.

```cpp
#include <iostream>
#include <vector>
#include <list>
#include <concepts>
#include <algorithm>
#include <iterator>

// 先定义我们自己的 concept：随机访问迭代器范围
// 注意：这里用标准库的 concept 来组合，不需要从零写
template<typename Iter>
concept RandomAccessRange =
    std::random_access_iterator<Iter> &&
    std::sentinel_for<Iter, Iter>;

// 一个受约束的 sort 包装
template<RandomAccessRange Iter, typename Comp = std::less<>>
    requires std::indirect_strict_weak_order<Comp, Iter>
void safe_sort(Iter first, Iter last, Comp comp = {}) {
    std::sort(first, last, comp);
}

int main() {
    // 正确用法：vector 的迭代器是随机访问迭代器
    std::vector<int> v = {5, 3, 1, 4, 2};
    safe_sort(v.begin(), v.end());
    for (int x : v) std::cout << x << " ";
    std::cout << "\n";
    // 输出：1 2 3 4 5

    // 错误用法：list 的迭代器不是随机访问迭代器
    // 取消下面注释会看到非常清晰的错误信息
    // std::list<int> lst = {5, 3, 1, 4, 2};
    // safe_sort(lst.begin(), lst.end());
}
```

Try removing the last two lines of comments. On my machine (GCC 16.1.1, `-std=c++20`), the error message tells you exactly what's wrong: constraints not satisfied, `std::list<int>::iterator` does not satisfy `random_access_iterator`. No 400 lines of template instantiation dumps, no `__gap`, no `__move_assign`, just one sentence: your iterator type is wrong.

Seeing this error message felt incredibly satisfying. I've been tortured by `std::sort` error messages so many times in the past, and it turns out the solution is simple—we don't need extra tools or pretty-print scripts. We just need to write the constraints in the function signature. The compiler has always been capable of checking this; it just lacked the syntax for you to express the constraint.

### Intercepting Errors at the Door with Concepts

In the C++20 Standard Library, concepts that previously existed only as textual descriptions in the standard documents have become real code entities. This includes `std::random_access_iterator` and `std::sortable`.

I used to think concepts were just syntactic sugar for template constraints, believing `enable_if` could do the job just as well. But after working through this example, I realized that the true value of concepts isn't about "whether it compiles," but rather **telling you why it failed when it doesn't compile**.

Here is a sorting function I wrote with concept constraints:

```cpp
#include <concepts>
#include <iterator>
#include <functional>
#include <vector>
#include <iostream>
#include <list>

// 我自己写的排序包装，用 concept 把要求说清楚
template<std::random_access_iterator It, typename Comp = std::less<>>
    requires std::sortable<It, Comp>
void my_sort(It first, It last, Comp comp = {}) {
    std::sort(first, last, comp);
}

int main() {
    // 这个能正常编译
    std::vector<int> vec = {5, 3, 1, 4, 2};
    my_sort(vec.begin(), vec.end());
    for (int x : vec) std::cout << x << " ";
    std::cout << "\n";

    // 这个会在编译期被拦住
    std::list<int> lst = {5, 3, 1, 4, 2};
    my_sort(lst.begin(), lst.end());  // 编译错误！
    return 0;
}
```

Now, when compiling the call to `list`, the error has changed to this:

```text
error: constraint not satisfied
required: 'std::random_access_iterator<std::_List_iterator<int>>'
note: no known conversion from 'std::bidirectional_iterator_tag' to 'std::random_access_iterator_tag'
```

**This is plain English, folks!** It tells us that the `list` iterator is a bidirectional iterator, while the requirement is a random access iterator, so the types don't match. You don't need to dig into the `stl_algo.h` source code, nor do you need to understand the SFINAE (Substitution Failure Is Not An Error) mechanism; the error message points directly to the constraint itself.

I specifically checked what `std::sortable` actually requires. The definition chain is roughly: `std::sortable<I>` requires `std::permutable<I>`, and `std::permutable<I>` requires `std::forward_iterator<I>`—note that this only requires a **forward iterator**, not a random access iterator. Additionally, the iterator's value type must satisfy `indirect_strict_weak_order` (meaning it can be compared using a given predicate) and support `swap` operations. Previously, all of this was hidden in the prose of the standard documentation, something only library implementers would look at. Now, it has become a queryable, referenceable code entity; you can even jump to the definition in your IDE.

:::warning Correction from Original Text
The original draft incorrectly stated that `std::sortable` requires a `random_access_iterator`. This is wrong.

Authoritative source (cppreference) text:
> `template<class I, class Comp = ranges::less, class Proj = std::identity> concept sortable = std::permutable<I> && std::indirect_strict_weak_order<Comp, std::projected<I, Proj>>;`
>
> Where `permutable<I>` requires `forward_iterator<I>`.
> — cppreference, std::sortable<RefLink :id="1" preview="cppreference, std::sortable" />

Actual verification result (GCC 16.1.1, `-std=c++20`):

```cpp
static_assert(std::sortable<std::forward_list<int>::iterator>);  // 通过！
static_assert(std::sortable<std::list<int>::iterator>);           // 通过！
static_assert(std::sortable<std::vector<int>::iterator>);         // 通过！
```

`forward_list` only has forward iterators, but it still satisfies `std::sortable`.

It is important to distinguish: the **`std::sort` algorithm** requires random-access iterators, but the **`std::sortable` concept** only requires forward iterators. The former is an implementation constraint of the algorithm, while the latter is the minimal requirement of the concept.
:::

So, looking back, concepts are not just syntactic sugar to "make template errors look prettier." They are the missing piece of the puzzle that generic programming has lacked for over thirty years. The so-called generic code we wrote before was actually "generic code without declared constraints"—the constraints existed, but only in documentation and in the programmer's mind, invisible to the compiler. Now, concepts make constraints an explicit part of the code, allowing the compiler to finally do what it should have been doing all along.

---

# Iterator Pitfalls and the Range Solution

Honestly, for the first two years of learning C++, I took the standard library algorithm calling convention for granted—pass a `begin`, pass an `end`, pass a comparison function, and this trio handles everything. It wasn't until I recently mistakenly called `std::sort` on a `std::list` and stared at the screen full of template error messages for a full twenty minutes that I truly understood what problems C++20 concepts and ranges are actually solving. Today, I want to fully document this journey from "pain to enlightenment."

## But the iterator pair has an even bigger pitfall

Am I satisfied just because the error messages look better? No. Because I thought of an even more terrifying problem.

I've seen code like this in projects before—someone passed `begin` and `end` in the wrong order:

```cpp
std::vector<int> vec = {1, 2, 3, 4, 5};
std::sort(vec.end(), vec.begin());  // 注意：反了！
```

Do you know what happens here? It won't crash immediately. Internally, `std::sort` calculates `last - first`, resulting in a very large number (since subtracting pointers where `end` precedes `begin` should yield a negative value, but the conversion to an unsigned type turns it into a massive positive value). The algorithm then proceeds to read and write out-of-bounds memory frantically. It might run for a long time before causing a segmentation fault, or it might "silently" corrupt your heap memory and crash in a completely unrelated location. I spent an entire afternoon debugging a bug like this once.

There is an even more absurd scenario—where two iterators come from different containers:

```cpp
std::vector<int> a = {1, 2, 3};
std::vector<int> b = {4, 5, 6};
std::sort(a.begin(), b.end());  // 两个不同容器的迭代器！
```

This is undefined behavior (UB) according to the C++ standard, but the compiler won't stop you at all. From the perspective of the type system, the types of `a.begin()` and `b.end()` are identical—both are `std::vector<int>::iterator`. The compiler has no way to know whether they originate from the same container.

Simply adding concept constraints to iterators won't solve these problems. The issue isn't "what type" the iterators are, but whether the "relationship" between this pair of iterators is valid.

## Ranges Are the Right Way

C++20 introduced ranges not to show off, but to fundamentally address the design flaw of "iterator pairs."

A range naturally represents "a contiguous sequence of elements from a container." It eliminates the possibility of `begin` and `end` coming from different containers, and it avoids the issue of reversed order (although theoretically you could construct a range with a mismatched sentinel, this won't happen with normal usage).

Besides, honestly, writing `xxx.begin(), xxx.end()` every time we call an algorithm is just too verbose. Plus, we've seen bugs like `A.begin(), B.end()` before... Well, ranges, I like you!

Let's take a look at how clean the range-based syntax is:

```cpp
#include <ranges>
#include <algorithm>
#include <vector>
#include <string>
#include <iostream>

// 我自己包装的 range 版排序
template<std::ranges::random_access_range R,
         typename Comp = std::ranges::less>
    requires std::sortable<std::ranges::iterator_t<R>, Comp>
void my_sort(R&& r, Comp comp = {}) {
    std::ranges::sort(std::forward<R>(r), comp);
}

int main() {
    // vector of doubles，升序
    std::vector<double> vd = {3.14, 1.41, 2.72, 0.58};
    my_sort(vd);
    for (double x : vd) std::cout << x << " ";
    std::cout << "\n";

    // vector of strings，降序
    std::vector<std::string> vs = {"hello", "world", "cpp", "ranges"};
    my_sort(vs, std::ranges::greater{});
    for (const auto& s : vs) std::cout << s << " ";
    std::cout << "\n";

    return 0;
}
```

Please provide the Chinese Markdown content you would like me to translate. I am ready to apply the specified terminology, style guide, and formatting rules to your embedded systems and modern C++ documentation.

```text
0.58 1.41 2.72 3.14
world hello ranges cpp
```

See, when we call it, we only need to pass a range object. We don't need `begin()` or `end()`, nor do we need to worry about whether two iterators match. Furthermore, the constraint is written as `std::ranges::random_access_range`, which directly expresses "this thing must support random access," rather than "this thing's iterator must satisfy certain conditions." The semantic level is significantly higher.

If you try to pass a `list` in:

```cpp
std::list<int> lst = {5, 3, 1, 4, 2};
my_sort(lst);  // 编译错误
```

The error will directly tell you that `std::list<int>` does not satisfy `random_access_range`. Clean and simple.

I used to think that ranges were just syntactic sugar. The pipeline style using `views::transform` and `views::filter` looked cool but unnecessary. Looking back now, I realize the core value of ranges is actually **replacing the error-prone abstraction of "a pair of iterators" with the less error-prone abstraction of "a single range"**. The pipeline syntax is just a bonus.

At this point, I have completely grasped the evolution logic from iterators to ranges. But the story isn't over—in the example above, I sorted a `vector<string>` in descending order using `std::ranges::greater{}`. This looks fine, but what if you have more specific requirements for string sorting? For example, sorting by length, or sorting lexicographically while ignoring case? This involves customizing predicates, so let's keep reading.

---

# Concept Composition and Overload Resolution

My understanding of concepts used to be stuck at the level of "it's just syntactic sugar for SFINAE." I thought it just made compiler errors look better and the code slightly cleaner, but fundamentally, it was still doing the same old template stuff. Is that right? If it were, I wouldn't be writing this note.

## From sort to forward_sortable_range

It all started when I needed to sort a `std::forward_list`. I had a habit of writing a generic `sort` function with no constraints, just listing the template parameters and stuffing any type into it. Guess what happened? The compiler didn't complain, of course, but it blew up at runtime because `std::sort` requires random access iterators under the hood, while `forward_list` only has forward iterators. This error is completely invisible during compilation and only exposes itself at runtime, making debugging a nightmare.

So, can we block this kind of error at the type system level? Not relying on documentation saying "Do not use this function on lists" (let's face it, everyone is busy and no one has time to read docs, unless the compiler has already scolded you!), but making the code itself physically prevent you from doing so. This is the core problem concepts aim to solve—it's not about "prettier error messages," but about "making incorrect usage unwriteable."

I wrote a constraint for forward sortable ranges and provided an overload of `sort` based on this constraint. First, let's look at the concept I defined:

```cpp
#include <concepts>
#include <ranges>
#include <forward_list>
#include <vector>
#include <algorithm>
#include <iostream>
#include <iterator>

// 先定义一个"前向可排序范围"的 concept
// 它说的是：这个范围必须是 forward_range，并且它的元素必须能用给定的谓词进行比较
template<typename R, typename C = std::less<>>
concept forward_sortable_range =
    std::ranges::forward_range<R> &&
    requires(R& r, C comp) {
        // 需要能拿到前向迭代器
        { std::begin(r) } -> std::forward_iterator;
        // 元素之间需要能用谓词比较
        { *std::begin(r) < *std::begin(r) } -> std::convertible_to<bool>;
    };
```

You might ask, why not just use `std::sortable`? Good question. `std::sortable` exists in the standard library, and it actually only requires forward iterators <RefLink :id="1" preview="cppreference, std::sortable" />—yes, even `forward_list` iterators satisfy `std::sortable`. However, I want to express the semantic nuance that "this range is sortable, but not necessarily via random access," so I chose to define a more explicit constraint. Additionally, `forward_sortable_range` explicitly checks the comparison operations between elements, which expresses intent better than using raw `std::sortable` in certain scenarios. This is the power of concepts—we can precisely express the semantics we need, rather than being tied down to a specific standard library concept.

Then, I wrote two `sort` overloads: one for random access ranges, and one for forward ranges:

```cpp
// 重载1：给随机访问范围用的（vector、deque 等）
// 约束更严格，编译器会优先匹配这个
template<std::ranges::random_access_range R, typename C = std::less<>>
    requires std::sortable<std::ranges::iterator_t<R>, C>
void my_sort(R& r, C comp = C{}) {
    std::ranges::sort(r, comp);
    std::cout << "  [走随机访问路径]\n";
}

// 重载2：给前向可排序范围用的（forward_list 等）
// 关键：用 !random_access_range 显式排除随机访问范围，避免歧义
template<forward_sortable_range R, typename C = std::less<>>
    requires (!std::ranges::random_access_range<R>)
void my_sort(R& r, C comp = C{}) {
    // 简单实现：复制到 vector，排序，再复制回来
    // 生产环境可以用更高效的 list 排序算法，这里只是为了演示
    std::vector<std::ranges::range_value_t<R>> tmp(
        std::begin(r), std::end(r)
    );
    std::ranges::sort(tmp, comp);
    std::ranges::copy(tmp, std::begin(r));
    std::cout << "  [走前向迭代器路径：复制-排序-回写]\n";
}
```

Here is a particularly important point, and a pitfall I fell into myself: **disambiguation rules for concept overloading**. In the initial draft, I assumed that "the compiler would automatically select the overload with the strictest constraint," but actual testing revealed that when Overload 1's constraint is `std::ranges::random_access_range` and Overload 2's constraint is a custom `forward_sortable_range`, there is no subsumption relationship between the two constraints—the compiler cannot determine which is stricter, resulting in an **ambiguity error**.

:::warning Correction: Disambiguation of Concept Overloading
The original text claimed that "when multiple overloads match, the compiler will select the one with the strictest constraint." This statement holds true under specific conditions (when a subsumption relationship exists between the two constraints), but it does not necessarily hold for custom concepts.

The C++20 constraint partial ordering rules ([temp.constr.order]) require that Overload A's constraints must **subsume** Overload B's constraints for the compiler to select A. While `std::ranges::random_access_range` does subsume `std::ranges::forward_range` (since the former is a refinement of the latter), it **does not** subsume the custom `forward_sortable_range` (because the latter's `requires` clause contains different atomic constraints).

Actual verification results (GCC 16.1.1, `-std=c++20`):

```text
error: call of overloaded 'my_sort(std::vector<int>&)' is ambiguous
```

Fix: Add `requires (!std::ranges::random_access_range<R>)` to overload 2 to explicitly exclude random-access ranges, preventing both overloads from matching simultaneously.
:::

This `!random_access_range` trick is quite useful—essentially telling the compiler, "Only consider overload 2 if the constraints for overload 1 are not met." When passing a `vector`, overload 2 is excluded; when passing a `forward_list`, overload 1 is not satisfied. Each case matches a unique candidate, eliminating ambiguity.

Let's run this to verify:

```cpp
int main() {
    // 测试1：vector 走随机访问路径
    std::vector<int> v = {5, 3, 1, 4, 2};
    std::cout << "排序 vector: ";
    my_sort(v);
    for (int x : v) std::cout << x << ' ';
    std::cout << '\n';

    // 测试2：forward_list 走前向迭代器路径
    std::forward_list<int> fl = {5, 3, 1, 4, 2};
    std::cout << "排序 forward_list: ";
    my_sort(fl);
    for (int x : fl) std::cout << x << ' ';
    std::cout << '\n';

    // 测试3：用 greater 降序排
    std::vector<int> v2 = {1, 2, 3, 4, 5};
    std::cout << "降序排序 vector: ";
    my_sort(v2, std::greater<>{});
    for (int x : v2) std::cout << x << ' ';
    std::cout << '\n';

    return 0;
}
```

Compile and run (GCC 16.1.1, `-std=c++20`):

```text
排序 vector:   [走随机访问路径]
1 2 3 4 5
排序 forward_list:   [走前向迭代器路径：复制-排序-回写]
1 2 3 4 5
降序排序 vector:   [走随机访问路径]
5 4 3 2 1
```

Perfect, the two paths go their separate ways without interfering with each other. Notice that I provided a default value for the predicate, `std::less<>`. This covers common cases so we don't have to pass it every time, and if we want descending order, we just pass `std::greater<>{}`. This habit of "providing sensible defaults" is something I learned from the standard library; it significantly reduces the burden on the caller.

## Concepts Aren't New, They've Always Been There

After finishing the example above, I looked back and suddenly realized something: concepts weren't invented in C++20.

If you look at history, Dennis Ritchie implicitly used concepts in early C—`int` and `float` are two concepts, although they weren't called that back then; they were called "types." When you write a function accepting `int`, you are essentially saying, "I need something that satisfies integer semantics." STL has them too. When Stepanov designed STL, he had concepts like iterator, container, and sequence in mind, but since C++ lacked language-level support at the time, these concepts existed only in documentation and in the designer's mind, existing as implicit contracts. Looking further back, the field of mathematics had abstract concepts like monads, groups, and rings centuries ago, and concepts in graph theory can even be traced back to Euler's 1736 paper on the Seven Bridges of Königsberg.

So, what is the essence of concepts? **It is the formal expression of domain knowledge.** Whether you use the C++ `concept` keyword or not, as long as you are doing generic programming, you must have concepts in your head. The only difference is: previously, these concepts were implicit, hidden in the designer's brain and documentation, unknown to the compiler; now you can write them as code, and the compiler can check them for you.

I've seen a lot of so-called "generic" C++ code before where template parameters are just written as `typename T` without any constraints, and then a comment says "T must support addition and multiplication." Isn't that just an unformalized concept? Can I skip the comment? Can the compiler check it for you? No to both. So, this code crashes as soon as you pass the wrong type, and the crash location is miles away from the actual error.

## From "Template Programming" to "Concept-Based Generic Programming"

I increasingly feel that we should stop saying "template programming" and instead call it "concept-based generic programming." What's the difference between these two terms?

"Template programming" focuses on "how to instantiate." You think about type deduction, SFINAE, specialization ordering, and other mechanism-level details. "Concept-based generic programming" focuses on "what I need." You think, "I need a sortable forward range," then you write this requirement as a concept, and finally write the function that satisfies this concept. The mechanism becomes an implementation detail. See, this aligns our programming mindset—focus on "what is needed" rather than "how to implement it."

This shift in thinking was pivotal for me. Previously, when I wrote template code, I would always write the function body first, find out it wouldn't compile, and then patch it up with SFINAE. The whole process was "bottom-up." Now I've learned to define the concept first, think through the requirements clearly, and then write the implementation. The whole process is "top-down." It's not only smoother to write but also clearer to read—seeing the concept constraints on the function signature tells you immediately what the function expects, without needing to dig into the implementation.

Furthermore, concepts are often layered and composed, just like my `forward_sortable_range` above, which is composed of more basic concepts like `forward_range` and `forward_iterator`. The more and finer-grained concepts you define, the more flexible they are to reuse. This is the same principle as function decomposition—good concept design is like good function design; it's all about "correct levels of abstraction."

Seen this way, concepts aren't a new toy created out of thin air by C++20; they are the missing piece of the puzzle in generic programming. Without them, generic programming is still possible, but it's like walking a tightrope blindfolded; with them, you at least have a balance beam. Looking back, it's not that hard, but before you figure it out, it just feels awkward.

---

# `requires` Expressions and Usage Patterns

When exactly should we use a `requires` expression, and when should we define a named concept? When I saw the quote in a talk saying "If you require requires in your code, you might be doing something wrong" <RefLink :id="3" preview="Stroustrup, Concept-based Generic Programming, CppCon 2025" />, I really resonated with it—turns out I wasn't the only one confused by this; there really is a clear criterion for judgment.

Today, let's thoroughly clarify this.

## Starting with a Simple Combination

I used to think that concept composition was some profound, complex thing. Then one day, I was writing a generic sorting function that needed to require both "this range is forward iterable" and "elements in this range are sortable." I wrote a bunch of messy constraints, only to realize later that it was just connecting two concepts with `&&`. There is no essential difference from writing a logical AND operation in a normal function.

```cpp
#include <concepts>
#include <ranges>
#include <vector>
#include <algorithm>
#include <iostream>

// 我自己定义的一个 concept：可排序的范围
// 本质上就是 forward_range 和 sortable 的"与"操作
template<typename R>
concept sortable_range = std::ranges::forward_range<R> && std::sortable<std::ranges::iterator_t<R>>;

// 用这个组合出来的 concept 去约束函数模板
template<sortable_range R>
void my_sort(R&& r) {
    std::ranges::sort(std::forward<R>(r));
}

int main() {
    std::vector<int> v{3, 1, 4, 1, 5, 9, 2, 6};
    my_sort(v);  // 编译通过，vector<int> 既满足 forward_range 又满足 sortable

    // my_sort("hello");  // 编译错误，字符串不满足 sortable
    // 报错信息会明确告诉你：约束 'sortable_range<R>' 未满足

    for (int x : v) std::cout << x << ' ';
    // 输出：1 1 2 3 4 5 6 9
}
```

You see, although the syntax involves writing `sortable_range R` in the template parameter list instead of the typical `typename R`, the definition of the concept itself is simply an expression that returns a bool. `std::ranges::forward_range<R>` is a bool, `std::sortable<...>` is a bool, and combining two bools with `&&` yields a bool. It is just that simple. I used to overthink it, assuming there was some special syntactic magic involved, but there isn't.

## The `requires` expression: The underlying brick of concepts

Once we understand composition, the next question is: how are these standard library concepts actually implemented? The answer is the `requires` expression.

I was initially confused when I saw the `requires` keyword appear in two different places—the `requires` clause (placed after the function signature) and the `requires` expression (containing a list of checks inside braces). These two things share the same name but have completely different responsibilities. The `requires` expression is the one that does the actual work by checking whether a specific construct is valid.

Let's look at how we might write the classic `equality_comparable` ourselves:

```cpp
#include <concepts>
#include <type_traits>

// 自己实现一个简化版的 equality_comparable
// 检查 T 和 U 之间是否可以进行相等和不相等比较
template<typename T, typename U>
concept my_equality_comparable =
    requires(const T& t, const U& u) {
        // 下面每一行都是一个"使用模式"的检查
        // 编译器会尝试编译这些表达式，如果都能编译通过，这一项就是 true
        { t == u } -> std::convertible_to<bool>;
        { u == t } -> std::convertible_to<bool>;
        { t != u } -> std::convertible_to<bool>;
        { u != t } -> std::convertible_to<bool>;
    };

// 验证一下
static_assert(my_equality_comparable<int, double>);   // int 和 double 可以比较
static_assert(my_equality_comparable<int, int>);      // 同类型当然可以
static_assert(!my_equality_comparable<int, std::nullptr_t>);  // int 和 nullptr 不能比较
```

I've encountered a few pitfalls regarding these details. First, the parameter list `const T& t, const U& u` inside the `requires` braces introduces some "hypothetical variables" that are used solely for the internal check; they are not actually instantiated. Second, in the syntax `{ t == u } -> std::convertible_to<bool>`, the braces contain the expression to be checked, and the arrow specifies the requirement for the return type. Note that we use `convertible_to<bool>` instead of `same_as<bool>`, because the `==` operator does not necessarily return a strict `bool` type; as long as it can be implicitly converted to `bool`, it is sufficient—this is explicitly defined in the C++ standard.

## What does "require a requires" actually mean?

The talk mentioned that "if you require a requires in your code, you might be doing something wrong." I didn't grasp this at first, but upon reflection, it refers to situations like this:

```cpp
// 反面教材：直接在函数约束里写 requires 表达式
template<typename T>
    requires requires(T t) { t + t; }
auto add_stuff(T a, T b) {
    return a + b;
}

// 正确做法：给它起个名字，定义成 concept
template<typename T>
concept addable = requires(T t) { t + t; };

template<addable T>
auto add_stuff(T a, T b) {
    return a + b;
}
```

Why is the first approach bad? Because when you see the error message, you are greeted with a wall of expanded `requires` expressions, making it impossible to discern the "semantic intent" of the constraint. With the second approach, the compiler will directly tell you that "constraint `addable<T>` not satisfied," which is immediately clear just by looking at the name. This demonstrates the value of "concepts with meaningful names." The `requires` expression is the brick, and the concept is the house built with those bricks; naturally, you should live inside the house, not directly on the bricks.

## Usage Patterns: Why It Changes the Game

What I am about to discuss is, in my opinion, the most ingenious design feature of concepts—usage patterns.

I used to assume that if I wanted to constrain a type to support the `+` operator, I needed to specify exactly how that `+` is implemented. Is it a member function `T::operator+`? Is it a free function `operator+(T, T)`? Are the parameters `const`-qualified? What is the exact return type? If I had to spell out all these details in a concept, it would be a nightmare, placing a massive burden on anyone using that concept.

Usage patterns, however, completely flip the script: they don't care how you implement it, only whether "the task can be done."

```cpp
#include <concepts>
#include <string>

// 我只要求 A + B 这个表达式能编译通过，并且结果能转成某种公共类型
// 至于 A + B 是通过成员函数实现还是自由函数实现，我完全不关心
template<typename A, typename B>
concept can_add = requires(A a, B b) {
    { a + b } -> std::convertible_to<std::common_type_t<A, B>>;
};

// 来验证一下使用模式的威力

// 情况1：内置类型的加法
static_assert(can_add<int, int>);

// 情况2：混合模式算术，int + double
static_assert(can_add<int, double>);

// 情况3：std::string 的加法（通过自由函数 operator+ 实现）
static_assert(can_add<std::string, std::string>);

// 情况4：自定义类型，用成员函数实现 operator+
class MyInt {
    int val;
public:
    MyInt(int v) : val(v) {}
    MyInt operator+(const MyInt& other) const { return MyInt(val + other.val); }
};
static_assert(can_add<MyInt, MyInt>);

// 情况5：另一个自定义类型，用自由函数实现 operator+
class MyFloat {
    float val;
public:
    MyFloat(float v) : val(v) {}
    float get() const { return val; }
};
MyFloat operator+(const MyFloat& a, const MyFloat& b) {
    return MyFloat(a.get() + b.get());
}
static_assert(can_add<MyFloat, MyFloat>);

// 情况6：int 和 std::string 不能相加
static_assert(!can_add<int, std::string>);
```

:::details Original Code Correction Notes
In the initial draft, the definition of `can_add` used a default template parameter `typename R = std::remove_cvref_t<decltype(std::declval<A>() + std::declval<B>())>` to deduce the return type. This approach has a pitfall: when `A + B` is invalid (e.g., `int + std::string`), the evaluation of the default parameter fails during the template argument substitution phase, resulting in a **hard compilation error** instead of the concept returning `false`.

Actual verification results (GCC 16.1.1, `-std=c++20`):

```text
error: no match for 'operator+' (operand types are 'int' and 'std::__cxx11::basic_string<char>')
```

This is a hard error—`static_assert(!can_add<int, std::string>)` fails to compile entirely.

The fix: remove the return type deduction from the default template parameter and use `std::common_type_t<A, B>` as the constraint target. This way, when `A + B` is invalid, only the check inside the requires expression fails (in the "immediate context"), and the concept correctly returns `false`.
:::

This is where things get really exciting. The `can_add` concept works for both `MyInt` (implemented via a member function) and `MyFloat` (implemented via a free function). It doesn't care about the implementation details at all. This means the interface becomes incredibly stable—we can implement `operator+` as a member function today and switch to a free function tomorrow. As long as the `a + b` expression remains valid, none of the code relying on the `can_add` concept needs to change. This level of stability was simply impossible to achieve with SFINAE and tag dispatch in the past.

Furthermore, this checking is implicit. What does "implicit" mean? It means that when we instantiate a template, the compiler automatically verifies the constraints for us, without us needing to write any extra code. However, if we want to be sure, we can explicitly verify that a specific type satisfies a concept as early as possible, just like the `static_assert` examples we wrote above. This flexibility is excellent—the set of types is open; anyone can write a new type, and as long as it fits the usage pattern, it works. At the same time, we can explicitly add guards wherever we need extra protection.

## Handling Mixed-Mode Arithmetic and Implicit Conversions

Another advantage of the usage pattern is that it naturally handles C++'s complex implicit conversion rules. For example, `int + double` works because `int` is implicitly converted to `double`. The usage pattern doesn't care how this conversion happens; it simply verifies whether the `int + double` expression ultimately compiles.

```cpp
#include <concepts>

template<typename A, typename B>
concept can_compare = requires(A a, B b) {
    { a == b } -> std::convertible_to<bool>;
};

// int 和 double 可以比较，因为 int 会隐式转换为 double
static_assert(can_compare<int, double>);

// int 和 long 可以比较
static_assert(can_compare<int, long>);

// int 和 std::string 不行，没有从 string 到 int 的隐式转换
static_assert(!can_compare<int, std::string>);
```

You might ask: what if we want more precise control, disallowing implicit conversions and requiring exact type matches? We can use `std::same_as` instead of `std::convertible_to`, or add more constraints within the `requires` expression. The usage pattern provides the most relaxed default behavior, but we can tighten it up whenever needed. This is a vast improvement over the old approach of "checking nothing by default."

## Why concepts must be part of the language, not an isolated sub-language

Finally, here is a point that I hadn't fully grasped until now. The talk mentioned, "I don't like isolated sub-languages that stand alone," and that really struck a chord with me.

Concepts are not a separate, isolated world within C++. They work alongside `if constexpr`, coexist with SFINAE (although we no longer need to write it manually), and integrate with `constexpr` functions and modules. They simply use C++'s existing language features—any valid C++ expression can be written inside a `requires` expression, and a concept definition is just a standard `template` combined with a `bool` constant expression.

This means we don't need to learn a separate "concept-specific syntax" and then a separate "C++ syntax"—we are simply learning C++ itself. Concepts transform generic programming from "using template metaprogramming black magic to simulate constraints" into "using the language itself to express constraints." Once this clicks, looking back, it isn't actually that difficult; the hard part is breaking the old habits of thinking in terms of SFINAE.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="std::sortable"
    :year="2020"
    url="https://en.cppreference.com/w/cpp/iterator/sortable"
  />
  <ReferenceItem
    :id="2"
    author="cppreference.com"
    title="std::ranges::sort"
    :year="2020"
    url="https://en.cppreference.com/w/cpp/algorithm/ranges/sort"
  />
  <ReferenceItem
    :id="3"
    author="Bjarne Stroustrup"
    title="Concept-based Generic Programming"
    publisher="CppCon 2025"
    :year="2025"
    url="https://www.youtube.com/watch?v=VMGB75hsDQo"
  />
  <ReferenceItem
    :id="4"
    author="Alexander Stepanov, Meng Lee"
    title="The Standard Template Library"
    publisher="HP Laboratories"
    :year="1995"
    chapter="TR95-11(R.1)"
    url="https://www.stepanovpapers.com/stl.pdf"
  />
  <ReferenceItem
    :id="5"
    author="cppreference.com"
    title="C++ named requirements: LegacyRandomAccessIterator"
    :year="2020"
    url="https://en.cppreference.com/w/cpp/named_req/RandomAccessIterator"
  />
</ReferenceCard>
