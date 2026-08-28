---
title: "Crash Lab: The Error Is Here, the Crash Is There"
description: "A C++ crash casebook—every case ships code that deliberately crashes for you, a fixed version, and a debugging log that drags the culprit into the open"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
---

# Crash Lab: The Error Is Here, the Crash Is There

Hey! Welcome to my little side project. The idea came to me while I was hunting a submodule crash at work and nearly broke down myself: it hit me that there doesn't seem to be a dedicated collection out there—really, a dedicated one—for walking through crash investigations, case by case. We keep saying—hey friend, fire up that damn GDB of yours and come stare with me at a stack trace with no head and no tail, no clue which damned place it blew up in. And that's supposed to be crash debugging.

The causes, to be honest, run the whole range: from the dead simple—your own brain lapse, touching a freed object—to callbacks hitting invalidated objects, timed gaps exposing a null pointer, or some mysterious moment where a freed object gets touched while every other reference was never nulled... whatever, these are all everyday C++ crash causes. I've seen them all.

What I hope this series does is share the crash types I've run into as an ordinary, pass-by C++ developer in real work, so that later—whether you write C or C++—the code you ship crashes a bit less.

PS: Written by CharlieChen114514, in the small hours, after days of overtime wrestling with dogshit crashes.

## Categories Already Open

First to open its doors is [Memory Safety](/en/crash-lab/a-memory-safety/), currently holding two cases:

- [01 · Null Pointer Dereference: The Crash That Hides Nothing](/en/crash-lab/a-memory-safety/01-null-deref)
- [02 · Use-After-Free: The Pointer Outlives the Memory](/en/crash-lab/a-memory-safety/02-use-after-free)

Every case ships with code you can compile and run yourself, under `code/volumn_codes/crash-lab/` in the repo: a deliberately broken `crash.cpp` and a fixed `fixed.cpp`—clone it, `cmake -B build && cmake --build build`, and run. The other categories—arithmetic overflow, iterator invalidation, data races and that batch—are still being moved in; one batch lands, one batch lights up.
