---
chapter: 10
cpp_standard:
- 11
- 14
- 17
- 20
description: "You have been using vector and string for two chapters already; this article formally introduces the family they come from, the STL: containers store the data, iterators serve as the interface, algorithms do the work, and at the end we take a quick look at the two different reactions of at() and operator[] when an access goes out of bounds"
difficulty: beginner
order: 0
platform: host
prerequisites:
- Template Specialization Basics
reading_time_minutes: 10
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: 'What Is the STL: Containers, Algorithms, and the Iterators In Between'
translation:
  source: documents/vol1-fundamentals/ch10/00-what-is-stl.md
  source_hash: ebc95943b7f36b674635b45e66e4a17f88ef7683c3709e6a76fa7c569b2cafbe
  translated_at: '2026-09-27T03:58:41+00:00'
  engine: anthropic
  token_count: 5600
---
# What Is the STL: Containers, Algorithms, and the Iterators In Between

At the end of *Why We Need Templates* we left a remark: the STL in Chapter 10 is built entirely on that chapter's groundwork. We have now arrived. And you did not come empty-handed: back in the OOP practice, `vector<unique_ptr<Shape>>` loaded a whole chapter of shapes onto the canvas; the hand-written `Stack` in the templates chapter has a `vector` cushioning it underneath; and `std::string` has been tagging along since the first volume. All of these share one origin—they are all members of the STL. We see the name every day, yet the formal introduction has been owed all along; this article pays that debt: which batch of things the STL is, and how the roles inside it cooperate.

## Which Batch of Things "STL" Refers To

STL is short for Standard Template Library. *Why We Need Templates* covered the history: a library of containers and algorithms written with templates by Stepanov and Meng Lee at Hewlett-Packard Labs, voted into the draft standard in July 1994 and landing with the first official standard in 1998. One detail about the name itself is worth adding here: strictly speaking, STL refers to the batch absorbed in 1994—that is, those families of templates for containers, iterators, algorithms, and function objects. `std::string` and `iostream` also live in the standard library, but they do not count as original members of that batch. In casual speech people often call the entire C++ standard library "the STL"; it is enough that you know this distinction exists—failing to keep it straight will not hamper your coding in the least.

What the STL hands us boils down to three roles: **containers** handle storage (`vector`, `map`, `set`, and friends), **algorithms** handle the computing (`sort`, `find`, `count`, and friends), and in between, **iterators** connect the two. There are also two families of accessories—function objects and adapters—which we will meet in the algorithms article. Of the three, the one with the lowest profile is the iterator, because it always hides inside `begin()` and `end()` playing the supporting role—so let's just run some code; the moment it steps on stage, its job becomes clear.

## Run It Once and Watch the Three Roles Cooperate

Pick any scenario you like: a week's log of daily high temperatures, and you want to see them sorted and count the days at 30 degrees or above. Out of old habit, you might spin up an array and hand-write two rounds of loops. Now swap in the STL way of writing it:

```cpp
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

int main() {
    std::vector<int> temps = {28, 31, 26, 33, 29, 30, 7};  // one day's entry was a slip of the hand

    std::sort(temps.begin(), temps.end());    // algorithm: sorting

    std::cout << "sorted: ";
    for (int t : temps) {
        std::cout << t << ' ';
    }
    std::cout << "\n";

    int hot = std::count_if(temps.begin(), temps.end(),
                            [](int t) { return t >= 30; });    // algorithm: counting with a condition
    std::cout << "days >= 30: " << hot << "\n";

    return 0;
}
```

Output:

```text
sorted: 7 26 28 29 30 31 33
days >= 30: 3
```

Three key calls—let's take them one line at a time.

`std::vector<int> temps = {...}` is the container at work: a dynamic array that manages memory automatically—stuff in as much as you like, and it takes care of all of it. This part you already know well.

`std::sort(temps.begin(), temps.end())` is the algorithm at work, but please look closely at the arguments: sort does not receive the container `temps`; it receives two iterators. `begin()` points at the first element, and `end()` points one slot past the last element (yes, one slot past it—that is a placeholder sentinel, standing for no element at all). You can think of an iterator as a generalization of a pointer: it points at some position in the container, can be dereferenced to grab the element, and can be `++`-ed to move to the next position. Bare pointers could already do these things for arrays; the STL standardized this set of moves into "iterators," so that every kind of container hands out a `begin()`/`end()` pair written exactly the same way.

The payoff of this design arrives at once: algorithms do not need to know containers at all. Hand `sort` a range and it sorts that range; whether the data lives in a `vector` or a `deque`, whether it holds `int` or `std::string`—it could not care less. If you do not believe it, let's point the very same `sort` at strings:

```cpp
std::vector<std::string> cities = {"Beijing", "Chengdu", "Anshan", "Dali"};
std::sort(cities.begin(), cities.end());

std::cout << "cities: ";
for (const auto& c : cities) {
    std::cout << c << ' ';
}
std::cout << "\n";
```

Output:

```text
cities: Anshan Beijing Chengdu Dali
```

