---
title: Ranges, Iterators, and Concept Combinations
description: CppCon 2025 talk notes — from the problems with iterator pairs to Range
  abstractions, then to Concept composition and requires expressions
conference: cppcon
conference_year: 2025
talk_title: Concept-based Generic Programming
speaker: Bjarne Stroustrup
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW
video_youtube: https://www.youtube.com/watch?v=VMGB75hsDQo
tags:
- cpp-modern
- host
- intermediate
difficulty: intermediate
platform: host
cpp_standard:
- 20
- 23
chapter: 1
order: 2
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/01-concept-based-generic-programming/02-range-and-concept-composition.md
  source_hash: f8ffa47fc5c0d3fefa9ec35bd85b19babfabe35422ee000ac0049b29efa51d6c
  translated_at: '2026-05-26T11:05:48.659458+00:00'
  engine: anthropic
  token_count: 6360
---
# Unchecked Pointers and the Boundaries of Generic Programming

Back when I was writing C++, I ran into a very typical problem: I'd get a pointer and want to take its first 10 elements to form a sub-view, but the code always felt awkward no matter how I wrote it. For example, if you have a `double*`, and you want to say "I want the first 10 elements this pointer points to," just looking at that code, neither you nor the compiler has any way to know how many elements the pointer actually points to, or whether 10 is out of bounds. This is completely unchecked. I used to think there was nothing to be done about it—pointers are just like that. But later I realized that if your code review doesn't flag this pattern as a potential issue, the review itself isn't rigorous enough.

Of course, in reality we sometimes do get raw pointers from various external systems, C interfaces, or legacy code. You can't just say "I don't touch pointers," so we must have this capability. But the key question is: can you, as soon as you get a pointer, wrap it in something that carries boundary information and type safety checks? That's what I hadn't figured out before—I assumed "using pointers" and "type safety" were contradictory, but they aren't. They belong to two different stages.

## First, Let's Fix a Minor Annoyance

Before diving into deeper topics, I want to mention an issue that nearly drove me crazy while typing. Previously, when I was working on type-safe numerics, I had to write things like `number_of<double>`, explicitly spelling out `double` every single time. It was way too tedious. I'm not a fast typist to begin with, and honestly, the people who designed and iterated on C and Unix probably weren't fast typists either—which is why you see names like `int`, `double`, and `ptr` that are absurdly short. But we have type deduction now, so why are we still typing this out manually?

My approach is: if `number` has an initializer, just take the initializer's type as the base type for `number`. For example, I can write `number_of{1}`, and it deduces to `number_of<int>`; write `number_of{3u}`, and it's `number_of<unsigned>`; write `number_of{1.0}`, and it's `number_of<double>`. Only when you truly need it—like when you initialize with an integer but want `double` precision—do you need to explicitly write `number_of<double>{1}`. This way, in daily use you barely type any extra characters, but you don't lose any type safety.

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

See? It compiles and runs, and all the `static_assert` checks pass. I used to think CTAD was just syntactic sugar, but in scenarios like this, it makes writing type-safe code just as smooth as writing ordinary code.

## Does This Count as Generic Programming?

At this point you might ask: does this count as generic programming? Isn't it just a template class with CTAD?

I hesitated about this too, but at this point, I believe it is generic programming. It uses generic programming techniques to solve a fundamental problem caused by C++'s history: implicit conversions between numeric types lead to all sorts of hard-to-spot bugs. You could design a new language without this historical baggage, but we don't have that luxury. We can only use a small library within C++ to eliminate these problems. And notice this: the core logic for implementing a type-checked `number` is only about 37 lines; implementing a bounds-checked `span` is under 100 lines. That's shorter than the specification document describing the language's behavior. Using minimal code to solve a systemic problem—isn't that exactly what generic programming should do? (Broadly speaking, describing what a system should do without worrying about the vast majority of common details—that is generic programming.)

## The Classic Problem That Really Gave Me Headaches: std::sort Error Messages

Alright, warm-up's over. Let's talk about a problem I struggled with for a long time and finally started to understand.

