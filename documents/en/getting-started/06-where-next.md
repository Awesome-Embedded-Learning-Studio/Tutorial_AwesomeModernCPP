---
title: "It Works — So Where Next"
description: "The getting-started volume is done. Pick your next step by goal: learn the syntax, chew through CMake, dig into compiling and linking, or go embedded — four roads, each pointing to its own volume"
chapter: 14
order: 6
platform: host
difficulty: beginner
cpp_standard: [17, 20]
tags:
  - host
  - 入门
  - 基础
  - beginner
  - 工具链
reading_time_minutes: 3
translation:
  source: documents/getting-started/06-where-next.md
  source_hash: 9323a0c60bd0063d873ad9aa9ee937a4213652a4278bdbb2f9f419b009c4dfbb
  translated_at: '2026-09-26T15:05:01+00:00'
  engine: anthropic
  token_count: 1900
---

# It Works — So Where Next

By this point you've worked through everything the getting-started volume set out to do: part 2 got vscode, the compiler, and CMake installed; part 3 ran your first hello inside vscode; part 4 grew the project into multiple files and put CMake in charge of a real project for the first time; part 5 made vscode genuinely understand your code (click a function and you jump, the red squiggles go away). A working C++ environment now sits in front of you — it compiles, it autocompletes, it jumps to definitions. That's everything this volume owed you, done.

Where to head next depends on what you're after. Four roads are laid out below — pick the one closest to what you have in mind and walk down it.

## If You Want to Nail Down C++ First

The getting-started volume fixed "the environment runs"; it never touched a single line of proper C++ syntax — what a variable is, how to write a loop, how to define a function, what a class is. We haven't said one word about any of that yet. That's the real capital you write C++ with, and it's the foundation every later volume stands on.

Your next stop is [Volume 1 · Fundamentals](/vol1-fundamentals/), which runs from C++'s most basic syntax all the way up to object orientation and templates. This volume is the main line — whatever direction you end up choosing, you can't get around it. Grind through Volume 1 first, then talk about the rest.

## If You Want to Understand CMake and Build Systems

In the getting-started volume you only learned "copy a snippet of CMakeLists, click the button, it runs." What CMake is actually doing behind the scenes, why there are two steps called "configure" and "generate", why the word `target` shows up everywhere, and which patch `add_executable` and `target_link_libraries` each cover — none of that got unpacked.

For the answers, head to [Volume 7 · Engineering Practice](/vol7-engineering/). That's where the advanced CMake material lives, going from a single target up to multi-module organization and how to pull external dependencies in. One word of warning, though: Volume 7 assumes you already know basic C++ syntax, so even if engineering is what excites you more, we suggest running through Volume 1 first — otherwise you'll get stuck reading it.

## If You Want to Understand How Compiling and Linking Work

You may have already bumped into a few odd phenomena in part 4: you clearly changed only one file, yet CMake rebuilds just that one and leaves the others untouched; every so often an `undefined reference` pops up, with an error message that looks terrifying; and people chat about static libraries versus dynamic libraries as if they were two completely different things. Underneath all of these runs the same machinery — compiling and linking.

To get this machinery straight, go to [Compilation and Linking, In Depth](/compilation/). It walks from "what the compiler translates a `.cpp` into" to "how the linker stitches a pile of fragments into an `.exe`", and covers thoroughly the difference between static and dynamic libraries and exactly which step an `undefined reference` gets stuck on. It goes fairly deep, so newcomers are advised to grind through Volume 1 first — otherwise it's easy to be scared off.

## If You Want to Do Embedded and Program Microcontrollers

Plenty of folks come here for embedded — they want their code running on a chip the size of a fingernail, like an STM32, lighting LEDs, reading sensors, driving motors. Honest talk first: most projects in the embedded world use C, not C++. But modern C++ works in embedded too and brings its own payoffs (type safety, zero-overhead abstraction, RAII for resource management), and this tutorial's embedded track takes the C++ route, in [Volume 8 · Domains](/vol8-domains/).

But the embedded track's threshold is not low: you need C++ syntax first (Volume 1), plus some grasp of builds and toolchains (the cross-compiling part of Volume 7), and the resources on a chip are tight and finicky. So the precondition is to lay the groundwork from Volume 1 through Volume 7 first. Don't dive headfirst into the chip right away, or you'll get stuck hanging in midair.

## The Getting-Started Volume Drops You Off Here

The getting-started volume walks you to the great gate of C++, presses the key into your hand, and points at the door. The real C++ journey starts in [Volume 1](/vol1-fundamentals/).
