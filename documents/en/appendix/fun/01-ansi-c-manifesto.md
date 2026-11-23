---
title: "A Manifesto Against ANSI C"
description: "Community Contribution: Using the style of a 'Manifesto' to criticize ANSI C and praise Modern C++, for fun"
chapter: 99
order: 1
tags:
  - host
  - cpp-modern
  - beginner
  - 整活
difficulty: beginner
platform: host
reading_time_minutes: 3
contributors:
  - github: owollz4
    roles:
      - author
translation:
  source: documents/appendix/fun/01-ansi-c-manifesto.md
  source_hash: 5c05a0566b95d9b4ccaa41c622467d0235bd89820be34dd09e7b9017a309cab8
  translated_at: '2026-11-22T13:37:53+00:00'
  engine: qwen3:14b
  token_count: 2062
---
# A Manifesto Against ANSI C

> **Editor's Note**: This article is a community contribution by [owollz4](https://github.com/owollz4), originally posted in [Issue #274](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/issues/274). When it was included, only the formatting was adjusted, and the text was kept as is. A 'manifesto' is an ancient declaration used before a battle to denounce the enemy and inspire one's own side — this article uses this style to list the advantages of Modern C++ one by one. It's for fun, and the technical stance should be taken from the main content of each chapter.

The people have long suffered under ANSI C; the old order has perished, and Modern C++ shall rise!

Recalling the past, the two sages K&R forged the sword in Bell Labs, and the C language emerged from the heavens: concise syntax, close to hardware, and the name of portability shook the world. Its contribution to the founding of the nation is indelible. However, since the establishment of ANSI C (C89), over forty years of wind and rain have passed, and the accumulated problems have become mountains, and the people have suffered greatly.

**Crime One: Memory management is entirely manual.** malloc/free are manually handled, and a slight mistake can lead to leaks, dangling pointers, and double frees, all three disasters at once; valgrind chasing the culprit all night, Segmentation fault (core dumped) shocks you awake.

**Crime Two: Generics are nothing more than a mirage.** To write generic code, the only option is void*, with type casting flying everywhere, type safety is a mere name, and the compiler watches silently.

**Crime Three: Strings are naked.** Character arrays add '\0' and claim to be strings; strcpy, sprintf are full of landmines, buffer overflows are a century-old nightmare in security.

**Crime Four: Error handling relies on return codes.** Layer upon layer of if statements, nested eighteen levels deep; goto cleanup is everywhere, and the ban on goto is nothing more than a formality.

**Crime Five: Macros are the source of all evil.** #define masquerades as functions but performs text replacement; side effects, operator precedence, and repeated inclusion have killed countless heroes; #ifdef nesting is like a maze, and cross-platform compilation is like a trial.

**Crime Six: Infrastructure is all built manually.** Manually crafting linked lists, creating hash tables, writing string splitting functions repeatedly... programmers spend half their time building wheels and the other half debugging them.

The old order has perished! Now there is Modern C++, which has risen since C++11, and has experienced five dynasties of revival through C++14, C++17, C++20, and C++23, striving for excellence and a new era:

- **RAII as the foundation of the state**: Resource acquisition is initialization, stack objects are automatically destructed, and the threat of leaks is eliminated forever;
- **The Three Greats of Smart Pointers**: unique_ptr exclusively owns the resource, shared_ptr shares the count, and weak_ptr resolves cyclic references — naked pointers can be sent to the museum for retirement;
- **A thousand armies of containers**: string, vector, map, unordered_map... with a single command from STL, ten thousand troops march forth — why bother crafting data structures by hand?
- **auto and lambda**: type deduction eliminates the need for lengthy signatures, closures accompany you, and callbacks come with context;
- **A new era of type safety**: optional defeats null, variant defines the harmony of types, and Concepts constrain templates at compile time;
- **Performance is not left behind**: move semantics claim all resources, constexpr calculates everything at compile time, and std::sort's inlining optimization outperforms qsort's function pointers;
- **Modules pacify the chaos of #include**: cyclic dependencies and the drudgery of forward declarations are all eliminated in one fell swoop!

Therefore, it is said: "Pointers On C" by Kenneth Reek is a classic, and in the past was indeed a good teacher; however, what is taught in the book are all the techniques of a handcrafted workshop — manually controlling memory, macro tricks with pointers, and crafting containers by hand. Just like practicing martial arts with horse stance, although it is the foundation, today's battlefield is already the era of firearms.

Now, everyone has a copy of "Effective Modern C++", builds the foundation with "C++ Primer", carries "cppreference" as a field manual, and deeply understands RAII, smart pointers, move semantics, and concurrency — this is the correct path!

Alas! In the past, we manually wrote strlen to inspect memory, and spent the whole night with gdb chasing dangling pointers; today, we have thousands of compile-time assertions, and Concepts detect errors before they occur. This is truly the golden age for programmers!

The old order has perished, and Modern C++ shall rise; in the year of Bing Wu, the world is in great harmony!

---

*Note One: In fields such as kernel drivers, embedded systems, and microcontrollers, C is still an unshakable cornerstone; C and C++ share a common origin, and why are they so eager to fight each other?*

*Note Two: C++ compiler errors often start from three pages, and template errors are even more like ancient texts — this is the tax of the new dynasty, and all of you must endure it.*

*Note Three: Although "Pointers On Modern CPP" has not yet been realized, my heart yearns for it, and I hope some great person will write it soon.*