You've definitely used `std::sort`. Its signature looks roughly like this: it takes two random-access iterators, `first` and `last`, plus an optional comparison function. The C++ standard document states clearly: these two iterators must satisfy the LegacyRandomAccessIterator requirements, the iterator's value type must satisfy MoveAssignable and MoveConstructible, and the comparison function must satisfy StrictWeakOrdering...

But the problem is, these requirements are never directly checked.

They only exist in the documentation, in the minds of the committee members. When the compiler instantiates `std::sort`, it doesn't first verify whether your iterator is a random-access iterator. It just hard-instantiates it, and then at some point deep in the template expansion, if your type doesn't satisfy the requirements, it throws a several-hundred-line error in some completely unrelated place. You might pass in a `std::list` iterator, and the error message tells you some `__move_assign` failed, or some `__gap` variable has issues. When you see that error message, you're just completely lost.

### Reproducing the Error That Made Me Lose It

Let me set up the environment first: I'm using GCC 16.1.1, with `-std=c++20` enabled, running on Arch Linux WSL. The compile command is just the standard `g++ -std=c++20 -Wall -Wextra`.

First, write some code that looks perfectly fine:

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

Guess what? The compilation just explodes. Let me grab a relatively "readable" snippet from the error output:

```text
/usr/include/c++/16/bits/stl_algo.h: In instantiation of 'void std::sort(_RandomAccessIterator, _RandomAccessIterator, _Compare) [with _RandomAccessIterator = std::_List_iterator<int>; _Compare = __gnu_cxx::__ops::_Iter_less_iter]':
...
error: no match for 'operator-' (operand types are: 'std::_List_iterator<int>' and 'std::_List_iterator<int>')
```

When I saw this error, I knew the iterator type was wrong because I'd learned that `list` is a doubly-linked list and doesn't support random access. But what if you're a beginner who's been learning for less than six months? You'd see `no match for 'operator-'` and start wondering: did I forget to overload some operator? Did I miss some header file include? This error message tells you absolutely nothing about the real problem—**you used an iterator that doesn't support random access to call an algorithm that requires it**.

I used to think "ugly template errors" was an over-complained-about topic, figuring you'd get used to it after seeing them a few times. But this time I thought about it seriously, and that's not how it is. The problem isn't that the error is "long"—it's that the error message describes the **symptom** (can't find `operator-`) rather than the **root cause** (iterator category doesn't satisfy requirements). For someone unfamiliar with template metaprogramming, the gap between those two is an uncrossable chasm.

## What About Now?

Now we have concepts.

Concepts were introduced in C++20, but their ideological roots trace back to Alex Stepanov's (the father of the STL) original vision for generic programming<RefLink :id="4" preview="Stepanov & Lee, The Standard Template Library, 1995" />. From the very beginning, he believed that generic algorithms should have clear, checkable requirements for their parameters. This isn't some optional nice-to-have—it's foundational infrastructure for generic programming. It just took C++ over thirty years to build that infrastructure.

Looking back at this now, it feels like a room that was always missing a wall. Everyone got used to the wind blowing in, even learned how to live with it, until one day someone finally built the wall, and you realized: wow, it can be this comfortable.

Next, I want to write some code and see how concepts actually change the way we write generic code. Not those textbook `template<std::integral T>` examples, but usages that solve real problems. Let's start with the simplest scenario: write a `sort` constraint ourselves, then deliberately pass in the wrong type and see just how good the error message can be.

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

Try uncommenting those last two lines. On my end (GCC 16.1.1, `-std=c++20`), the error message directly tells you: constraint not satisfied, `std::list<int>::iterator` does not satisfy `random_access_iterator`. No 400-line template expansion, no `__gap`, no `__move_assign`—just one sentence: your iterator type is wrong.

When I saw this error message, it felt incredibly satisfying. After being tortured by `std::sort` error messages so many times, it turns out the solution is this simple—you don't need any extra tools, you don't need scripts to prettify error messages, you just write the constraints on the function signature. The compiler already had the ability to check; it just didn't have the syntax to let you express the constraint before.

### Intercepting Errors at the Door with Concepts

In the C++20 standard library, those concepts that previously only existed as prose descriptions in the standard document have now become real code entities. This includes `std::random_access_iterator` and `std::sortable`.

