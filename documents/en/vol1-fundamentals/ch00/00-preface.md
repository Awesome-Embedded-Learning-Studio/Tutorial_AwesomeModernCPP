---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Understand the core value of C++, where it is actually used, and the
  learning route ahead — the start of your modern C++ journey
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
  source_hash: 2ef47889e6a21eafd46980fc30b41281f1d840eb24f7379bcc547bc0e87e5f18
  translated_at: '2026-09-25T09:42:43+00:00'
  engine: anthropic
  token_count: 3200
---
# Hey Everyone, Welcome to C++

No need for the words "to be honest" here — this simply *is* the truth. At the very beginning, this opening was nothing special; I wrote it back when the project was first getting started. I never imagined that one day this project would grow this lively! Which is why I've decided to perk up, pick up my keyboard, and give this opening a proper write.

I am not a language zealot, and honestly I have zero desire to become one of those die-hard fans of language X. I have **always held that a language is a form of expression for solving problems: there are only expressions that fit and expressions that don't — there is no "correct" or "incorrect" expression. (This is one of the baselines I want this tutorial to build on: explain where a language fits, analyze its pros and cons, and give those of us who love programming something new to think about, not a new dogma. In short: never expect one feature to solve every engineering problem.)**

So, by convention, most tutorials would start hyping C++ right about here — hey, our C++ is so fresh, so tasty, so awesome. Not me. I'd rather just sit down with you for a chat, drawing on the work I've done and the code I've written — about why we're all sitting here discussing the question "Why C++?".

> What I also mean is: a cold list of "C++ is powerful" reasons is no different from flipping through Wikipedia — frankly boring. So I want to try a different angle: talk about why I personally bother with C++, and why I believe that today, in 2026, C++ is still worth your time and serious study.

## Where This Tutorial Came From

Every journey owns its beginning, and we're no exception — ours will span at least 10 volumes, maybe more. My starting point traces back to December 2025, over one lunch break (yes, nothing sacred about the moment at all, purely a case of having eaten my fill): I was sitting in the company cafeteria when I suddenly found myself remembering my days writing embedded code. These days I do big-frontend development work in C++ — modern C++, at that. That trip down memory lane planted a question in me: C and C++ are such close neighbors, so why do they feel so different to write?

> How are they different? Let me sketch it briefly!
> This tutorial also expects everyone to be at least familiar with C. If your C seems to have faded, head over to the [C prerequisite content](../c_tutorials/01-program-structure-and-compilation.md) and start your journey from there. If you simply don't know C at all, this journey may not exactly suit you. But that's okay — you can still carry on; only the slope will feel a little different.
> C is Spartan-simple, so simple you can see straight through it to the assembly it becomes. But it's also a hassle: it doesn't suit large projects that demand both high-speed iteration and extremely high stability. We have to manage resources by hand, pass callback function pointers everywhere, and do generics with macros — keep up those habits long enough and the code bloat becomes a headache, with maintenance costs climbing ever higher. As for C++ — once you've studied it, you'll notice it really does differ from C by no small margin.

There I was, face to face with my stir-fried pork with chili peppers, wondering: is there a way to keep C's "close-to-the-metal" control while still using more modern language features to organize code? The answer, of course, is C++ — and not the 1990s "C with Classes" kind, but modern C++, evolved all the way from C++11 up to C++23.

> My modern C++ journey began with *Effective Modern C++*, a book that pretty much punched straight through my old ideas about the language. I'll recommend it here as a quick way to survey modern C++ — although it isn't suited for learning the more recent flavors of C++ (it mostly stays within C++11), for building the entry-level mindset it fits the bill well.

I did know a little C++ (really just a tiny bit... compared with the real heavyweights), and I had come across plenty of excellent modern C++ tutorials. But far more of them, back in my day, were so-called `modern C++` tutorials that really meant C++11 — and quite a few of their features have since been deprecated, or gained better answers in newer C++!

Well, it's the AI era — learning has certainly gotten much easier. So I thought: could I build a mono C++ collection repository, comb through the notes I had on hand, and shape them into a more complete foundational tutorial? That is this repository:

> <https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP>

— and the origin of this volume. There are other volumes, of course; I'll keep slowly organizing my notes and bringing in LLMs to see what can be expanded. Let the whole chain read as smoothly as possible, and give friends still writing C in embedded-land a taste of something fresh!

As the work went on, I simply went more general: Tutorial Awesome Modern C++. I've tried hard to make this tutorial look, um, not like a language-lawyer's manual, and not like a translation of the official standard document — it's the study notes of someone wrestling with C++ (gazing up at one legend.png after another, all day), recording the complete journey of mastering C++ from zero.

