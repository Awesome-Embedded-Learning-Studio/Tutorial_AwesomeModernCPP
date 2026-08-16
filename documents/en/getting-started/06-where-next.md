---
title: "It Works — So Where Next"
description: "The getting-started volume is done. Pick your next step by goal: learn the syntax, dig into CMake, dig into compiling and linking, or go embedded — each path points to a specific volume"
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
---

# It Works — So Where Next

By this point you've worked through everything the getting-started volume set out to do: part 2 got vscode, a compiler, and CMake installed; part 3 ran your first hello inside vscode; part 4 grew the project into multiple files and put CMake to real use for the first time; part 5 made vscode actually understand your code (click a function and jump to it, make the red squiggles go away). A C++ environment now sits in front of you — it compiles, it autocompletes, it jumps to definitions. That's everything this volume owed you, done.

Where to go next depends on what you're after. Four roads are laid out below. Pick the one that matches what you have in mind and walk down it.

## If you want to nail down C++ first

The getting-started volume sorted out "the environment runs." It never touched a single line of real C++ syntax — what a variable is, how to write a loop, how to define a function, what a class is, we haven't said a word about any of that. That's the real capital you spend to write C++, and it's the foundation every later volume stands on.

Your next stop is [Volume 1 · Fundamentals](/vol1-fundamentals/), going from C++'s most basic syntax all the way up to object orientation and templates. This volume is the main line. Whatever direction you end up going, you can't get around it. Grind through Volume 1 first, then talk about the rest.

## If you want to understand CMake and build systems

In the getting-started volume you only learned "copy a CMakeLists, click the button, it runs." What CMake is actually doing behind the scenes, why there are two steps called "configure" and "generate," why the word `target` shows up everywhere, what `add_executable` and `target_link_libraries` are each responsible for — none of that got unpacked.

For the answers, head to [Volume 7 · Engineering Practice](/vol7-engineering/). That's where the CMake material lives, going from a single target up to multi-module organization and how to pull in external dependencies. One word of warning, though: Volume 7 assumes you already know basic C++ syntax, so even if engineering is what you really want, run through Volume 1 first — otherwise you'll get stuck partway in.

## If you want to understand compiling and linking

You may have already run into a few odd things in part 4: you only changed one file, so why does CMake rebuild just that one and leave the others alone; every so often an `undefined reference` pops up and the error looks terrifying; people also chat about static libraries and dynamic libraries as if they were two completely different things. Underneath all of it runs the same machinery — compiling and linking.

To get that machinery straight, go to [Compilation and Linking, In Depth](/compilation/). It walks from "what the compiler turns a `.cpp` into" to "how the linker stitches a pile of fragments into an `.exe`," and makes the difference between static and dynamic libraries, and exactly which step an `undefined reference` gets stuck on, all clear. It goes fairly deep, so newcomers are advised to grind through Volume 1 first — otherwise it'll scare you off.

## If you want to do embedded, program microcontrollers

A lot of folks come here for embedded — they want their code running on a chip the size of a fingernail like an STM32, lighting LEDs, reading sensors, driving motors. Honest talk: most embedded work out there is done in C, not C++. But modern C++ has a place in embedded too, with its own payoffs (type safety, zero-overhead abstraction, RAII for resource management), and this tutorial's embedded track takes the C++ route, in [Volume 8 · Domains](/vol8-domains/).

But the embedded track has a real threshold: you need C++ syntax first (Volume 1), plus some grasp of building and toolchains (the cross-compiling part of Volume 7), and the resources on a chip are tight and finicky. So the prerequisite is to lay the groundwork from Volume 1 through Volume 7 first. Don't dive straight into the chip, or you'll get stuck hanging in midair.

## The getting-started volume drops you off here

The getting-started volume walks you up to the great door of C++, presses the key into your hand, and points at the door. The real C++ journey starts in [Volume 1](/vol1-fundamentals/).