Sorted in dictionary order. You may find that unremarkable, but say it once in the words of *Why We Need Templates* and it becomes clear: `sort` makes exactly one demand of the element type—"can be compared"—`int` satisfies it, and so does `std::string`; keep the requirement and strip away the type, and one single algorithm serves every type that meets the requirement. The templates chapter was about how such a thing becomes possible; the STL is the official ready-made version—the standard's authors having written that entire big batch of commonly used ones for us.

The `[](...){...}` we handed to `std::count_if` is a lambda expression: it amounts to writing the judgment "what counts as 30 degrees or above" as a small piece of code on the spot and passing it to the algorithm. It is the protagonist of the algorithms article; here we just use it as a black box: given a range and a condition, it returns how many elements satisfy the condition.

## at() and operator[]: Two Reactions to Going Out of Bounds

While the example is still warm in our hands, there is one more behavior related to "things going wrong" that is worth knowing right now. Let's deliberately make one out-of-bounds access, with `at()`:

```cpp
#include <stdexcept>

try {
    std::cout << temps.at(100) << "\n";    // temps has only 7 elements
} catch (const std::out_of_range& e) {
    std::cout << "caught: " << e.what() << "\n";
}
```

Output:

```text
caught: vector::_M_range_check: __n (which is 100) >= this->size() (which is 7)
```

The program did not crash; it printed one explanatory line and kept running. What happened here is called "throwing an exception": when `at()` goes out of bounds, it throws an object of a type called `std::out_of_range`, and the `try`/`catch` pair in the code is in charge of catching it. You do not need to dig into the details of this syntax right now—a glance at the structure is enough. Exceptions are an entire chunk of C++; a dedicated chapter later in this volume takes them apart, and the `at()` example will make its return there. What to remember for now is one contrast: `at()`'s behavior is deterministic—an out-of-bounds access always throws `std::out_of_range`, and the **type** thrown is guaranteed by the standard in black and white. The exact wording inside `what()` is libstdc++'s own writing (the GCC family's standard library); MSVC and libc++ produce different English sentences. When we write code, what we should depend on is the type, not the prose.

Then why do we usually dare to write subscripts like `temps[100]`? Because `operator[]` does no bounds checking—go out of bounds and it is undefined behavior: it might crash, might spit out garbage values, or might look perfectly fine. `at()` amounts to paying for one extra bounds check in exchange for a deterministic behavior. When debugging and when writing interfaces exposed to the outside, that cost is well worth it; only in hot paths where we have confirmed the index cannot go out of bounds do we dare to streak with `[]`.

Stitched together, the three snippets above form one complete runnable program—click it open and try it directly:

<OnlineCompilerDemo
  title="The Three STL Roles Cooperating: stl_overview.cpp"
  source-path="code/examples/vol1/30_stl_overview.cpp"
  description="Run the cooperation of container, iterators, and algorithms online. Try changing that mis-recorded 7 in temps to 35 and watch how each of the two output lines changes."
  run-options="-O2 -std=c++17"
  allow-run
/>

## A Quick Tour of the Common Containers and This Chapter's Route

Of the three roles, algorithms and iterators are supporting players; the container is the choice you have to make every single day. Let's lay out the containers this chapter will meet, and put a face to each one:

| Container                                  | What it is for                                          |
| ------------------------------------------ | ------------------------------------------------------- |
| `vector`                                   | Dynamic array, contiguous storage, O(1) random access; when in doubt, use it |
| `array`                                    | Fixed-length array, size locked in at compile time, no dynamic-allocation overhead |
| `deque`                                    | Double-ended queue, fast insert and erase at both ends, insertion and deletion in the middle stay painful |
| `list`                                     | Doubly linked list, O(1) insert and erase anywhere, but no random access |
| `map` / `set`                              | Sorted key-value pairs / sorted set of unique elements, red-black tree implementation, O(log n) operations |
| `unordered_map` / `unordered_set`          | Hash table implementation, O(1) on average, no guarantee of element order |

Behind every word in this table hides a trade-off, and this chapter takes them apart one by one in this order. First we spend a whole article digesting `vector`: it is the most frequently cast container, and the very embodiment of the phrase "when in doubt, use it." Then come the three brothers `map`, `set`, and `unordered_map`, answering the need "give me a key and look up a result." Next comes the turn of the algorithms library and its partner, the lambda. Finally, cross-container questions such as container choice and iterator invalidation get gathered up into one article. You will notice that this chapter is heavy on code and light on mechanism, because the templates chapter already finished laying the foundation for the mechanics—what we are doing now is mostly getting to know the tools and building a feel for them.

## Exercises

### Exercise 1: Swap in a Different Algorithm

Please swap `count_if` for `count` (the version without a condition). Think through what it counts before running it again to verify your expectation.

### Exercise 2: See Undefined Behavior with Your Own Eyes

Change `temps.at(100)` to `temps[100]`, run it several times, and also try both `-O0` and `-O2`, recording the behavior you observe. When you are done watching, remember to change it back to `at()`—there is no standard answer on this trip, and that is precisely what undefined behavior means.

### Exercise 3: Explain It to a Friend

Without writing code, answer out loud: why does `std::sort` work on both `vector<int>` and `vector<std::string>`? Please use the templates chapter's phrasing: "keep the requirements, strip away the types."
