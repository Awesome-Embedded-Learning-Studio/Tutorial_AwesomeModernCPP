---
chapter: 1
conference: cppcon
conference_year: 2025
cpp_standard:
- 20
- 23
description: CppCon 2025 talk notes — from the pitfalls of iterator pairs to the range abstraction, then on to concept composition and requires expressions
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
title: Ranges, Iterators, and Concept Composition
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW
video_youtube: https://www.youtube.com/watch?v=VMGB75hsDQo
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/01-concept-based-generic-programming/02-range-and-concept-composition.md
  source_hash: 81d63705108f30756d72ebf9d55f4497b516113acdd8ea33ebeb4e89e954b674
  translated_at: '2026-09-26T15:20:50+00:00'
  engine: anthropic
  token_count: 7500
---
# Unchecked Pointers and the Boundaries of Generic Programming

Back when I was writing C++, I ran into a perfectly typical problem: you get a pointer and want to take its first 10 elements as a sub-view, but the code you write looks awkward no matter how you look at it. Say you have a `double*` and you want to say "I want the first 10 elements this pointer points to" — but from that code alone, neither you nor the compiler can tell how many elements the pointer actually points to, or whether 10 runs past the end. It is completely unchecked. I used to think there was simply nothing to be done — pointers are just like that. But later I realized: if your code review doesn't flag this pattern as a potential problem, then the review itself isn't strict enough.

Of course, in reality we do sometimes get raw pointers from external systems, C interfaces, and legacy code, so you can't just say "I don't touch pointers" — the capability has to exist. The key question is: once you have the pointer, can you wrap it, as quickly as possible, in something that carries bounds information and type-safety checks? That's what I had failed to understand for the longest time. I assumed "using pointers" and "type safety" were contradictory. They aren't — they are two different stages of the same job.

## First, an Annoying Little Problem

Before we get into anything deeper, I want to talk about a problem that nearly drove me to keyboard-throwing. When I was working on the type-safe number, I had to write things like `number_of<double>`, spelling out `double` every single time. Far too tedious. I'm not a fast typist, and honestly the people who designed and iterated on C and Unix probably weren't fast typists either — which is why names like `int`, `double`, and `ptr` are ridiculously short. But we have type deduction now; why keep writing it by hand?

My approach: if `number` has an initializer, take the initializer's type as `number`'s underlying type. I can write `number_of{1}` and get `number_of<int>`; `number_of{3u}` gives `number_of<unsigned>`; `number_of{1.0}` gives `number_of<double>`. Only when you truly need to — say you are initializing with an integer but what you want is `double` precision — do you have to write `number_of<double>{1}` explicitly. Day to day, this costs almost no extra typing, and not one bit of type safety is lost.

```cpp
#include <iostream>
#include <type_traits>

// Basic definition of number: holds one value, its type given by the template parameter
template<typename T>
struct number {
    T value;
    // No implicit conversion to T, so you can't accidentally use it as a plain number
    explicit operator T() const { return value; }
};

// CTAD (class template argument deduction) guide: deduce the type from the initializer
template<typename T>
number(T) -> number<T>;

// When you do need to spell the type out, this alias keeps it short
template<typename T>
using number_of = number<T>;

int main() {
    // Automatic deduction: number_of<int>
    number_of a{42};
    static_assert(std::is_same_v<decltype(a), number<int>>);

    // Automatic deduction: number_of<unsigned>
    number_of b{3u};
    static_assert(std::is_same_v<decltype(b), number<unsigned int>>);

    // Automatic deduction: number_of<double>
    number_of c{2.718};
    static_assert(std::is_same_v<decltype(c), number<double>>);

    // When you must be explicit: initializing with an integer but wanting double
    number_of d = number_of<double>{1};
    static_assert(std::is_same_v<decltype(d), number<double>>);

    std::cout << a.value << "\n";  // 42
    std::cout << b.value << "\n";  // 3
    std::cout << c.value << "\n";  // 2.718
    std::cout << d.value << "\n";  // 1

    // The line below wouldn't compile, because explicit blocks implicit conversion
    // int x = a;
    // But this is fine:
    int x = static_cast<int>(a);
    std::cout << x << "\n";  // 42
}
```

See? It compiles and runs, and every `static_assert` passes. I used to think CTAD was mere syntactic sugar, but in this scenario it genuinely makes type-safe code as smooth to write as ordinary code.

## Does This Count as Generic Programming

Having read this far, you might ask: does this thing count as generic programming? Isn't it just a template class plus some CTAD?

