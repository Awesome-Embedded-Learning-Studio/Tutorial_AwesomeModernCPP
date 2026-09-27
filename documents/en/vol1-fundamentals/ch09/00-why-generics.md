---
chapter: 9
cpp_standard:
- 11
- 14
- 17
- 20
description: Three stacks whose logic is identical to the letter, yet copy-paste hands
  you three copies to maintain—generic programming turns the type into a parameter,
  templates are C++'s mechanism for putting that idea into practice, and one copy
  of code generates each type's own version at compile time.
difficulty: intermediate
order: 0
platform: host
prerequisites:
- OOP in Practice
reading_time_minutes: 11
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Why We Need Templates
translation:
  source: documents/vol1-fundamentals/ch09/00-why-generics.md
  source_hash: 8af77c55ad7242030cfc2c20d22a1c35562c903afdc5c43ae08fecf667364ea8
  translated_at: '2026-09-27T03:53:20+00:00'
  engine: anthropic
  token_count: 2700
---
# Why We Need Templates: Leaving the Type Open as a Parameter

Last chapter, when we loaded elements into our `Canvas`, we used a `vector<unique_ptr<Shape>>`; and when we called `emplace`, the line sitting above its head read `template <typename ConcreteShape, typename... Args>`. We took both of those at face value at the time—now we can let the cat out of the bag: `vector` is a template, and that line riding on top of `emplace` is a template declaration. You have been using templates for an entire chapter; you just never stopped to ask: how does one copy of code get away with holding `int` one moment and `std::string` the next?

> Good question—great question, even. A little spoiler before we start: welcome to the world of generic programming!

So let's start from a requirement that looks utterly unremarkable!

Write a stack that holds `int`; then another that holds `double`, and yet another that holds `std::string`. push, pop, top—three jobs, and the logic is identical down to the last character.

> Oh my! We seriously suggest you write it yourself, bro—consider it a chance to relive your days writing C; what you learn after this will sink in far deeper (grin).

What would you do? The path of least resistance is right in front of you: finish the `int` version, copy it twice, and swap out the type names.

It runs, but however many types the stack must hold, that's how many copies of the code you maintain: the day we discover that `pop` forgot to check for an empty stack, we have to go fix it file by file, and every one we miss is one more bug. If the standard library were written this way, `vector` alone would come with hundreds of nearly identical copies. The root of all this duplication comes down to a single thing: the type is hard-coded into the code.

## This Kind of Problem Has a Name: Generic Programming

The three-stack predicament—"one copy of logic, many types"—isn't bad luck we happened to stumble into; it is common enough to have earned a dedicated name: generic programming.

David Musser and Alexander Stepanov popularized that name in the 1980s, and their definition is refreshingly down-to-earth: abstract from concrete, efficient algorithms to arrive at algorithms that hold for a whole family of types. Applied to our two examples: in `smallest`, the logic genuinely uses nothing more than "two things can be compared"; in `Stack`, nothing more than "elements can be copied". Keep those requirements, strip away the concrete types, and one copy of logic holds for every type that satisfies them. "Generic" as in broad, "type" as in type—the name says exactly that.

This line of thinking is far older than C++ templates. The functional language ML had functions that held for arbitrary types back in the 1970s—think of `length`, which computes the length of a list no matter what its elements are. The Ada language already carried a generic mechanism when it was standardized in 1983, and Ada is what Musser and Stepanov used for their earliest batch of generic algorithm libraries. C++'s answer was the template, finalized around 1990, and the motivation was nothing complicated: write containers and algorithms once, fill in whatever type you like. Stepanov later moved to Hewlett-Packard Laboratories, where he and Meng Lee used templates to write the STL (Standard Template Library); in July 1994 the standards committee voted it into the draft, and it landed with the first official standard in 1998. Java's and C#'s own generics didn't catch up until 2004 and 2005 respectively. The `vector` you used every single day last chapter descends directly from that STL.

Seen this way, generic programming is by no means a C++ privilege; it is closer to a way of thinking. The templates we talk about are the language mechanism C++ built for this idea; the "generics" that Java and C# talk about are their own respective mechanisms—different implementations of the same idea. One more piece of background for your reference: Stepanov was famously blunt about object orientation, having said publicly, "I find OOP technically untenable!"

