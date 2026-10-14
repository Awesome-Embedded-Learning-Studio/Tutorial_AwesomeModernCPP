---
title: "Learning Roadmap"
description: "Pick a starting point by background, see the ten volumes at a glance, then read them one by one, with pacing advice, companion resources, and content maturity along the way. This page answers how to learn, where to start, and what each volume teaches"
translation:
  source: documents/roadmap/index.md
  source_hash: 6c3ff7739e6f8aa8e847f03412a6ce01738242ca8632e6a9f93ae0d148dfd2ce
  translated_at: '2026-10-11T08:54:10+00:00'
  engine: anthropic
  token_count: 2600
---

# Learning Roadmap

Hey! If you're looking at this page, you're probably feeling a bit lost — right? If you're truly new, meeting C++ or even programming for the first time, you've no doubt heard that C++ is a hard language to break into. It is. There's no denying it: the language carries heavy historical baggage, one language with a thousand ways to write the same thing. It's easy to slide into language-lawyer territory, and then find yourself helpless the moment real production work needs to ship. This little repository is an attempt to hand you a way through.

We hope that every volume, once you've walked through it, leaves you with something to think about and something to take away. C++ truly is a weighty language, and every construct can reflect a weighty historical legacy. That, I believe, is something AI in the LLM era can't teach you, and can't easily cultivate either. But it is a hard road — you wouldn't be here otherwise, right?

This page answers three questions only: how to learn it, where to start, and what each volume teaches. Ready to go? The path chooser and the overview cards sit right below. Want a closer look at each volume? The **volume-by-volume breakdown** walks through them one at a time. Just checking whether a volume is finished? **Content maturity** at the end of the page gives you the qualitative answer.

> What you are reading is the **learning roadmap** — it answers only how to learn. The project's own development progress and release planning are a separate matter, and we pull the two apart at the end of this page.

<RoadmapJourney />

## Pick a Starting Point by Background

Your current background decides the starting point, and you don't have to begin at page one of Vol.1. Starting from zero, or knowing only C? We put the entry at the Quick Start volume, with Vol.1 right after it. Coming from C or embedded and missing the modern style? Vol.2 is where you catch up, then you dive into the hands-on work in Vol.8. Already writing C++? Go straight to the topics that match your goals. The chips on the cards below are clickable and take you straight to the corresponding volume.

<PathChooser />

## The Ten Volumes at a Glance

The card wall below shows each entry's role, difficulty, prerequisites, and status. Click a card to enter the volume, or click "On this page" to jump to its breakdown below. The main line runs Vol.1 through Vol.10. Quick Start and Compilation and Linking in Depth sit outside that numbering, one at the very front and one alongside the engineering volume, and both have their own cards on the wall.

<VolumeCardGrid />

## Volume-by-Volume Breakdown

The entries below are ordered by learning path rather than by volume number: Quick Start first, then Vol.1, with Compilation and Linking in Depth sitting next to Vol.7. Each entry covers role, topics, difficulty, and prerequisites, and closes with my pacing advice. Entry length follows the content — a few volumes wrap up in a couple of lines, others earn more.

<a id="gs"></a>

### Quick Start

If you're starting from zero, this is the entrance built for you, placed before Vol.1. The volume starts from what an editor and a compiler actually are, walks you through running your first program, then assembles a multi-file project with CMake and gets clangd completion and squiggles working — with real screenshots throughout, so you can follow along click by click. The classic blockers, an environment that won't install and a program that won't run, are all dealt with here.

Beginner level, no prerequisites to bring. With a programming background you can skip the volume entirely and come back when your environment misbehaves. Work through it, and the syntax course in Vol.1 won't be hanging in the air.

<a id="v1"></a>

### Vol.1 · Fundamentals

Vol.1 is the foundation of the whole tutorial: we go from the environment and the type system through object orientation, first steps with templates, and exceptions, and close with a C crash course covering pointers, struct alignment, and embedded C idioms. Finish it and the later volumes share a vocabulary with you, which is exactly why it comes first.

Difficulty runs from beginner to intermediate, and you need no prerequisites at all. Read from the top if you're new. Skip the C crash course if C is already home turf — but spend real time on value categories, object orientation, and template basics, because they decide how smooth everything after goes. This volume is being rewritten: chapter order may shift, the topic coverage won't, so don't be surprised by the occasional awkward arrangement.

<a id="v2"></a>

### Vol.2 · Modern Features

The watershed between writing C++ and writing modern C++ sits in [Vol.2](/vol2-modern-features/). Move semantics, rvalue references, smart pointers, and ownership carry the weight. `constexpr`, lambdas, and structured bindings follow at the syntax level. A set of type-safe tools (`enum class`, `variant`, `optional`, `expected`) rounds out the base every later volume builds on.