I hesitated over that too, but by this point, I believe it is generic programming. It uses generic-programming techniques to solve a fundamental problem that exists in C++ for historical reasons: numeric types convert to one another implicitly, which breeds all kinds of hard-to-spot bugs. You could design a new language without that historical baggage, but we don't have that option — we can only eliminate these problems inside C++ with a small library. And notice: the core logic of the type-checked `number` is only about 37 lines; a bounds-checked `span` is under 100 lines. That's shorter than the specification documents that describe the language's behavior. Solving a systemic problem with very little code — isn't that exactly what generic programming is supposed to do? (Broadly speaking, describing what a system should do without concerning yourself with the vast majority of common details — that is generic programming.)

## The Classic Problem That Really Gave Me a Headache: `std::sort` Error Messages

Alright, warm-up over. Let's talk about a problem I wrestled with for a long time and have finally started to understand.

You have certainly used `std::sort`. Its signature looks roughly like this: it takes two random access iterators `first` and `last`, plus an optional comparison function. The C++ standard document spells it out clearly: the two iterators must satisfy the requirements of LegacyRandomAccessIterator, the iterator's value type must satisfy MoveAssignable and MoveConstructible, the comparison function must satisfy StrictWeakOrdering...

But here is the problem: these requirements have never been checked directly.

They exist only in the documentation and in the heads of the committee members. When the compiler instantiates `std::sort`, it does not first verify that your iterator is a random access iterator. It just blindly instantiates, and then, somewhere deep in the template expansion, if your type doesn't meet the requirements, you get a several-hundred-line error in a completely unrelated place. You might pass in a `std::list` iterator, and the error tells you some `__move_assign` failed, or that some `__gap` variable has a problem. When you see that error message, you are completely lost.

### First, Reproduce the Error That Broke Me

Environment first: I'm using GCC 16.1.1 with `-std=c++20`, on Arch Linux WSL. The build command is the plainest possible: `g++ -std=c++20 -Wall -Wextra`.

Start with a piece of code that looks perfectly fine:

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

Guess what? The compilation just explodes. Here is the relatively "readable" fragment of the error:

```text
/usr/include/c++/16/bits/stl_algo.h: In instantiation of 'void std::sort(_RandomAccessIterator, _RandomAccessIterator, _Compare) [with _RandomAccessIterator = std::_List_iterator<int>; _Compare = __gnu_cxx::__ops::_Iter_less_iter]':
...
error: no match for 'operator-' (operand types are: 'std::_List_iterator<int>' and 'std::_List_iterator<int>')
```

When I saw that error, I knew the iterator type was wrong, because I had learned that a `list` is a doubly-linked list and doesn't support random access. But what if you are a beginner with less than half a year of experience? You see `no match for 'operator-'` and start wondering: did I forget to overload some operator? Did I miss an include? The error message tells you nothing about the real problem — **you called an algorithm that requires random access with an iterator that doesn't support it**.

I used to think "template error messages are ugly" was a topic people complained about far too much — read them a few times and you'll get used to them. But this time I thought about it seriously, and no, that's not it. The problem is not that the error is long; it's that the message reports the **symptom** (no `operator-` found) rather than the **cause** (the iterator category doesn't meet the requirements). For anyone not fluent in template metaprogramming, the gap between those two is a chasm.

## What About Now

Now we have concepts.

Concepts were introduced in C++20, but their intellectual roots trace back to Alex Stepanov (the father of the STL) and his original vision of generic programming<RefLink :id="4" preview="Stepanov & Lee, The Standard Template Library, 1995" />. From the very beginning he believed that generic algorithms should state explicit, checkable requirements on their parameters. This is not an optional cherry on top — it is the infrastructure of generic programming. It just took C++ more than thirty years to build that infrastructure.

Looking back on it now, it feels like a room that was always missing a wall. Everyone got used to the wind blowing in, even learned how to live in the draft, until one day somebody finally bricked up the wall — and only then did you realize: oh, so it can be this comfortable.

Next I want to write some code and see how concepts actually change the way we write generic code. Not the textbook `template<std::integral T>` example, but usage that solves real problems. We'll start with the simplest scenario: write our own constraint for `sort`, then deliberately pass the wrong type, and see just how good the error message can get.

```cpp
#include <iostream>
#include <vector>
#include <list>
#include <concepts>
#include <algorithm>
#include <iterator>

// First, define our own concept: a random access iterator range
// Note: it composes standard library concepts — no need to write one from scratch
template<typename Iter>
concept RandomAccessRange =
    std::random_access_iterator<Iter> &&
    std::sentinel_for<Iter, Iter>;

// A constrained sort wrapper
template<RandomAccessRange Iter, typename Comp = std::less<>>
    requires std::indirect_strict_weak_order<Comp, Iter>
void safe_sort(Iter first, Iter last, Comp comp = {}) {
    std::sort(first, last, comp);
}

int main() {
    // Correct usage: a vector's iterators are random access iterators
    std::vector<int> v = {5, 3, 1, 4, 2};
    safe_sort(v.begin(), v.end());
    for (int x : v) std::cout << x << " ";
    std::cout << "\n";
    // Output: 1 2 3 4 5

    // Wrong usage: a list's iterators are not random access iterators
    // Uncomment the two lines below for a very clear error message
    // std::list<int> lst = {5, 3, 1, 4, 2};
    // safe_sort(lst.begin(), lst.end());
}
```