## Passing the Type In as a Parameter

The idea is in hand, the name is in hand; the question left is how to write it. The syntax C++ uses to realize this idea is called a template. Let's write the smallest one possible:

```cpp
template <typename T>
T smallest(T a, T b)
{
    return (a < b) ? a : b;
}
```

`template <typename T>` declares a type parameter `T`; every `T` in the function body waits to be replaced with a real type until we actually use the function. The act of filling in a real type and generating concrete code is called instantiation. We'll unpack every last detail of the syntax in the next article; for now, let's run it for real and see whether one copy of logic really does serve three types:

```cpp
std::cout << smallest(3, 7) << '\n';
std::cout << smallest(2.5, 1.5) << '\n';
std::cout << smallest(std::string("banana"), std::string("apple")) << '\n';
```

Output:

```text
3
1.5
apple
```

One pass of writing, and we have all three versions—`int`, `double`, `std::string`—without a single line of copy-paste. Functions can pull this off, and so can classes; the `Stack<int>`, `Stack<std::string>` notation is for the next article. Right now there is something more pressing that must be verified at once: what relationship do the types generated from a template have with one another?

## One Copy of Code Becomes a Crowd of Types

Let's write the stack as a class template, then deliberately drag in an old habit from the previous chapter: take a pointer of one type and point it at an object of another type, and see whether the compiler goes along.

```cpp
template <typename T>
class Stack {
public:
    void push(const T& v) { data_.push_back(v); }
private:
    std::vector<T> data_;
};

int main()
{
    Stack<int> a;
    Stack<double> b;
    Stack<int>* p = &b;    // Will the compiler allow this?
}
```

GCC's answer:

```text
distinct.cpp:15:21: error: cannot convert ‘Stack<double>*’ to ‘Stack<int>*’ in initialization
   15 |     Stack<int>* p = &b;
      |                     ^~
```

Not a chance. The compiler is telling us: `Stack<int>` and `Stack<double>` are two unrelated types, in exactly the same way that `int` and `double` themselves are. Both come from the same `Stack` template, and following that template, the compiler generates a separate, independent copy of the code for each type. Last chapter, inheritance turned a crowd of types into one family—a `Circle` is a kind of `Shape`; the template does the opposite, turning one copy of code into a crowd of types with no kinship whatsoever among them. That is why inside a `Stack<int>`, the elements are plain `int`s laid out contiguously—no vtable pointer, no indirect jumps; accessing them is indistinguishable from accessing a plain array.

## Two Roads, Each Minding Its Own Business

At this point you might want to ask: didn't last chapter's polymorphism also amount to "one interface serving many types"? When it comes to writing a stack, why can't virtual functions get a word in? One real attempt, following last chapter's line of thinking, makes it obvious. Give all stacks a common base class and define the interface like this:

```cpp
class StackBase {
public:
    virtual void push(const ???& value) = 0;    // What goes in the ??? spot?
};
```

`push`'s parameter type must be pinned down. Write `int`, and `std::string` can no longer get in; write a universal base class `Object`, and you are demanding that every element type derive from it. `Circle` can manage that, because it is a class we wrote ourselves—adding a `: public Shape` settles it. But `int`? Let's try to get it into the class hierarchy:

```cpp
class Shape {
public:
    virtual ~Shape() = default;
};

class IntShape : public int {    // Let int join the class hierarchy?
};
```

Run it for real, and GCC's error looks like this:

```text
dead1.cpp:6:25: error: expected class-name before ‘int’
    6 | class IntShape : public int {
      |                         ^~~
```

`int` is a built-in type; it is not a class at all, and nobody can inherit from it. Swap `int` for `double` and try again—the same error comes back once more, so the inheritance road is shut tight for built-in types across the board. Forcing it is still possible: write a base class `Box`, then create `class IntBox : public Box` to wrap the value before pushing it onto the stack, and unwrap it when taking it out.