I used to think concepts were just syntactic sugar for template constraints, and that `enable_if` could do the same job. But after working through this example, I finally understood that the real value of concepts isn't in "whether it compiles," but in **telling you why it failed to compile**.

Here's a sorting function I wrote with concept constraints:

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

Now when compiling that `list` call, the error becomes this:

```text
error: constraint not satisfied
required: 'std::random_access_iterator<std::_List_iterator<int>>'
note: no known conversion from 'std::bidirectional_iterator_tag' to 'std::random_access_iterator_tag'
```

**This is plain English, folks!** It tells you that `list`'s iterator is a bidirectional iterator, but you required a random-access iterator—the types don't match. You don't need to dig into `stl_algo.h`'s source code, you don't need to understand SFINAE substitution failure mechanisms. The error message points directly at the constraint itself.

I specifically looked up what `std::sortable` actually requires. Its definition chain is roughly: `std::sortable<I>` requires `std::permutable<I>`, and `std::permutable<I>` requires `std::forward_iterator<I>`—note, this only requires a **forward iterator**, not a random-access iterator. Additionally, it requires the iterator's value type to satisfy `indirect_strict_weak_order` (meaning it can be compared with a given predicate), and to support `swap` operations. Previously, all of this was buried in the prose descriptions of the standard document; only library implementors would ever look at it. Now it has become a queryable, referenceable code entity. You can even jump to its definition in your IDE.

:::warning Original text correction
The initial draft of the original text stated that `std::sortable`'s iterator requirement was `random_access_iterator`. This is incorrect.

Authoritative source (cppreference) original text:
> `template<class I, class Comp = ranges::less, class Proj = std::identity> concept sortable = std::permutable<I> && std::indirect_strict_weak_order<Comp, std::projected<I, Proj>>;`
>
> where `permutable<I>` requires `forward_iterator<I>`.
> — cppreference, std::sortable<RefLink :id="1" preview="cppreference, std::sortable" />

Actual verification result (GCC 16.1.1, `-std=c++20`):

```cpp
static_assert(std::sortable<std::forward_list<int>::iterator>);  // 通过！
static_assert(std::sortable<std::list<int>::iterator>);           // 通过！
static_assert(std::sortable<std::vector<int>::iterator>);         // 通过！
```

`forward_list` only has forward iterators, but it still satisfies `std::sortable`.

The distinction to make is: the **`std::sort` algorithm** requires random-access iterators, but the **`std::sortable` concept** only requires forward iterators. The former is an algorithm's implementation constraint; the latter is the concept's minimal requirement.
:::

So looking back: concepts are not syntactic sugar that "makes template errors a bit prettier." They complete the puzzle piece that generic programming had been missing for over thirty years. The so-called generic code we wrote before was really "generic code without constraint declarations"—the constraints existed, but only in documentation, in programmers' heads, invisible to the compiler. Now concepts make constraints part of the code, and the compiler can finally do what it should have been doing all along.

---

# Iterator Pitfalls and the Range Solution

Honestly, for the first two years of learning C++, I was completely used to the standard library algorithm calling convention—pass a begin, pass an end, pass a comparison function, the classic three-piece combo. Until the other day, I absentmindedly called `std::sort` on a `std::list`, then stared at that blob of template error output on my screen for a full twenty minutes. Only then did I truly understand what problem C++20's introduction of concepts and ranges was solving. Today, I'm going to document this entire journey "from pain to epiphany."

## But Iterator Pairs Have Even Bigger Pitfalls

Am I satisfied just because the error messages look better? No. Because I thought of an even more terrifying problem.

I've seen code like this in projects before—someone passed `begin` and `end` in the wrong order:

```cpp
std::vector<int> vec = {1, 2, 3, 4, 5};
std::sort(vec.end(), vec.begin());  // 注意：反了！
```

Do you know what happens? It doesn't crash immediately. Internally, `std::sort` computes `last - first`, yielding a very large number (because when subtracting pointers, `end` comes after `begin`, so the result should be positive, but reversed it becomes a negative number cast to unsigned, turning into a huge value). Then the algorithm starts frantically reading and writing out-of-bounds memory. It might run for a long time before segfaulting, or it might "quietly" corrupt your heap memory and crash in a completely unrelated place. I've debugged this kind of bug once—it took me an entire afternoon.