Intermediate at most, one prerequisite. This one rewards a careful read, and skimming pays poorly. Concurrency, performance, and the domain work later all assume you've internalized move semantics and smart pointers. The content is complete, and we're still polishing wording and examples article by article, so an occasional style mismatch between articles is normal and doesn't affect the material.

<a id="v3"></a>

### Vol.3 · Standard Library in Depth

[Vol.3](/vol3-standard-library/) covers the machinery under the standard library, and it outgrew containers and strings long ago. We split it into six subdirectories: containers, strings, iterators, and algorithms form the data-structure group, and IO streams, time and numerics, error handling, and utilities form the everyday-development group. Both are complete. How `vector` grows, when iterators invalidate, how `string`'s small-string optimization saves memory — the questions about what the standard library does for you all get answered here.

Intermediate, with Vol.1 and Vol.2 as prerequisites. The volume is substantial now, so reading on demand works best: dig into containers and strings for performance-sensitive work, the IO group if streams are your daily bread. Everything is in place, and any article works as an entry point.

<a id="v4"></a>

### Vol.4 · Advanced Topics

Writing libraries or high-performance generic code? [Vol.4](/vol4-advanced/) is unavoidable. Coroutine basics and a scheduler implementation sit at the top, alongside three-way comparison, the empty base optimization, and MSVC Modules. Below, three template sublines follow the standards timeline: C++11/14 lay the template foundations, C++17 brings template techniques and Ranges, and C++20/23 is metaprogramming, where concepts, requires expressions, and static reflection all live. A small STL project at the end puts the whole toolbox to work. Design patterns form a separate, independent line, and the classic patterns are all in.

Advanced, gated by Vol.2 and Vol.3. Reading the template sublines in timeline order is the easiest path — concepts and requires are the lingua franca of modern template code, so get comfortable with them before entering metaprogramming. Coroutines, three-way comparison, and Modules you can take à la carte. The table of contents is not the order.

<a id="v5"></a>

### Vol.5 · Concurrency

[Vol.5](/vol5-concurrency/) teaches concurrency with a deliberately restrained ordering: correctness before performance, locks before lock-free, synchronization primitives before task queues. Chapters start with concurrency basics and the thread lifecycle, move through `atomic` and the memory model, then pick up concurrent data structures, `future` and thread pools, async IO and coroutines in turn. Actor and Channel each get a chapter, and the last two cover debugging and testing plus the bridge to distributed systems.

This is the most hands-on volume in the tutorial — the Labs and the Capstone all revolve around it, with a companion exercise area. Lab 0/1 come with full scaffolding you can start writing against right away. Lab 2 through the Capstone exist as handbook text only at the moment, and the scaffolding is still to come, so you'd assemble your own. Difficulty is intermediate-advanced, and you'll want Vol.1 through Vol.4 all in place.

<a id="v6"></a>

### Vol.6 · Performance

We rearranged [Vol.6](/vol6-performance/) entirely around measurement methodology: it opens with performance thinking and benchmarking method, moves through CPU microarchitecture, then teaches you to attribute a performance problem to its actual bottleneck, and then to optimize that bottleneck. Multicore performance, the cost of C++ abstraction, and compilers versus code size each get their own chapter.

The habit this volume wants to break is optimizing by feel: algorithmic complexity is not what the hardware experiences, and you need measurement data before your hands touch the tuning knobs. Intermediate-advanced. Read it after Vol.5, and it makes the best reference for the day your code stops being fast.

<a id="v7"></a>

### Vol.7 · Engineering Practice

[Vol.7](/vol7-engineering/) handles the engineering landing: CMake basics, cross-compilation, compiler options, linkers and linker scripts, then WSL development, how MSVC debugging works, and VS2026 Modules, all under one roof. The file-copier project walks the full path from requirements and framework to the core implementation, and the direction tracks leave an entrance for readers of different stripes.

Intermediate, and the volume is mid-construction. Read it alongside Compilation and Linking in Depth. When the mechanism side and the tool side both line up, engineering problems become tractable.

<a id="cmp"></a>

### Compilation and Linking in Depth

[This volume](/compilation/) answers what the compiler and the linker do behind your back: how source becomes an executable, where static and dynamic libraries differ, how symbols get found and how they fail to, and which step an `undefined reference` actually died at. Static libraries get an article of their own. Dynamic libraries run from design principles through symbol visibility, runtime loading, search logic, and executability. The volume is complete.

Intermediate, and a C++ foundation is all it takes. Before cross-compiling, designing a dynamic library's ABI, or chasing a link error, it's worth a pass, and it works best paired with Vol.7.

<a id="v8"></a>

### Vol.8 · Domain Applications

