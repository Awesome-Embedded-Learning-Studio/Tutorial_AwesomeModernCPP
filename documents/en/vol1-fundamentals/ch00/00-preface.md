---
title: 'Preface: Why Learn C++'
description: Understand the core value, application areas, and learning path of C++,
  and start your modern C++ journey
chapter: 0
order: 0
difficulty: beginner
reading_time_minutes: 8
platform: host
tags:
- cpp-modern
- host
- beginner
- 入门
cpp_standard:
- 11
- 14
- 17
- 20
- 23
---
# Preface: Why Learn C++

To be honest, I spent a long time thinking about how to open this preface. If I just coldly listed a bunch of reasons why "C++ is powerful," it would be no different from reading Wikipedia, which would be pretty boring. So I want to take a different approach: let's talk about why I personally bother with C++, and why I believe that in 2026, C++ is still worth your time to learn seriously.

## How This Tutorial Came to Be

Let me give you some background first. The starting point of this tutorial is actually a very personal motivation—as I did embedded development, I increasingly felt that writing pure C was becoming unsustainable. Manually managing resources, passing callback function pointers everywhere, using macros for generic programming—after using these patterns for a while, the code bloat gave me headaches, and maintenance costs kept climbing. I started wondering: is there a way to keep that C-like "close to the hardware" control, while using more modern language features to organize code? The answer, of course, is C++—and not the nineties-era "C with Classes," but modern C++ as it evolved from C++11 all the way to C++23. (My own journey into modern C++ started with *Effective Modern C++*, which completely shattered my previous notions about the language.)

Later, after I actually learned a little bit of C++ (really just a tiny bit... compared to the real experts out there), I discovered that many so-called `现代C++` tutorials claiming to teach C++11 still feature plenty of constructs that have since been deprecated or superseded by better solutions in newer C++ standards!

Well, in the AI era, learning is definitely much easier. So I thought—could I create a mono C++ repository, organize my notes, and turn them into a more comprehensive foundational tutorial? That's exactly what this repository is:

> <https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP>

And that's how this volume came to be. There are other volumes, too—I'm slowly organizing my notes and using LLMs to see if there are areas worth expanding on. That's the origin story of this tutorial series. It's really that simple. I'm trying to make this tutorial look, uh, not like a language lawyer's manual, nor a translation of the official standard document—it's the study notes of someone struggling with C++ (constantly looking up to the experts), documenting the complete journey of mastering C++ from scratch.

> Q: Is there LLM-generated content?
> A: Yes, I admit it. I see LLMs as a good tool, but they aren't reliable enough. So I hold myself to this standard: published content must be rewritten, at least attempting to erase any traces of LLM generation—that's the bare minimum responsibility I owe to my serious publications.

## Where Is C++ Actually Used

If you're still hesitating about whether learning C++ has a future, let's look at what C++ is actually doing in the real world.

The game industry is almost C++'s home turf. Unreal Engine has been built with C++ at its core since its inception, and it remains the go-to engine for Triple-A game development today. Unity's underlying runtime is likewise C++; even the recently popular Godot engine has its core modules written in C++. The game industry's extreme pursuit of performance—a budget of 16 milliseconds per frame, real-time rendering of millions of polygons, physics simulation, and AI logic—makes C++ virtually irreplaceable in this domain.

Operating systems and foundational software are even more traditional territory for C++. Many core components of Windows use C++ extensively, as do many system frameworks in macOS. While the Linux kernel itself insists on C, the entire user-space ecosystem built around it—from desktop environments to graphics drivers—relies heavily on C++. The database field goes without saying: MySQL, PostgreSQL, MongoDB, Redis—every single one of these names depends on C++ for its core implementation.

Browsers might be one of C++'s most successful application scenarios. Chrome's rendering engine Blink, Firefox's Gecko, Safari's WebKit—software used by billions of people every day, all written in C++. What browsers need to do is extraordinarily complex: parsing HTML and CSS, executing JavaScript, rendering pages, managing network requests and caches, all while maintaining 60fps smoothness. The performance demands on the language are nearly ruthless. (My own work involves dealing with Chromium—man, the C++ code is really well-written. I'll pull out and discuss various component concepts, like the `WeakPtr`/`Factory` components I'm particularly fond of.)