Go ahead and uncomment those last two lines. On my machine (GCC 16.1.1, `-std=c++20`), the error message tells you directly: constraint not satisfied, `std::list<int>::iterator` does not satisfy `random_access_iterator`. No 400 lines of template expansion, no `__gap`, no `__move_assign` — just one sentence: your iterator type is wrong.

Seeing that error message felt glorious. I had been tortured by `std::sort`'s error output so many times, and it turns out the fix was this simple — not adding tools, not running some error-beautifying script, just writing the constraint into the function signature. The compiler always had the ability to check; there was simply no syntax for expressing the constraint before.

### Stopping Errors at the Door with Concepts

In the C++20 standard library, the notions that used to exist only as prose in the standard document have become real code entities. Among them are `std::random_access_iterator` and `std::sortable`.

I used to think concepts were just syntactic sugar for constraining templates, and that `enable_if` could do the same job. But after wrestling with this example, I finally understood: the real value of concepts is not "whether it compiles," but **telling you why it failed when compilation fails**.

Here is a concept-constrained sort function I wrote:

```cpp
#include <concepts>
#include <iterator>
#include <functional>
#include <vector>
#include <iostream>
#include <list>

// My own sort wrapper, with a concept spelling out the requirements
template<std::random_access_iterator It, typename Comp = std::less<>>
    requires std::sortable<It, Comp>
void my_sort(It first, It last, Comp comp = {}) {
    std::sort(first, last, comp);
}

int main() {
    // This compiles just fine
    std::vector<int> vec = {5, 3, 1, 4, 2};
    my_sort(vec.begin(), vec.end());
    for (int x : vec) std::cout << x << " ";
    std::cout << "\n";

    // This one gets stopped at compile time
    std::list<int> lst = {5, 3, 1, 4, 2};
    my_sort(lst.begin(), lst.end());  // Compile error!
    return 0;
}
```

Now, when the `list` call compiles, the error reads:

```text
error: constraint not satisfied
required: 'std::random_access_iterator<std::_List_iterator<int>>'
note: no known conversion from 'std::bidirectional_iterator_tag' to 'std::random_access_iterator_tag'
```

**Now that is plain human language, my friends!** It tells you the `list` iterator is a bidirectional iterator, that you required a random access iterator, and that the types don't match. No digging through the source of `stl_algo.h`, no understanding the substitution-failure machinery of SFINAE — the error message points straight at the constraint itself.

I went and checked what `std::sortable` actually requires. Its definition chain goes roughly: `std::sortable<I>` requires `std::permutable<I>`, and `std::permutable<I>` requires `std::forward_iterator<I>` — note, only a **forward iterator** is required here, not a random access iterator. It additionally requires that the iterator's value type satisfy `indirect_strict_weak_order` (that is, be comparable with the given predicate), and that `swap` be possible. These things used to be buried in the prose of the standard, read only by library implementers. Now they are queryable, referenceable code entities — you can even jump to the definition in your IDE.

:::warning Correction to the Original Text
An early draft of this article stated `std::sortable`'s iterator requirement as `random_access_iterator`. That is wrong.

From the authoritative source (cppreference):
> `template<class I, class Comp = ranges::less, class Proj = std::identity> concept sortable = std::permutable<I> && std::indirect_strict_weak_order<Comp, std::projected<I, Proj>>;`
>
> where `permutable<I>` requires `forward_iterator<I>`.
> — cppreference, std::sortable<RefLink :id="1" preview="cppreference, std::sortable" />

Actual verification (GCC 16.1.1, `-std=c++20`):

```cpp
static_assert(std::sortable<std::forward_list<int>::iterator>);  // passes!
static_assert(std::sortable<std::list<int>::iterator>);           // passes!
static_assert(std::sortable<std::vector<int>::iterator>);         // passes!
```

A `forward_list` has only forward iterators, yet it still satisfies `std::sortable`.

The distinction to keep straight: the **`std::sort` algorithm** requires random access iterators, but the **concept `std::sortable`** only requires forward iterators. The former is the implementation constraint of the algorithm; the latter is the concept's minimal requirement.
:::

So in retrospect: concepts are not syntactic sugar for "making template errors a bit prettier." They are the puzzle piece generic programming had been missing for more than thirty years. The so-called generic code we wrote before was really generic code without declared constraints — the constraints existed, but only in documentation and in programmers' heads, invisible to the compiler. Now concepts make constraints part of the code, and the compiler can finally do what it should have been able to do all along.

---

# Iterator Pitfalls and the Range Solution