There's an even more absurd scenario—two iterators from different containers:

```cpp
std::vector<int> a = {1, 2, 3};
std::vector<int> b = {4, 5, 6};
std::sort(a.begin(), b.end());  // 两个不同容器的迭代器！
```

This is undefined behavior (UB) in the C++ standard, but the compiler won't stop you at all. Because from the type system's perspective, `a.begin()` and `b.end()` have exactly the same type—they're both `std::vector<int>::iterator`. The compiler has no way to know whether they come from the same container.

These problems can't be solved just by adding concept constraints to iterators. Because the problem isn't "what type the iterator is," but whether "the relationship between this pair of iterators" is valid.

## So Ranges Are the Right Path

C++20 didn't introduce ranges to show off. It introduced them to fundamentally fix the design flaw of "iterator pairs."

A range inherently represents "a contiguous sequence of elements from a container." It can't have begin and end coming from different containers, and it's not easy to get them in the wrong order (though theoretically you could construct a range with a mismatched sentinel, you wouldn't in normal usage).

And honestly, every time you write an algorithm call, the `xxx.begin(), xxx.end()` routine is just too verbose. Plus, there was that whole `A.begin(), B.end()` incident back in the day... Yeah, range, I like you!

Look at how clean the range-based approach is:

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

Output:

```text
0.58 1.41 2.72 3.14
world hello ranges cpp
```

See? When calling it, you only need to pass a range object. No need for `begin()` or `end()`, no need to worry about whether the two iterators match. And the constraint is written as `std::ranges::random_access_range`, directly expressing "this thing must support random access," rather than "this thing's iterator must satisfy some condition." The semantic level is a step higher.

If you try to pass in a `list`:

```cpp
std::list<int> lst = {5, 3, 1, 4, 2};
my_sort(lst);  // 编译错误
```

The error will directly tell you that `std::list<int>` doesn't satisfy `random_access_range`. Clean and decisive.

I used to think ranges were just syntactic sugar, and that the `views::transform` and `views::filter` pipeline style looked cool but was unnecessary. Looking back now, the core value of ranges is actually **replacing the error-prone abstraction of "a pair of iterators" with the less error-prone abstraction of "a range."** The pipeline style is just an incidental bonus.

At this point, I finally fully understood the evolutionary logic from iterators to ranges. But the story isn't over—in the example above, I sorted `vector<string>` in descending order using `std::ranges::greater{}`. This looks fine, but what if you have more nuanced requirements for sorting strings? Like sorting by length, or sorting lexicographically ignoring case? That involves customizing predicates, so let's keep going.

---

# Concept Composition and Overload Resolution

My understanding of concepts had always been stuck at the level of "it's just syntactic sugar for SFINAE." I thought it just made compilation errors prettier and the code a bit cleaner to write, but fundamentally it was still doing the same old template stuff. Was I right? If I were, I probably wouldn't be writing these notes.

## From sort to forward_sortable_range

It started when I needed to sort a `std::forward_list`. I'd always had this habit of writing a generic `sort` function with no constraints at all—just slap down the template parameters and shove every type in there. Guess what happened? The compiler of course didn't report an error, but it blew up at runtime, because `std::sort` internally requires random-access iterators, and `forward_list` only has forward iterators. This kind of error is completely invisible at compile time and only surfaces at runtime, making it absolutely maddening to track down.

So, can we intercept this kind of error at the type system level? Not by relying on documentation that says "please do not use this function with a list" (keep in mind everyone's busy these days and no one has time to read your docs, unless the compiler has already beaten them up!), but by making the code itself disallow it. This is the core problem concepts solve—not "prettier error messages," but "incorrect usage is literally unwriteable."

I wrote a constraint for forward-sortable ranges, then provided an overload of `sort` based on this constraint. First, let's see what the concept I defined looks like:

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

You might ask, why not just use `std::sortable`? Good question. `std::sortable` does exist in the standard library, and it actually only requires forward iterators<RefLink :id="1" preview="cppreference, std::sortable" />—yes, `forward_list`'s iterators also satisfy `std::sortable`. But here I wanted to express the semantic level of "this range can be sorted, but not necessarily via random access," so I chose to define a more explicit constraint myself. Plus, `forward_sortable_range` additionally checks comparison operations between elements, which in certain scenarios better expresses intent than just using `std::sortable` raw. This is the power of concepts—you can precisely express the semantics you need, rather than being locked into some ready-made standard library concept.

Then I wrote two `sort` overloads, one for random-access ranges and one for forward ranges:

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

There's a particularly important point here, and one where I'd fallen into a big trap before: **disambiguation rules for concept overloads**. In the initial draft, I thought "the compiler will automatically pick the most constrained overload," but actual testing revealed: when overload 1's constraint is `std::ranges::random_access_range` and overload 2's constraint is the custom `forward_sortable_range`, there's no subsumption relationship between the two constraints—the compiler can't determine which is more strict, so it reports an **ambiguity error**.

:::warning Original text correction: Disambiguation of concept overloads
The original text claimed that "when multiple overloads can match, the compiler will pick the most constrained one." This statement holds under specific conditions (when a subsumption relationship exists between two constraints), but it doesn't necessarily hold for custom concepts.

C++20's constraint partial ordering rules ([temp.constr.order]) require: overload A's constraint must **subsume** overload B's constraint for the compiler to choose A. `std::ranges::random_access_range` does subsume `std::ranges::forward_range` (because the former is a refinement of the latter), but it does **not** subsume the custom `forward_sortable_range` (because the latter's `requires` clause contains different atomic constraints).

Actual verification result (GCC 16.1.1, `-std=c++20`):

```text
error: call of overloaded 'my_sort(std::vector<int>&)' is ambiguous
```

Fix: add `requires (!std::ranges::random_access_range<R>)` to overload 2, explicitly excluding random-access ranges to prevent both overloads from matching simultaneously.
:::

This `!random_access_range` trick is quite practical—essentially, you're telling the compiler "only consider overload 2 if overload 1's constraints aren't satisfied." When passing `vector`, overload 2 is excluded; when passing `forward_list`, overload 1 isn't satisfied. Each matches a unique candidate, no ambiguity.

Let's run the verification:

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

Perfect. The two paths each go their own way without interfering. Notice that I gave the predicate a default value of `std::less<>`, so common cases don't need it passed every time, and when you want descending order, just pass `std::greater<>{}`. This habit of "providing sensible defaults" is something I learned from the standard library—it significantly reduces the burden on the caller.

## Concepts Aren't a New Invention—They've Always Been Here

After finishing the example above, I looked back and suddenly realized something: concepts weren't invented by C++20 at all.

Look at the history. Dennis Ritchie implicitly used concepts in early C—`int` and `float` are two concepts, except they weren't called that back then; they were called "types." When you write a function that accepts `int`, you're really saying "I need something that satisfies integer semantics." The STL had them too. When Stepanov designed the STL, he had concepts like iterator, container, and sequence in his mind, but C++ at the time had no language-level support, so these concepts only existed in documentation and designers' minds, in implicit conventions. Look even further back: the math field had abstract concepts like monad, group, and ring hundreds of years ago, and graph theory concepts can even be traced back to Euler's 1736 paper on the Seven Bridges of Königsberg.

So what is the essence of concepts? **It is the formal expression of domain knowledge.** Whether or not you use C++'s `concept` keyword, as long as you're doing generic programming, you must have concepts in your head. The only difference is: previously these concepts were implicit, hidden in designers' brains and documentation, unknown to the compiler. Now you can write them as code, and the compiler can check them for you.

I've seen a lot of so-called "generic" C++ code where template parameters are just written as `typename T` with no constraints at all, and then a comment says "T must support addition and multiplication." Isn't that just an unformalized concept? Can I just skip reading the comment? Can the compiler check it for you? Neither. So this kind of code explodes the moment you pass the wrong type, and the explosion point is miles away from the actual error.

## From "Template Programming" to "Concept-Based Generic Programming"

I increasingly feel that we shouldn't say "template programming" anymore. We should say "concept-based generic programming." What's the difference?

"Template programming" focuses attention on "how to instantiate." What's in your head is type deduction, SFINAE, and partial ordering of specializations—mechanism-level stuff. "Concept-based generic programming" focuses attention on "what I need." What's in your head is "I need a sortable forward range," and then you write that requirement as a concept, and then write a function that satisfies it. The mechanism becomes an implementation detail. See? This way, our programming mindset is correct—focus on "what is needed" rather than "how to implement it."

This mindset shift was crucial for me. Before, when I wrote template code, I'd always write the function body first, find it wouldn't compile, then patch it up with SFINAE. The whole process was "bottom-up." Now I've learned to define the concept first, think through the requirements clearly, and then write the implementation. The whole process is "top-down." Not only is it smoother to write, it's also clearer to read—when you see the concept constraints on a function signature, you immediately know what the function expects, without needing to dig into the implementation.

Moreover, concepts are often composed in layers, just like my `forward_sortable_range` above, which is composed of more basic concepts like `forward_range` and `forward_iterator`. The more and finer-grained concepts you define, the more flexible they are to reuse. It's the same principle as function decomposition—good concept design, like good function design, is about "the right level of abstraction."

From this perspective, concepts aren't a new toy C++20 conjured out of thin air. They're the puzzle piece that generic programming had always been missing. Without them, you could still do generic programming, but it was like walking a tightrope blindfolded. With them, you at least have a balance pole. Looking back, it's really not that hard, but before you figure it out, it just feels wrong.

---

# requires Expressions and Usage Patterns

When exactly should you use a `requires` expression, and when should you define a named concept? When I heard the talk mention "if you require requires in your code, you're probably doing something wrong"<RefLink :id="3" preview="Stroustrup, Concept-based Generic Programming, CppCon 2025" />, I really resonated with it—so I wasn't the only one confused by this. This really is a question with clear judgment criteria.

Today, let's thoroughly sort this out.

## Starting with the Simplest Composition

I used to think concept composition was some deep, mysterious thing, until one day I was writing a generic sorting function that needed to simultaneously require "this range can be iterated forward" and "the elements in this range can be sorted." I wrote a bunch of messy constraints at first, then realized it was really just connecting two concepts with `&&`—no fundamental difference from a logical AND operation in a regular function.

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

See? Syntactically, although you're writing `sortable_range R` in the template parameter list instead of a regular `typename R`, the concept definition itself is just a bool-returning expression. `std::ranges::forward_range<R>` is a bool, `std::sortable<...>` is also a bool, two bools combined with `&&` yield a bool. It's that simple. I'd been overcomplicating it, thinking there was some special syntactic magic involved, but there isn't.

## requires Expressions: The Underlying Bricks of Concepts

Once I understood composition, the next question was: how are the standard library concepts actually implemented? The answer is `requires` expressions.

At first, seeing the `requires` keyword appear in two places confused me—one is the `requires` clause (the kind placed after a function signature), and the other is the `requires` expression (the kind with curly braces containing a bunch of checks). These two things share the same name but have completely different responsibilities. The `requires` expression is the one that actually does the work—it checks whether a particular construct is valid.

Let's look at how to write the classic `equality_comparable` yourself:

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

There are a few details I tripped over before. First, the parameter list `const T& t, const U& u` inside the `requires` curly braces introduces some "hypothetical variables" that are only for use by the checks inside the braces. They aren't actually created. Second, the `{ t == u } -> std::convertible_to<bool>` syntax—the curly braces contain the expression to check, and the arrow is followed by the return type requirement. Note that it uses `convertible_to<bool>` rather than `same_as<bool>`, because the `==` operator doesn't necessarily return a strict `bool` type—as long as it can implicitly convert to bool, it's fine. This is explicitly specified in the C++20 standard.

## What Does "Requiring requires" Actually Mean?

The talk said "if you require requires in your code, you're probably doing something wrong." I didn't understand this at first, but then I thought about it—it's referring to situations like this:

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

Why is the first style bad? Because when you see the error message, you see a bunch of `requires` expression expansions, and you have no idea what the "semantic intent" of this constraint is. With the second style, the compiler error directly tells you "constraint `addable<T>` not satisfied," and you understand at a glance from the name. This is the value of "a concept with a meaningful name." The `requires` expression is a brick, and a concept is a house built from bricks. You should obviously live in the house, not directly on the bricks.

## Usage Patterns: Why They Change the Game

The next thing I want to discuss is, in my opinion, the most exquisite design in concepts, bar none—usage patterns.

I used to think that if I wanted to constrain a type to support the `+` operator, I needed to specify exactly how that `+` was implemented. Is it a member function `T::operator+`? Is it a free function `operator+(T, T)`? Do the parameters carry `const`? What exactly is the return type? If I had to spell all this out in a concept, it would be a nightmare, and it would place a huge burden on everyone using that concept.

But usage patterns take a completely different approach: they don't care how you implement it. They only care about "can this thing be done?"

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

:::details Original code correction note
The initial draft's definition of `can_add` used a default template argument `typename R = std::remove_cvref_t<decltype(std::declval<A>() + std::declval<B>())>` to deduce the return type. This approach has a trap: when `A + B` is ill-formed (for example, with `int + std::string`), the evaluation of the default argument fails during the template parameter substitution phase, causing a **hard compilation error** rather than the concept returning `false`.

Actual verification result (GCC 16.1.1, `-std=c++20`):

```text
error: no match for 'operator+' (operand types are 'int' and 'std::__cxx11::basic_string<char>')
```

This is a hard error—`static_assert(!can_add<int, std::string>)` simply cannot compile.

Fix: remove the return type deduction from the default template argument, and use `std::common_type_t<A, B>` as the constraint target instead. This way, when `A + B` is ill-formed, only the check inside the requires expression fails (in the "immediate context"), and the concept correctly returns `false`.
:::

This got me really excited. The `can_add` concept works for both `MyInt` (member function implementation) and `MyFloat` (free function implementation). It doesn't care about the implementation approach at all. This means interfaces become incredibly stable—you might implement `operator+` as a member function today and change it to a free function tomorrow. As long as the `a + b` expression still works, all code depending on the `can_add` concept doesn't need to change. This kind of stability was simply impossible to achieve with SFINAE and tag dispatch before.

And this checking is implicit. What does implicit mean? It means that when you instantiate a template, the compiler automatically checks it for you—you don't need to write any extra code. But if you're worried and want to confirm as early as possible that a type satisfies a concept, you can also proactively check, just like those `static_assert` I wrote above. This flexibility is great—the set of types is open; anyone can write a new type, and as long as it satisfies the usage pattern, it works. But at the same time, in places where you want to add guards, you can explicitly add them.

## Handling Mixed-Mode Arithmetic and Implicit Conversions

Usage patterns have another benefit: they naturally handle C++'s complex implicit conversion rules. For example, `int + double` works because int implicitly converts to double. Usage patterns don't care how this conversion happens; they only verify whether the `int + double` expression can ultimately compile.

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

You might ask: what if I want more precise control, disallowing implicit conversions and only allowing exact type matches? Then you can use `std::same_as` instead of `std::convertible_to`, or add more constraints inside the requires expression. Usage patterns give you the most permissive default behavior, but you can narrow it down at any time. This is so much better than the previous approach of "not checking anything by default."

## Why Concepts Must Be Part of the Language, Not an Isolated Sub-Language

Finally, one more thing I hadn't figured out before but now understand. The talk mentioned "I don't like isolated sub-languages that only exist in their own world," and that sentence woke me up.

Concepts are not a separate little world within C++. They can work with `if constexpr`, they can coexist with SFINAE (though you no longer need to hand-write SFINAE), they can work with constexpr functions, and they can work with modules. They use C++'s own language features—you can write any valid C++ expression inside a `requires` expression, and a concept definition is just an ordinary `template` + `bool` constant expression.

This means you don't need to learn a "concept-specific syntax" and then a separate "C++ syntax." What you're learning is C++ itself. Concepts elevated generic programming from "using template metaprogramming dark magic to simulate constraints" to "using the language itself to express constraints." I finally get it now. Looking back, it's really not that hard. The hard part is shaking off the inertia of all that SFINAE thinking.

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