Vol.8 puts modern C++ into concrete domains. The subdomains differ wildly in progress, so we take them one completion state at a time.

The systems-programming subdomain is complete, and it runs Linux and Windows in parallel: the same topic gets the POSIX treatment on the Linux side and the Win32 treatment on the Windows side, with each article reconciling the two at the end. Processes, memory, file IO, IO multiplexing, terminals, and time are all in place, and cross-platform abstractions and shared utilities close the volume, and the IO multiplexing chapter connects directly to epoll in the networking subdomain.

Embedded is where the main line lives, and it's still under construction: the f103 main line is moving, the first articles of ZerOS, a hand-rolled RTOS, are in place, and f407 is a placeholder for now. Networking runs from classic sockets to modern wrappers and on into epoll and the Reactor pattern, with a mini-reactor Lab alongside. TinyML walks you through building an inference engine by hand, and the pointer-semantics and mini-STL deep dives are there when you want to dig. Algorithms, data storage, and GUI hold placeholders, and testing doesn't even have that yet.

Entry difficulty is intermediate, but prerequisites differ by subdomain: read systems programming and networking after Vol.1 through Vol.7. For embedded you need a board on your desk, and the peripheral chapters are meant to be followed hands-on.

<a id="v9"></a>

### Vol.9 · Learning from Open Source

In [Vol.9](/vol9-open-source-project-learn/) we take industrial-grade source apart, and the current focus is Chromium: the OnceCallback callback design and the WeakPtr weak pointer each have their articles, flat_map and no_destructor have their own pieces, and the prerequisites `deducing this` and `move_only_function` get dedicated deep dives.

Intermediate-advanced, with Vol.1 through Vol.7 all as prerequisites, and the foundations from Vol.4 and Vol.5 matter most. It's a source-reading volume: finish templates and concurrency before stepping in.

<a id="v10"></a>

### Vol.10 · Course and Talk Notes

[Vol.10](/vol10-open-lecture-notes/) is talk notes and secondary creations, currently drawn from CppCon 2025: Bjarne Stroustrup on concept-based generic programming, Matt Godbolt's *Some Assembly Required* to walk you into reading assembly, Mike Shah on Ranges and Ben Saks on move semantics (both Back to Basics talks), plus sessions on the evolution of `std::optional`, how microbenchmarks lie, and cache-friendly C++.

Intermediate, with prerequisites spanning Vol.1 through Vol.5. Use it for depth: after finishing the matching volume, reading the related notes sets what you learned a shade firmer. Weave it between the main-line volumes — on its own it can't carry the main line.

## Pacing and Advice

Starting from zero, go Quick Start into Vol.1, the path with the least friction. With background, jump in via the paths above. If you'd rather be safe, follow the volume order strictly. The price of skipping is backtracking for prerequisites, and every volume's table of contents doubles as an index when you do.

The whole journey is a long-term project — don't count on a shortcut. We suggest setting goals by volume and typing out every volume's examples by hand, and keep reading and writing at least at one to one.

[Weekly Problems](/weekly-problems/) works as homework: you can start right after Vol.1, a fresh pack arrives each week, you write code in the browser, and the judging happens in that same browser. A pack after each block you finish, and the feel never goes away.

Practice works the same way: finish a block, then pick the matching project from the [hands-on projects](/projects/) index. Build a coroutine server, a concurrent runtime, or an embedded project, and the scattered knowledge turns into something that runs.

## Companion Resources

- [C++ feature reference cards](/cpp-reference/): quick lookup from C++98 to C++26, organized as two views, by standard version and by feature category, with each card annotated for embedded applicability.
- [Hands-on projects](/projects/): a project index that strings together the practical work scattered across the volumes.
- [Community articles](/community/): community submissions and reviews, and your contributions are welcome.
- [Vol.10 talk notes](/vol10-open-lecture-notes/): conference talk notes and secondary creations, for going deeper.
- [Weekly Problems](/weekly-problems/): the online problem column, a pack a week, with problem contributions welcome.
- [Crash Lab](/crash-lab/): debugging case files that crash on purpose, each shipping the faulty code, the fixed version, and the diagnosis trail.
- [Appendix · Glossary](/appendix/): a Chinese–English glossary of technical terms, for when the standard translation of a term won't come to mind.

## Content Maturity

Which volumes are finished and which are still growing — the component below gives one set of qualitative judgments: **done** means the volume is fully in place and safe to read, while **in progress** means content is still being added or rewritten. Plan against it as a state report, not a quality ranking.

<MaturityBoard />

What the project itself is building and how releases are paced live in two other documents: the [project development roadmap](/community/dev/) covers maintenance cadence, release governance, and site evolution, while the [changelogs](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/changelogs) record what each released version changed.