Honestly, during my first two years of learning C++, I had long since gotten used to how standard library algorithms are called — pass a begin, pass an end, maybe pass a comparison function; that three-piece toolkit goes everywhere. It wasn't until a few days ago, when my itchy fingers made me call `std::sort` on a `std::list` and I then stared at that blob of template error output on the screen for a solid twenty minutes, that I truly understood what C++20's concepts and ranges are actually solving. Today I'm writing down this whole journey "from pain to epiphany," start to finish.

## But Iterator Pairs Hide Bigger Pitfalls

Prettier error messages — am I satisfied now? No. Because I thought of something even scarier.

I've seen code like this in real projects — someone passed `begin` and `end` in reverse:

```cpp
std::vector<int> vec = {1, 2, 3, 4, 5};
std::sort(vec.end(), vec.begin());  // Note: reversed!
```

Do you know what happens then? It doesn't crash right away. Inside `std::sort`, `last - first` gets computed and yields a huge number (pointer subtraction: `end` sits after `begin`, so the result should have been positive, but reversed it's negative, which turns into an enormous unsigned value), and then the algorithm starts frantically reading and writing memory out of bounds. It might run for a long time before segfaulting, or it might "quietly" corrupt your heap and crash somewhere completely unrelated. I debugged one of these once; it ate an entire afternoon.

And there's an even more absurd case — two iterators from two different containers:

```cpp
std::vector<int> a = {1, 2, 3};
std::vector<int> b = {4, 5, 6};
std::sort(a.begin(), b.end());  // Iterators from two different containers!
```

In the C++ standard this is undefined behavior, yet the compiler won't stop you at all. From the type system's point of view, `a.begin()` and `b.end()` have exactly the same type — both are `std::vector<int>::iterator`. The compiler has no way to know whether they came from the same container.

Adding concept constraints to the iterators cannot solve these problems. Because the problem is not "what type the iterators are," but whether "the relationship between this pair of iterators" is valid.

## Why Ranges Are the Right Answer

C++20 did not introduce ranges to show off. It did it to fix, at the root, this design flaw of "iterator pairs."

A range inherently represents "a contiguous run of elements from one container." You can't end up with a begin and an end from different containers, and getting them backwards is much harder to do (in theory you can construct a range whose sentinel doesn't match, but not in normal usage).

And honestly, writing `xxx.begin(), xxx.end()` at every algorithm call is just too verbose. Plus there was that whole `A.begin(), B.end()` incident before... Yeah — ranges, I like you!

Look at how clean the range version is:

```cpp
#include <ranges>
#include <algorithm>
#include <vector>
#include <string>
#include <iostream>

// My own range-based sort wrapper
template<std::ranges::random_access_range R,
         typename Comp = std::ranges::less>
    requires std::sortable<std::ranges::iterator_t<R>, Comp>
void my_sort(R&& r, Comp comp = {}) {
    std::ranges::sort(std::forward<R>(r), comp);
}

int main() {
    // vector of doubles, ascending
    std::vector<double> vd = {3.14, 1.41, 2.72, 0.58};
    my_sort(vd);
    for (double x : vd) std::cout << x << " ";
    std::cout << "\n";

    // vector of strings, descending
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

See? At the call site you just pass one range object. No `begin()`, no `end()`, no worrying about whether two iterators match. And the constraint you write is `std::ranges::random_access_range`, which directly expresses "this thing must support random access," rather than "this thing's iterator must satisfy such-and-such." That is one level up, semantically.

If you try to pass in a `list`:

```cpp
std::list<int> lst = {5, 3, 1, 4, 2};
my_sort(lst);  // Compile error
```

The error will tell you outright that `std::list<int>` does not satisfy `random_access_range`. Clean and decisive.

I used to think ranges were just syntactic sugar, and that the `views::transform` / `views::filter` pipeline style looked cool but unnecessary. Looking back now, the core value of ranges is actually **replacing the error-prone abstraction of "a pair of iterators" with the hard-to-misuse abstraction of "a range."** The pipeline notation is just a bonus that came along for the ride.

At this point, the evolution from iterators to ranges has finally, completely clicked for me. But the story isn't over — in the example above I sorted a `vector<string>` in descending order using `std::ranges::greater{}`. That looks fine, but what if you have more refined needs for string sorting? Sorting by length, or lexicographically ignoring case? That's where predicate customization comes in. Let's keep going.

---

# Concept Composition and Overload Resolution

My understanding of concepts stayed at the level of "it's just syntactic sugar for SFINAE" for a long time. I figured it merely made compile errors a bit prettier and the writing a bit cleaner, but underneath you were still doing the same old template thing. Right? If that were right, I'm afraid this note wouldn't exist.

## From `sort` to `forward_sortable_range`

It started because I needed to sort a `std::forward_list`. I had this habit: write a general-purpose `sort` function with no constraints at all — lay out the template parameters and stuff whatever type in. And guess what happened? The compiler naturally raised no error, but at run time it blew up, because `std::sort` needs random access iterators underneath, while `forward_list` only has forward iterators. This kind of error is completely uncatchable at compile time; it only surfaces at run time, and tracking it down is absolutely maddening.

So, can we stop this kind of error at the type-system level? Not by relying on documentation that says "please don't use this function on a list" (mind you, everyone is busy these days — nobody has time to sit with you reading documentation, unless the compiler has just given you a beating!), but by making the code itself forbid you from doing it. That is the core problem concepts solve — not "prettier error messages," but "invalid usage simply cannot be written."

I set out and wrote a constraint for forward-sortable ranges, then provided overloads of `sort` based on it. First, here is the concept I defined:

```cpp
#include <concepts>
#include <ranges>
#include <forward_list>
#include <vector>
#include <algorithm>
#include <iostream>
#include <iterator>

