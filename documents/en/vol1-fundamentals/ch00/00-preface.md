---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Understand the core value, application domains, and learning path of
  C++, and start your journey with modern C++
difficulty: beginner
order: 0
platform: host
reading_time_minutes: 12
tags:
- cpp-modern
- host
- beginner
- 入门
title: 'Preface: Why Learn C++'
translation:
  source: documents/vol1-fundamentals/ch00/00-preface.md
  source_hash: a325c7da9e2ba36456be49c902b9a6730af1aac620735e336a7e96d8d591b134
  translated_at: '2026-06-16T03:39:05.903466+00:00'
  engine: anthropic
  token_count: 1321
---
# Preface: Why Learn C++

To be honest, I thought for a long time about how to start this preface—what tone to strike. If I just coldly listed a bunch of reasons why "C++ is powerful," it would be no different from reading Wikipedia, which is boring. So, I want to try a different approach: let's talk about why I personally bother with C++, and why I believe that in 2026, C++ is still worth your time and serious effort.

## The Origin of This Tutorial

First, a little background. The starting point of this tutorial is actually a very personal motivation—in the process of doing embedded development, I increasingly felt that writing pure C became a struggle as projects grew. Manually managing resources, passing callback function pointers everywhere, and using macros for generics—these patterns, when used for a long time, cause code bloat that gives you a headache, and maintenance costs get higher and higher. I wondered, is there a way that doesn't lose the "close-to-hardware" control of C, but allows us to use more modern language features to organize code? The answer, of course, is C++. And not the "C with Classes" from the 90s, but Modern C++ that has evolved from C++11 all the way to C++23. (My journey into Modern C++ started with *Effective Modern C++*, which completely shattered my previous conceptions of C++.)

Later, when I actually knew a little C++ (really just a tiny bit... compared to the big shots), I found that many existing so-called "C++11" tutorials cover features that have since been deprecated or for which better solutions exist in newer C++ standards!

Well, in the AI era, learning is definitely much easier. I thought—could I create a Mono C++ collection repository, organizing the notes at hand into a more complete basic tutorial? This is the origin of this repository:

> <https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP>

and this specific volume. Of course, there are other volumes; I am slowly organizing my notes and using LLMs to see if there are points that can be expanded. This is how this set of tutorials came about. It's just that simple. I try to make this tutorial look, well, not like a language lawyer's manual, nor a translation of an official standard document—it is the study notes of someone struggling with C++ (looking up at various giants every day), recording the complete journey of mastering C++ from scratch.

> Q: Is there LLM-generated content?
> A: Yes, I admit that. I see LLMs as a good tool, but they are not reliable enough. So I hold myself to a standard: published content must be rewritten, at least striving to erase the traces of the LLM—at the very least, this is the responsibility I fulfill for my serious published content.

## Where is C++ Actually Used?

If you are still hesitating, wondering "is there a future in learning C++?", let's look at what C++ actually does in the real world.

The game industry is almost C++ home turf. Unreal Engine has been built with C++ since its inception and remains the engine of choice for Triple-A game development today; Unity's underlying runtime is also C++; even the recently popular Godot engine has its core modules written in C++. The game industry's extreme pursuit of performance—a budget of 16 milliseconds per frame, real-time rendering of millions of polygons, physics simulation, and AI logic—makes C++ almost irreplaceable in this field.

Operating systems and infrastructure software are traditional C++ territory. Windows core components make extensive use of C++, as do many system frameworks in macOS. While the Linux kernel itself insists on C, the entire user-space ecosystem surrounding it—from desktop environments to graphics drivers—relies heavily on C++. The database field goes without saying: the core implementations of MySQL, PostgreSQL, MongoDB, Redis—every single one of these names depends on C++.