> Q: Even after rewriting this today, I still have to QA it all over again. Is there any LLM-generated content?
> A: There is — I admit that. In my view the LLM is a good tool, but not a reliable one. So the bar I set for myself is that published content must be rewritten, at least with the goal of scrubbing out the LLM's traces — the bare-minimum responsibility I owe to anything I publish seriously.

## Where Is C++ Actually Used?

If you're still hesitating over whether learning C++ leads anywhere, then let's look at what C++ is actually doing in the real world.

Let's start with two examples whose developer documentation you can find directly. In game development, Unreal Engine provides a complete [C++ programming entry point](https://dev.epicgames.com/documentation/en-us/unreal-engine/programming-with-cplusplus?application_version=4.27); in browsers, Chromium's Blink rendering engine communicates with its host through a [C++ API](https://www.chromium.org/blink/public-c-api/). My own work involves dealing with Chromium, and later I'll pull out some favorite components to discuss, such as WeakPtr / Factory. Learning C++ lets us read the implementations of these projects directly and understand what's really happening behind the interfaces.

That said, when listing application domains, C and C++ have to be kept straight. For example! [PostgreSQL is developed mainly in C](https://wiki.postgresql.org/wiki/Developer_FAQ), and [CPython is likewise a Python interpreter implemented in C](https://docs.python.org/3/glossary.html#term-CPython); you can't conclude a project is C++ just because it cares about performance and lives close to the metal. Language selection isn't a competition either: existing code, toolchains, team experience, and resource budgets are all far more concrete than "whose ranking is higher".

And in the embedded field — the scenario this tutorial series pays special attention to — what we care about is how, on top of existing C drivers and hardware interfaces, to use C++ to express resource ownership, interface constraints, and module relationships more clearly. The STM32 hands-on chapters later will unfold along exactly this direction. Not every platform is suited to enabling every language feature: compiler support, code size, and runtime cost all have to be factored in together. That's also why I want this tutorial series to lean heavily on real testing.

## So, Why C++, of All Languages?

At this point you might ask: C++ isn't the only language with good performance — isn't Rust strong too? Isn't Go fast too? Why learn C++, of all things?

Some friend might say: "Hey, are you about to start a flame war?"

Not at all. My actual answer is: there's no real need. The question above is missing an engineering context, so there's nothing to answer. **My answer is: it just so happens that the one widely used language I know best is C++. That's all. I have no plans to round up more reasons to defend C++**

Rust, Go, and their like are indeed sweeping the globe, gradually spreading into any scenario that demands high performance and high concurrency. C++, for most people, serves as perhaps their first genuine, multi-paradigm, high-level programming language. Starting from here, we get to see the programming notions common across computer programming — arguably all of them — and push our understanding deeper into the fields we're about to enter. Those can be high-performance computing, embedded engineering, backend work, or even the big-frontend where I sit; any position, any job. The sheer weight of its history — understanding what happened, and why it evolved this way — is itself our initiation into the world of computing, from the bottom at the level of registers, CPUs, and circuits, to the top at the browser you use to surf the internet.

> In other words, by learning C++ we hope to answer more rationally: "why C++, or any other programming language, is or isn't the right solution here." And Rust, Go, and the languages mentioned above each have engineering scenarios where they fit, too. We learn C++ here mainly to understand and maintain the C++ projects we care about, and to practice trading off between low-level control and code organization — we don't need developer headcounts or ranking boards to justify the choice. I also hope each of you can walk free of the church of language worship, and tell yourselves: "Just A Choice, Whatever". If this tutorial can help you get to that point, it is my honor.

This tutorial series runs along the C++11–C++23 mainline: for example, `auto` type deduction, move semantics, and lambdas have been provided since C++11, while concepts, coroutines, and ranges arrive with C++20. When we reach a specific feature, we'll mark the standard version and verify the examples; content from newer standards is left to the corresponding deep-dive topics — it is not a prerequisite for getting started.

> Honestly speaking, though, this is also a burden. I myself went through the pipeline of learning C++98 and then modern C++, and it was painful — truly painful. That makes it thoroughly unfriendly to friends who just want to build a program quickly. So C++ (I'm tempted to say C included) really doesn't suit people who aren't interested in the computer itself. Working up close with memory, the CPU, and maybe even the disk is no laughing matter.

## What This Volume Covers

This volume is the foundations of the whole tutorial system. I don't plan to drag you into template metaprogramming or the concurrency memory model right at the start — those wait in later volumes; no need for them to jump out now and scare everyone half to death. What this volume will do is build the fundamentals rock-solid: from how objects are stored and how lifetimes are reckoned, to types, functions, and classes — step by step.

The order goes like this: first get your development environment running, compile and execute a piece of C++ code with your own hands, and experience the whole journey from source code to executable; then move into the type system and value categories — integers, floating point, pointers, references, plus the lvalue/rvalue way C++ views data; control flow and functions follow — parameter passing, return values, overloading, default arguments — the basic units from which every complex program is later built. Only on top of that does object orientation get its turn: classes and objects, construction and destruction, inheritance and polymorphism, operator overloading. The volume closes with template basics, exception handling, an STL overview, and the memory management model, handing you a full picture of C++.

One thing to spell out up front: learning the fundamentals with a modern compilation environment does not mean you must first swallow C++98 style wholesale. Modern features like move semantics, smart pointers, lambdas, and constexpr will be dug into in later volumes; the idea of RAII, though, predates C++11, and this volume walks step by step from construction and destruction up to resource management. If you already have some C++ background and find this too simple, jump straight to the later volumes and pick whatever looks fun; if you're a newcomer, or want to consolidate the fundamentals systematically, I strongly recommend reading in order.

No C background at all? Don't worry. This volume carries a standalone C tutorial sub-directory, covering data types, pointers, and arrays all the way to structs and memory management — the complete C basics are all in there. It's positioned as supplementary material, not a mandatory prerequisite: absolute beginners can start directly from this chapter's environment setup, and whenever you find yourself short on C interface or pointer knowledge along the way, circle back and look it up as needed.

Here's the roadmap. Every stretch you finish, leave yourself a small runnable achievement; if you get lost later in your reading, come back and find your current position.

![Volume 1 learning route: first run programs, then learn types and control flow, functions and pointers, classes and objects, and finally move into templates, exceptions, STL, and memory fundamentals](./assets/00-preface/learning-route.drawio)

Starting from zero, pick either [Linux Environment Setup](./01-setup-linux) or [Windows Environment Setup](./02-setup-windows); if you can already compile a program, go straight into [Your First C++ Program](./03-first-program). Off you go, kids!

## How to Use This Tutorial

On usage, I have a few practical suggestions.

First: if you really, truly don't know how to start, consider reading in order — don't skip. The ordering of this series is designed: later content constantly references concepts covered earlier; skip around and you'll easily crash into something incomprehensible midway, then be forced to backtrack for it — costing more time in the end.

**Type the code yourself. Type the code yourself. Type the code yourself. Type the code yourself. Type the code yourself.**

I stress this to myself all the time, and I hope the emphasis takes root in your hearts as well. This tutorial will have mistakes; AI makes mistakes too. Does the compiler make mistakes? Extremely rarely — but for everyone still reading here, you'll seldom have a way to make it the culprit. Most of the time, the most practical move is to check whether we got something wrong ourselves.

In other words: understanding a piece of code by reading it, and typing it out with your own hands, compiling it, and watching the output — those are two completely different learning experiences. Along the way you'll run into all sorts of unexpected little problems — `int mian` (kids, this one really isn't funny), a missing semicolon (you're not writing Python anymore), a forgotten header include (whose `implicit declaration of XXX` is that?) — these are all part of real programming. Meeting them early and getting used to them early beats everything. However simple an example in this tutorial looks, please type it out once yourself.

LLMs are handy — I use AI to slack off myself, and that's perfectly normal. But in the learning stage, truly, don't cut corners. I watched with my own eyes as my own bro cut corners, got elbowed into pieces all over the floor by undefined reference, and finally discovered his compilation fundamentals weren't up to par. That example has little to do with C++, but it makes the point well enough.

One more: when stuck, think first — don't grind yourself to death against it. If a concept still doesn't click after two or three readings, mark it and keep going — many concepts only become clear in the practical applications that come later; as the context changes, understanding deepens with it. If it still doesn't make sense when you look back, go discuss it in the community (I don't know whether any of you are friends from the post-AI era; I myself am a CSDN and Stack Overflow regular — in the AI-less era I was one of those communities' big-time code haulers (I truly knelt before those gods)), or flip through the detailed write-ups on cppreference.com.

## All Set? Rest Up a Bit, Then Let's Begin

Take a rest, everyone! Work and study are hard on all of us — catch your breath, stand up, walk a couple of laps. In the days ahead, we're blasting through C++ together!

Still here? No knowledge in this section — we don't do the rat race. Having learned C++ to this day, my conclusion is: C++ is a language with depth, and its learning curve is honestly not gentle (or rather, where exactly is it gentle?) — on this point I won't snow you. But it is also a richly rewarding language — once you've truly got a handle on RAII, templates, zero-overhead abstraction and company, you'll find that writing C++ is a downright exhilarating thing.

This tutorial series won't turn you into a C++ expert overnight — **no tutorial can do that, and if someone claims theirs will make you a C++ master in 30 days, you'd better ask them what modern C++ even is**. The road will be rough, but I hope these tutorials can accompany you all the way to the end of it: from the most basic types and variables, to object-oriented design, and on to the use of templates and the standard library. Patience and the willingness to get hands-on, I believe, will decide how far our great expedition can go.

Rest a bit, folks — we're setting out.