// First, define a concept for a "forward sortable range"
// It says: the range must be a forward_range, and its elements must be comparable with the given predicate
template<typename R, typename C = std::less<>>
concept forward_sortable_range =
    std::ranges::forward_range<R> &&
    requires(R& r, C comp) {
        // We need to be able to get a forward iterator
        { std::begin(r) } -> std::forward_iterator;
        // Elements must be comparable with the predicate
        { *std::begin(r) < *std::begin(r) } -> std::convertible_to<bool>;
    };
```

You might ask: why not just use `std::sortable`? Good question. `std::sortable` does exist in the standard library, and it actually only requires forward iterators<RefLink :id="1" preview="cppreference, std::sortable" /> — yes, `forward_list`'s iterator satisfies `std::sortable` too. What I wanted to express here, though, is the semantic level of "this range can be sorted, but not necessarily by way of random access," so I still chose to define a more explicit constraint myself. Moreover, `forward_sortable_range` additionally checks the comparison between elements, which in some scenarios expresses intent better than using raw `std::sortable`. That is the power of concepts — you can state exactly the semantics you need, instead of being locked into some ready-made standard library concept.

Then I wrote two `sort` overloads, one for random access ranges and one for forward ranges:

```cpp
// Overload 1: for random access ranges (vector, deque, etc.)
// The stricter constraint; the compiler prefers this one
template<std::ranges::random_access_range R, typename C = std::less<>>
    requires std::sortable<std::ranges::iterator_t<R>, C>
void my_sort(R& r, C comp = C{}) {
    std::ranges::sort(r, comp);
    std::cout << "  [走随机访问路径]\n";
}

// Overload 2: for forward sortable ranges (forward_list, etc.)
// Key point: explicitly exclude random access ranges with !random_access_range to avoid ambiguity
template<forward_sortable_range R, typename C = std::less<>>
    requires (!std::ranges::random_access_range<R>)