Browsers are perhaps one of C++'s most successful application scenarios. Chrome's rendering engine Blink, Firefox's Gecko, Safari's WebKit—software used by billions of people every day—is all written in C++. Browsers need to do incredibly complex things: parse HTML and CSS, execute JavaScript, render pages, manage network requests and caches, all while maintaining 60fps fluidity. This places near-harsh demands on language performance. (My work involves dealing with Chromium; man, the C++ is written very well. I will pick out many component concepts to discuss, such as my favorite WeakPtr/Factory components).

High-frequency trading and financial systems are also deep users of C++. In the competition for nanosecond-level latency, every microsecond means real money. C++'s zero-overhead abstractions and precise memory control capabilities make it the standard language for quantitative trading systems. Compilers and development tools are the same—the cores of Clang and GCC are both C++. Even "higher-level" languages like Python and Java rely heavily on C++ in the bottom layers of their interpreters and virtual machines (CPython's reference implementation, the JVM's HotSpot compiler are classic examples).

And in the embedded field—the scenario this tutorial focuses on specifically—from peripheral drivers on STM32 microcontrollers, to task scheduling in RTOSs, to system-level programming on embedded Linux, C++ is gradually replacing traditional pure C solutions. This is because Modern C++ provides type safety and zero-overhead abstractions that are particularly valuable in resource-constrained environments. This is the value I originally wanted to demonstrate with this tutorial (trying to differentiate it). I personally love embedded systems, even though my skill level is terrible.

## What Makes C++ Unique?

At this point, you might ask: Aren't there other languages with good performance? Isn't Rust also strong? Isn't Go fast? Why learn C++ specifically?

That's a good question. Let's not rush to a conclusion, but look at a core concept of C++—**zero-overhead abstraction**. This is a quote from Bjarne Stroustrup, and the gist is: you don't pay any runtime cost for features you don't use, and for the features you do use, hand-written code won't be faster than what the compiler generates. This means you can use high-level abstractions like templates, RAII, smart pointers, and lambda expressions in C++ to organize code, while the compiler optimizes them into machine instructions almost identical to hand-written C code. This dual capability of "high-level abstraction + low-level control" is C++'s core competitive advantage.

Rust is indeed an excellent language. Its ownership system and borrow checker have made revolutionary contributions to memory safety. But the reality is, as of 2026, C++ still has over 16 million developers, its position as the world's fourth most popular language is rock-solid, and billions of lines of code libraries continue to run. Rust's ecosystem is still in a growth phase, while C++'s ecosystem is already deeply embedded in the marrow of key infrastructure like operating systems, game engines, compilers, and databases. This isn't to say Rust is bad, but rather that C++'s accumulation is too thick—decades of standard libraries, third-party libraries, toolchains, community experience, and documentation resources cannot be replaced in the short term.

Moreover, C++ itself hasn't stood still. Starting with C++11, the language has undergone a modernization rebirth: `auto` type deduction, move semantics, smart pointers, lambdas, `constexpr` compile-time computation, modules, concepts, coroutines, ranges—almost a new standard every three years, continuously improving the language's expressiveness and safety. The upcoming C++26 is even more heavyweight—static reflection, contracts, asynchronous senders/receivers, and other features have entered the standard, which will once again change the way we write C++. So the worry that "learning C++ is learning a dying language" can really be put to rest in 2026.

> Of course, to be honest, this is also a burden. I myself have gone through the process of learning C++98 to learning Modern C++. To be honest, it was painful, really painful. This also makes it very unfriendly to friends who want to build programs quickly. So, C++ (I might even say, including C) is really not suitable for friends who aren't interested in computers themselves. Dealing closely with memory, CPU, and possibly disks is not child's play.

## What Does This Volume Cover?

Having talked so much about "why learn," let's talk about "what exactly do we learn."

This volume is the **Foundation** of the entire tutorial system. The goal is to help you build a solid C++ foundation. We won't start with template metaprogramming or memory models—those are topics for later volumes. The content arrangement of this volume is gradual:

First is environment setup and running your first program, getting your development environment running, compiling and executing a piece of C++ code yourself, and feeling the complete process from source code to executable. Then we enter the type system and value categories, understanding how C++ views data—integers, floats, pointers, references, and the difference between lvalues and rvalues. Next is control flow, covering conditional branches, loops, and basic program logic organization. Further on are functions—parameter passing, return values, overloading, and default parameters, which are the basic units for building complex programs.

On top of this, we start touching on Object-Oriented Programming: classes and objects, construction and destruction, inheritance and polymorphism, and operator overloading. These are the core paradigms of C++ and the foundation for understanding subsequent advanced features. Finally, we will cover template basics, exception handling, an overview of the STL standard library, and the memory management model, giving you a basic grasp of the full picture of C++.

Note that this volume mainly covers C++ basic knowledge and core features from the C++98 era. Deep dives into Modern C++ (C++11 and later)—including move semantics, smart pointers, lambdas, `constexpr`, RAII, etc.—will be expanded in later volumes. So if you already have a certain C++ foundation and feel this is too simple, you can jump directly to later volumes to challenge more interesting content. But if you are a beginner, or want to systematically consolidate your foundation, I strongly suggest reading in order—the content ahead relies on understanding what comes before.

If you have absolutely no C language background, don't worry. In this volume, we provide an independent C language tutorial subdirectory, covering complete C language basics from data types, pointers, and arrays to structs and memory management. You can spend some time going through the C language tutorial first to establish a basic understanding of the underlying memory model and pointer operations, and then come back to learn C++—it will be much smoother.

## How to Use This Tutorial

Regarding usage, I have a few very practical suggestions.

**Read in order, do not skip around.** This tutorial is carefully sequenced; later content often references concepts explained earlier. If you skip around, you will likely hit a wall halfway through and be forced to look back—which wastes more time. If you really feel you have mastered a part, you can skim it to confirm you haven't missed anything, but try not to skip it entirely.

**Type the code out yourself.** This isn't a pleasantry. Understanding a piece of code and typing it out, compiling, running, and seeing the output yourself are two completely different learning experiences. You will discover various unexpected small problems while typing—spelling errors (kids, `int mian` isn't funny), forgetting semicolons (you aren't writing Python anymore), missing header files (who's family is `implicit declaration of XXX`)—these are all part of real programming. Meeting them early and getting used to them early is better than anything. So even if the example code in the tutorial looks simple, please type it out yourself.

LLMs are useful; I use AI to slack off myself, this is normal. But in the learning phase, I really don't recommend slacking off. I watched my buddy slack off only to be crushed by `undefined reference` all over the floor, finally discovering it was due to a lack of common sense in compilation technology. This example doesn't have much to do with C++, but it illustrates a point.

**Think for yourself when you don't understand, but don't get stuck.** If you don't understand a concept after two or three reads, mark it and continue reading. Many concepts will become clear in subsequent practical applications because the context changes, and your understanding will deepen accordingly. But if you come back and still don't understand, you can look for community discussions (I don't know if there are friends from the post-AI era; I am a regular on CSDN and StackOverflow; in the pre-AI era, I was a major code porter on these communities (really, I bow down to these dads)) or check the detailed explanations on cppreference.com.

## Let's Get Started

Writing this, I feel the basic groundwork is covered. C++ is a language with depth, and the learning curve isn't exactly smooth—I won't bullshit you on that. But it is also a language with extremely rich rewards: when you truly understand the elegance of RAII, the power of templates, and the philosophy of zero-overhead abstraction, you will find writing C++ to be a very satisfying experience.

This tutorial won't turn you into a C++ expert overnight—no tutorial can do that. But it will accompany you step-by-step along the whole road, from the most basic types and variables, to object-oriented design, to the use of templates and the standard library, providing clear explanations and runnable code at every stage. We don't need to be gifted, nor do we need a formal background in CS; all we need is patience and a willingness to get our hands dirty.

Alright, enough nonsense. In the next chapter, we start by setting up the development environment, getting the compiler running, and writing our first C++ program.

We are on our way.