High-frequency trading and financial systems are also deep C++ users. In the race for nanosecond-level latency, every microsecond means real money, and C++'s zero-overhead abstraction and precise memory control make it the standard language for quantitative trading systems. The same goes for compilers and development tools—the cores of Clang and GCC are both C++. Even "higher-level" languages like Python and Java rely heavily on C++ in their interpreters and VM underpinnings (CPython's reference implementation and the JVM's HotSpot compiler are classic examples).

And in the embedded domain—which this tutorial series pays special attention to—from peripheral drivers on STM32 MCUs, to task scheduling in an RTOS, to system-level programming on embedded Linux, C++ is gradually replacing traditional pure C solutions. This is because the type safety and zero-overhead abstraction provided by modern C++ are especially valuable in resource-constrained environments. This is the value I originally wanted to deliver with this tutorial (my attempt at differentiation). I personally love embedded systems, even though my skill level is pretty terrible.

## What Makes C++ Unique

At this point, you might ask: C++ isn't the only performant language out there. Isn't Rust also very powerful? Isn't Go also fast? Why learn C++ specifically?

That's a great question. Let's not rush to a conclusion; instead, let's look at a core philosophy of C++—**zero-overhead abstraction**. This is a phrase coined by Bjarne Stroustrup, and the gist is: you don't pay any runtime cost for features you don't use, and for the features you do use, hand-written code won't be faster than what the compiler generates. This means you can use high-level abstractions like templates, RAII, smart pointers, and lambda expressions to organize your code in C++, while the compiler optimizes them down to machine instructions almost identical to hand-written C code. This dual capability of "high-level abstraction + low-level control" is C++'s most core competitive advantage.

Rust is indeed an excellent language; its ownership system and borrow checker have made revolutionary contributions to memory safety. But the reality is that as of 2026, C++ still has over 16 million developers, its position as the world's fourth most popular language is rock-solid, and billions of lines of codebases continue to run. Rust's ecosystem is still in its growth phase, whereas C++'s ecosystem is deeply embedded in the very marrow of critical infrastructure like operating systems, game engines, compilers, and databases. This isn't to say Rust is bad—rather, C++'s accumulated foundation is simply too deep. Decades of standard libraries, third-party libraries, toolchains, community experience, and documentation resources cannot be replaced in the short term.

Moreover, C++ itself hasn't stood still. Starting with C++11, the language went through a thorough modernization: `auto` type deduction, move semantics, smart pointers, lambdas, `constexpr` compile-time computation, modules, concepts, coroutines, ranges—almost a new standard every three years, continuously improving the language's expressiveness and safety. The upcoming C++26 is even more heavyweight—features like static reflection, contracts, and sender/receiver have already entered the standard, and these will once again change how we write C++. So the worry of "am I learning an obsolete language?" can genuinely be put to rest in 2026.