void my_sort(R& r, C comp = C{}) {
    // Simple implementation: copy into a vector, sort, copy back
    // Production code could use a more efficient list-sorting algorithm; this is just a demo
    std::vector<std::ranges::range_value_t<R>> tmp(
        std::begin(r), std::end(r)
    );
    std::ranges::sort(tmp, comp);
    std::ranges::copy(tmp, std::begin(r));
    std::cout << "  [走前向迭代器路径：复制-排序-回写]\n";
}
```

There is a particularly important point here, one I stepped into myself before: **the disambiguation rules for concept overloads**. In my first draft I assumed "the compiler automatically picks the overload with the strictest constraints." Actual testing showed: when overload 1's constraint is `std::ranges::random_access_range` and overload 2's is the custom `forward_sortable_range`, there is no subsumption (containment) relation between the two constraints — the compiler cannot tell which is stricter, so it reports an **ambiguity error**.

:::warning Correction to the Original Text: Concept Overload Disambiguation
The original text claimed "when multiple overloads all match, the compiler picks the one with the strictest constraints." That statement holds under specific conditions (when a subsumption relation exists between the two constraints), but not necessarily for custom concepts.

C++20's constraint ordering rules ([temp.constr.order]) require: overload A's constraints must **subsume** overload B's constraints before the compiler will choose A. `std::ranges::random_access_range` does subsume `std::ranges::forward_range` (the former is a refinement of the latter), but it does **not** subsume the custom `forward_sortable_range` (whose `requires` clause contains different atomic constraints).

Actual verification (GCC 16.1.1, `-std=c++20`):

```text
error: call of overloaded 'my_sort(std::vector<int>&)' is ambiguous
```

The fix: add `requires (!std::ranges::random_access_range<R>)` to overload 2, explicitly excluding random access ranges so the two overloads can never both match.
:::

This `!random_access_range` trick is very practical — it essentially tells the compiler "only consider overload 2 under the condition that overload 1 fails." Pass a `vector` and overload 2 is excluded; pass a `forward_list` and overload 1 doesn't hold; each call matches a single candidate, with no ambiguity.

Let's run it to verify:

```cpp
int main() {
    // Test 1: vector takes the random access path
    std::vector<int> v = {5, 3, 1, 4, 2};
    std::cout << "排序 vector: ";
    my_sort(v);
    for (int x : v) std::cout << x << ' ';
    std::cout << '\n';

    // Test 2: forward_list takes the forward iterator path
    std::forward_list<int> fl = {5, 3, 1, 4, 2};
    std::cout << "排序 forward_list: ";
    my_sort(fl);
    for (int x : fl) std::cout << x << ' ';
    std::cout << '\n';

    // Test 3: descending order with greater
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

Perfect — the two paths each go their own way and never interfere. Notice that I gave the predicate a default of `std::less<>`, so for the common case you don't have to pass anything, and when you want descending order you just pass `std::greater<>{}`. I picked up this "provide a sensible default" habit from the standard library; it greatly reduces the burden on callers.

## Concepts Are Not a New Invention — They Have Always Been Here

After finishing the example above, I looked back and suddenly realized something: concepts were not invented by C++20 at all.

Look back into history. Dennis Ritchie implicitly used concepts in early C — `int` and `float` are two concepts; they just weren't called that back then, they were called "types." When you write a function that takes an `int`, you are really saying "I need something that satisfies integer semantics." The STL had them too: when Stepanov designed the STL, he had iterator, container, and sequence in his head — C++ simply had no language-level support at the time, so these notions existed only in documentation and in the designers' minds, as implicit conventions. Look even further back: mathematics has had abstract concepts like monad, group, and ring for hundreds of years, and notions from graph theory can even be traced back to Euler's 1736 paper on the Seven Bridges of Königsberg.

So what is the essence of concepts? **They are a formal expression of domain knowledge.** Whether or not you use C++'s `concept` keyword, if you do generic programming, you must have concepts in your head. The only difference is: before, these notions were implicit — hidden in designers' heads and in documentation, unknown to the compiler. Now you can write them down as code, and the compiler can check them for you.

I have seen plenty of so-called "generic" C++ code where the template parameter is just `typename T` with no constraints whatsoever, followed by a comment saying "T must support addition and multiplication." Isn't that a concept without formalization? I wrote it in a comment — can I skip reading it? Can the compiler check it for me? Neither. So this kind of code explodes the moment you pass the wrong type, and it explodes somewhere a hundred thousand miles from the actual mistake.

## From "Template Programming" to "Concept-Based Generic Programming"

I increasingly feel that we should stop saying "template programming" and call it "concept-based generic programming" instead. Where does the difference between the two lie?

"Template programming" puts the attention on "how to instantiate" — your head is full of mechanism-level things: type deduction, SFINAE, the partial ordering of specializations. "Concept-based generic programming" puts the attention on "what I need" — your head holds "I need a sortable forward range," you write that requirement down as a concept, and then you write functions that satisfy the concept. The mechanism becomes an implementation detail. See? Now our thinking as programmers is on the right track — focus on "what is needed," not on "how it is implemented."

This shift in thinking was decisive for me. Before, when I wrote template code, I always wrote the function body first, discovered it didn't compile, then patched it up with SFINAE — the whole process was bottom-up. Now I have learned to define the concept first, get the requirements straight, and then write the implementation — the whole process is top-down. It doesn't just write more smoothly; it reads more clearly too: seeing the concept constraint on a function signature, you immediately know what the function expects, without having to dig into the implementation.

Moreover, concepts tend to compose in layers — like my `forward_sortable_range` above, which is composed from more basic concepts such as `forward_range` and `forward_iterator`. The more concepts you define and the finer-grained they are, the more flexibly they can be reused. It's the same principle as factoring functions — good concept design, like good function design, is about "the right level of abstraction."

Seen this way, concepts are not a new toy that C++20 conjured out of thin air; they are the puzzle piece generic programming was always missing. Without it, you can still do generic programming, but it's like walking a tightrope blindfolded; with it, you at least have a balancing pole. Looking back, it really wasn't that hard — but before it clicked, it just felt awkward.

---

# `requires` Expressions and Use Patterns

When exactly should you use a `requires` expression, and when should you define a named concept? When I heard the line in the talk — "if you require requires in your code, you might be doing something wrong"<RefLink :id="3" preview="Stroustrup, Concept-based Generic Programming, CppCon 2025" /> — it resonated strongly. So I wasn't the only one confused by this; it really is a question with a clear criterion for deciding.

Today, let's straighten this out once and for all.

## Start from the Simplest Composition

I used to think concept composition was some profound thing, until one day I wrote a generic sorting function that needed to require both "this range can be traversed forward" and "the elements of this range can be sorted." I wrote a pile of messy constraints before realizing that it's just two concepts joined with `&&` — no essential difference from writing a logical AND in ordinary code.

```cpp
#include <concepts>
#include <ranges>
#include <vector>
#include <algorithm>
#include <iostream>

// A concept of my own: a sortable range
// It is essentially the "AND" of forward_range and sortable
template<typename R>
concept sortable_range = std::ranges::forward_range<R> && std::sortable<std::ranges::iterator_t<R>>;

// Constrain the function template with this composed concept
template<sortable_range R>
void my_sort(R&& r) {
    std::ranges::sort(std::forward<R>(r));
}

int main() {
    std::vector<int> v{3, 1, 4, 1, 5, 9, 2, 6};
    my_sort(v);  // Compiles: vector<int> satisfies both forward_range and sortable

    // my_sort("hello");  // Compile error: a string does not satisfy sortable
    // The error will tell you plainly: constraint 'sortable_range<R>' not satisfied

    for (int x : v) std::cout << x << ' ';
    // Output: 1 1 2 3 4 5 6 9
}
```

See: syntactically you are writing `sortable_range R` in the template parameter list instead of plain `typename R`, but the concept's own definition is just an expression that returns bool. `std::ranges::forward_range<R>` is a bool, `std::sortable<...>` is a bool, two bools joined with `&&` give a bool. That's all there is to it. I had been over-thinking it, assuming there was some special syntactic magic inside. There isn't.

## `requires` Expressions: The Building Bricks of Concepts

Once composition clicks, the next question is: how are the concepts in the standard library actually implemented? The answer is the `requires` expression.

At first I was baffled seeing the `requires` keyword show up in two places — the `requires` clause (the kind placed after a function signature) and the `requires` expression (the kind with a pile of checks inside curly braces). The two share a name but have completely different jobs. The `requires` expression is the one doing the real work: it checks whether some construct is valid.

Let's look at how the classic `equality_comparable` should be written by hand:

```cpp
#include <concepts>
#include <type_traits>

// A hand-rolled, simplified equality_comparable
// Checks whether equality and inequality comparisons are possible between T and U
template<typename T, typename U>
concept my_equality_comparable =
    requires(const T& t, const U& u) {
        // Each line below is a check of one "use pattern"
        // The compiler tries to compile these expressions; if they all compile, the requirement holds
        { t == u } -> std::convertible_to<bool>;
        { u == t } -> std::convertible_to<bool>;
        { t != u } -> std::convertible_to<bool>;
        { u != t } -> std::convertible_to<bool>;
    };

// Verify
static_assert(my_equality_comparable<int, double>);   // int and double compare fine
static_assert(my_equality_comparable<int, int>);      // same type, of course
static_assert(!my_equality_comparable<int, std::nullptr_t>);  // int and nullptr don't compare
```

There are a few details here I got burned by before. First, the parameter list inside the `requires` braces — `const T& t, const U& u` — introduces "hypothetical variables" used only by the checks inside the braces; they are never actually created. Second, the syntax `{ t == u } -> std::convertible_to<bool>`: inside the braces is the expression to check, and after the arrow is the requirement on its return type. Note that this uses `convertible_to<bool>` rather than `same_as<bool>`, because the `==` operator doesn't necessarily return exactly `bool` — being implicitly convertible to bool is enough. This is explicitly specified in the C++20 standard.

## What "Requiring requires" Actually Means

The talk said "if you require requires in your code, you might be doing something wrong." At first I didn't understand that sentence; after thinking it over, I realized it refers to this kind of situation:

```cpp
// Bad example: a requires expression written directly in the function's constraints
template<typename T>
    requires requires(T t) { t + t; }
auto add_stuff(T a, T b) {
    return a + b;
}

// The right way: give it a name and define a concept
template<typename T>
concept addable = requires(T t) { t + t; };

template<addable T>
auto add_stuff(T a, T b) {
    return a + b;
}
```

Why is the first version bad? Because when the error comes, what you see is an expanded pile of `requires` expression, and you have no idea what the constraint's semantic intent is. With the second version, the compiler's error tells you directly "constraint `addable<T>` not satisfied," and you understand at a glance from the name. That is the value of "concepts with names that carry clear meaning": the `requires` expression is the brick, the concept is the house built from the bricks — and of course you should live in the house, not directly on the bricks.

## Use Patterns: Why They Change the Game

The next thing I want to talk about is, in my opinion, the single most exquisite design in concepts — use patterns.

I used to think that if I wanted to constrain a type to support `+`, I had to specify exactly how that `+` is implemented. Is it the member function `T::operator+`? The free function `operator+(T, T)`? Are the parameters `const` or not? What exactly is the return type? If I had to write all of that into the concept, it would be a nightmare, and it would place an enormous burden on everyone using the concept.

But use patterns take a completely different approach: they don't care how you implement it — they care only about one question: "can this thing be done?"

```cpp
#include <concepts>
#include <string>

// I only require that the expression A + B compiles and that the result converts to some common type
// Whether A + B is implemented as a member function or a free function, I don't care at all
template<typename A, typename B>
concept can_add = requires(A a, B b) {
    { a + b } -> std::convertible_to<std::common_type_t<A, B>>;
};

// Now let's see the power of use patterns

// Case 1: addition of built-in types
static_assert(can_add<int, int>);

// Case 2: mixed-mode arithmetic, int + double
static_assert(can_add<int, double>);

// Case 3: std::string addition (implemented via a free operator+)
static_assert(can_add<std::string, std::string>);

// Case 4: a user-defined type implementing operator+ as a member function
class MyInt {
    int val;
public:
    MyInt(int v) : val(v) {}
    MyInt operator+(const MyInt& other) const { return MyInt(val + other.val); }
};
static_assert(can_add<MyInt, MyInt>);

// Case 5: another user-defined type implementing operator+ as a free function
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

// Case 6: int and std::string cannot be added
static_assert(!can_add<int, std::string>);
```

:::details Note on a Code Correction in the Original
The first draft defined `can_add` with a default template argument `typename R = std::remove_cvref_t<decltype(std::declval<A>() + std::declval<B>())>` to deduce the return type. That formulation has a trap: when `A + B` is ill-formed (for example, `int + std::string`), evaluating the default argument fails already during template argument substitution, producing a **hard compile error** instead of the concept returning `false`.

Actual verification (GCC 16.1.1, `-std=c++20`):

```text
error: no match for 'operator+' (operand types are 'int' and 'std::__cxx11::basic_string<char>')
```

This is a hard error — `static_assert(!can_add<int, std::string>)` doesn't even compile.

The fix: drop the return-type deduction in the default template argument and use `std::common_type_t<A, B>` as the constraint target instead. Now when `A + B` is ill-formed, only the check inside the requires expression fails (in the "immediate context"), and the concept correctly returns `false`.
:::

I got genuinely excited at this point. The `can_add` concept works for both `MyInt` (member-function implementation) and `MyFloat` (free-function implementation); it doesn't care the least bit about the implementation style. This means the interface becomes extremely stable — you implement `operator+` as a member function today, change it to a free function tomorrow, and as long as the expression `a + b` still works, none of the code depending on the `can_add` concept needs to change. That kind of stability was simply unattainable with SFINAE and tag dispatch.

And the check is implicit. What does implicit mean? When you instantiate the template, the compiler checks it for you automatically — you don't need to write any extra code at all. But if you are cautious and want to confirm early that some type satisfies some concept, you can also check proactively, like the `static_assert`s I wrote above. This flexibility is wonderful — the set of types is open: anyone can write a new type, and as long as it satisfies the use pattern, it works; yet at the same time, wherever you want a guardrail, you can add one explicitly.

## Mixed-Mode Arithmetic and Implicit Conversions

Use patterns have one more benefit: they naturally handle C++'s complicated implicit conversion rules. For example, `int + double` works because int implicitly converts to double. The use pattern doesn't care how that conversion happens; it only verifies that the expression `int + double` can ultimately compile.

```cpp
#include <concepts>

template<typename A, typename B>
concept can_compare = requires(A a, B b) {
    { a == b } -> std::convertible_to<bool>;
};

// int and double compare fine, because int implicitly converts to double
static_assert(can_compare<int, double>);

// int and long compare fine
static_assert(can_compare<int, long>);

// int and std::string don't: there is no implicit conversion from string to int
static_assert(!can_compare<int, std::string>);
```

You might ask: what if I want more precise control — no implicit conversions allowed, only exact type matches? Then you can use `std::same_as` in place of `std::convertible_to`, or add more constraints inside the requires expression. Use patterns give you the loosest default behavior, and you can narrow them at any time. That is vastly better than the old "check nothing by default" approach.

## Why Concepts Must Be Part of the Language, Not an Isolated Sub-Language

Finally, one more thing I never quite got before and now do. The talk mentioned "I dislike isolated sub-languages that can only exist as their own little sect" — that line woke me up.

Concepts are not a separate little world inside C++. They work together with `if constexpr`, coexist with SFINAE (even though you no longer need to hand-write SFINAE), work with constexpr functions, and work with modules. They use C++'s own language features — a `requires` expression can contain any valid C++ expression, and a concept's definition is just an ordinary `template` plus a `bool` constant expression.

This means you don't have to learn one "concept-specific syntax" and then another separate "C++ syntax" — what you learn is C++ itself. Concepts take generic programming from "simulating constraints with template-metaprogramming black magic" to "expressing constraints in the language itself." At this point it has finally clicked; looking back, it really isn't that hard. The hard part was shaking off the old SFINAE habits of thought.

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