Congratulations—you have just discovered Java's approach. But as you well know, this is, frankly, a hot mess. It runs completely against C++'s long-standing principles, and we trust you would have a hard time accepting it—though, to be sure, nothing stops you from writing it this way.

This style forces every element to be constructed as its own object on the heap, each carrying an extra vtable pointer on its back, with a virtual function call on every retrieval. Worse still, as many types as there are, that's how many wrappers you must write—`IntBox`, `DoubleBox`, `StringBox`, lining up all the way down. We set out to eliminate duplicated code, and this detour multiplies it instead. This wrapping scheme has a famous real-world instance: in Java, `List<int>` does not exist—only `List<Integer>`—which amounts to enshrining the wrapping as a language rule.

The root of this dead end is worth stopping to think through clearly: inheritance solves "choosing different implementations at runtime within one type family", while the stack problem runs exactly the other way—**what varies is the type of the parameter itself**. Type, as such, has no place to live in an inheritance hierarchy.

So virtual functions and templates are two roads that each mind their own business: virtual functions pick an implementation at runtime; templates generate implementations at compile time. When to take which road has a clear criterion. If **the concrete type is not known until runtime**, use virtual functions—`Canvas` draws whatever shape it resolves to, genuinely impossible to predict while writing the code. If **the type is settled the moment you write the code and it is purely the logic being duplicated**, use templates—the instant you type `Stack<int>`, the type is fixed.

This template approach goes by a widely circulated name: compile-time polymorphism, also known as static polymorphism. What the name is trying to say: the very same call can land on different implementations—`smallest(3, 7)` and the string version of `smallest` have call sites that look exactly alike. But the word "polymorphism" can lead us astray, as if `Stack<int>` and `Stack<double>` shared the family bond that `Circle` and `Shape` have—they do not; the compiler already delivered that verdict a moment ago. Inheritance-based polymorphism is one interface, many implementations, with one picked at runtime; over on the template side, one copy of code generates a version per type at compile time, and there is no family among the types.

|                             | Virtual function polymorphism          | Template generics                                    |
| --------------------------- | -------------------------------------- | ---------------------------------------------------- |
| When implementations are picked/generated | Runtime                    | Compile time                                         |
| Relationship between types  | Must share a common base class         | Unrelated; they only need to carry this logic        |
| Built-in types (`int`, `double`) | Locked out; wrapping is the only way in | Supported directly                               |
| Cost profile                | One indirect jump per call, and it blocks inlining | Zero dispatch overhead, but one code copy per type |
| Typical scenarios           | Plugins, GUI widget trees, runtime-parsed input | Containers, general-purpose algorithms, utility functions |

The two roads are not an either-or; in real-world engineering they often work in concert. Look back at last chapter's `Canvas`: it holds a `vector<unique_ptr<Shape>>`. The `vector` itself is a template, in charge of "what type the container holds"; the `Shape*` stored inside goes through virtual functions, in charge of "how the canvas draws". Two mechanisms inside one class, each doing its own job.

The next three articles take the template's parts apart one by one: function templates make type deduction clear and teach how to read those dozens-of-lines error messages; class templates have us implement a generic stack with our own hands; specialization handles how to make separate arrangements for "types the general version cannot take care of". Once you finish these three, you will be able to see how the things you use every day—`vector`, `string`—are actually built. Chapter 10's STL stands entirely on this chapter's foundation.

## Exercises

### Exercise 1: Deciding Which Road to Take

For each of the three scenarios below, pick virtual functions or templates, and give a one-sentence reason: a sequential list that can hold elements of any type; plugin-style tools in a graphics editor that can be enabled at runtime; parsing a JSON file whose values may be strings, numbers, or nested objects.

### Exercise 2: Spot the Templates in the Previous Article

Go back to the previous article, OOP in Practice, and find every spot where a template shows up (at least 4). Hint: the line sitting above `emplace` is merely the most conspicuous one.

### Exercise 3: Explain It to a Friend

Without writing any code, explain out loud: why can a `Shape*` point to a `Circle`, while a `Stack<int>*` cannot point to a `Stack<double>`? If you can articulate the difference between these two relationships clearly, this article counts as passed.