> To be completely honest, though, this is also a burden. I personally went through the process of learning C++98 and then transitioning to modern C++, and frankly, it was painful. Really painful. This also makes it very unfriendly to friends who want to build programs quickly. So, C++ (I'd even say this includes C) is truly not suitable for people who have no interest in computers themselves. Dealing closely with memory, the CPU, and possibly even the disk is no child's play.

## What This Volume Covers

Having talked so much about "why learn," let's now discuss "what we specifically learn."

This volume is the **Foundations** volume of the entire tutorial series. The goal is to help you build a solid C++ foundation. We won't start off with hardcore topics like template metaprogramming or memory models—that's for later volumes. The content in this volume is arranged progressively:

First is environment setup and running your first program, getting your development environment up and running, personally compiling and executing a piece of C++ code, and experiencing the complete process from source code to executable. Then we dive into the type system and value categories, understanding how C++ views data—integers, floats, pointers, references, and the distinction between lvalues and rvalues. Next is control flow, covering conditional branching, loops, and basic program logic organization. After that comes functions—parameter passing, return values, overloading, and default arguments, which are the basic building blocks for constructing complex programs.

On top of that, we start touching on object-oriented programming: classes and objects, construction and destruction, inheritance and polymorphism, and operator overloading. These are C++'s core paradigms and the foundation for understanding the advanced features that follow. Finally, we cover template basics, exception handling, an overview of the STL standard library, and the memory management model, giving you a basic grasp of the full picture of C++.

Note that this volume primarily covers foundational C++ knowledge and the core features of the C++98 era. In-depth exploration of modern C++ (C++11 and later)—including move semantics, smart pointers, lambda expressions, `constexpr`, RAII, and so on—will unfold in subsequent volumes. So if you already have some C++ foundation and feel this is too simple, you can jump straight to later volumes for more interesting challenges. But if you're a beginner, or want to systematically solidify your foundation, I strongly recommend reading in order—everything that follows is built on the understanding established earlier.

If you have absolutely no C language background, don't worry either. In this volume, we provide a standalone C language tutorial subdirectory, covering complete C fundamentals from data types, pointers, and arrays to structs and memory management. You can spend some time going through the C tutorial first, building a basic understanding of the underlying memory model and pointer operations, and then come back to learn C++—things will go much more smoothly that way.

## How to Use This Tutorial

Regarding how to use this tutorial, I have a few very practical suggestions.

**Read in order, don't skip around.** The sequence of this tutorial is carefully designed, and later content frequently references concepts already explained earlier. If you skip around, you'll very likely hit things you don't understand midway, and then be forced to go back and find them—which actually wastes more time. If you really feel you've already mastered a certain part, you can quickly skim it to confirm you haven't missed anything, but try not to skip it entirely.

**Type the code out yourself.** I'm not just saying this to be polite. Understanding a piece of code and personally typing it out, compiling and running it, and seeing the output are two completely different learning experiences. You'll discover all sorts of unexpected little issues while typing code—spelling errors (kids, `int mian` isn't funny), forgetting semicolons (you're not writing Python anymore), missing header includes (who does `implicit declaration of XXX`)—these are all part of real programming. Encountering them early and getting used to them early is better than anything. So no matter how simple the example code in the tutorial looks, please type it out yourself.

LLMs are handy—I use AI to slack off myself, which is totally normal. But during the learning phase, I really don't recommend cutting corners. I've personally watched my buddy slack off, only to get absolutely shattered by `undefined reference` errors, which ultimately came down to his own lack of basic compilation knowledge. This example doesn't have much to do with C++ itself, but it illustrates the point well enough.

**When you encounter something you don't understand, think about it yourself first, but don't obsess over it.** If you've read a concept two or three times and still don't get it, mark it and keep reading. Many concepts become clearer in subsequent practical applications, because as the context changes, your understanding deepens along with it. But if you come back to it later and still don't understand, you can go find community discussions (I don't know if there are friends from the post-AI era—I'm a regular on CSDN and Stack Overflow; in the pre-AI era, I was a major code scavenger on these sites—seriously, I bow down to these legends) or check the detailed explanations on cppreference.com.

## Let's Get Started

At this point, I think I've covered all the groundwork that needed to be laid. C++ is a deep language, and its learning curve is admittedly not gentle—I won't mislead you about that. But it's also an incredibly rewarding language: when you truly understand the elegance of RAII, the power of templates, and the philosophy of zero-overhead abstraction, you'll find that writing C++ is a deeply satisfying experience.

This tutorial won't turn you into a C++ expert overnight—no tutorial can do that. But it will walk with you step by step through the entire journey, from the most basic types and variables, to object-oriented design, to the use of templates and the standard library, providing clear explanations and runnable code at every stage. We don't need extraordinary talent, nor do we need a formal computer science degree. All we need is patience and a willingness to get our hands dirty.

Alright, enough talk. In the next chapter, we'll start by setting up the development environment, getting the compiler running, and writing our first C++ program.

Let's hit the road.
