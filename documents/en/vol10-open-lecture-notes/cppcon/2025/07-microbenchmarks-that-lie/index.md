---
title: "Why 99% of C++ Microbenchmarks Lie"
description: "CppCon 2025 notes — Kris Jusiak on the compiler-optimization, noise, bias, branch-prediction and correlation traps in microbenchmarking, with GCC 16.1.1 measurements run locally"
conference: cppcon
conference_year: 2025
talk_title: 'Why 99% of C++ Microbenchmarks Lie – and How to Write the 1% that Matter!'
speaker: "Kris Jusiak"
tags:
  - cpp-modern
  - host
  - intermediate
  - 优化
difficulty: intermediate
platform: host
cpp_standard: [20]
---

<TalkInfoCard
  talkTitle="Why 99% of C++ Microbenchmarks Lie – and How to Write the 1% that Matter!"
  speaker="Kris Jusiak"
  conference="cppcon"
  :year="2025"
  videoYoutube="https://www.youtube.com/watch?v=s_cWIeo9r4I"
/>

These are notes from Kris Jusiak's CppCon 2025 talk. Kris is the author of [Boost].UT and has long worked on compile-time computation and testing frameworks. The talk zeroes in on a frustrating question: you write a benchmark, get a beautiful nanosecond number, optimize against it, ship — and production is unchanged, or even slower. Kris's answer is blunt: your benchmark is almost certainly lying, and not in one place — several lies chained together.

The notes are split into five parts, peeling liars off layer by layer: first the compiler deleting your loop entirely, then noise and bias as two fundamentally different kinds of error, then the branch predictor and cache conspiring to flatter your numbers, then latency vs. throughput as two dimensions people conflate, and finally the cruelest one — a faster microbenchmark is not the same as a faster program.

::: warning About the local environment
All experiments in this series were run on the same machine: **Arch Linux / WSL2, AMD Ryzen 7 9700X (Zen 5), GCC 16.1.1, `-std=c++20`**. This box has no CPU pinning, no locked frequency, no quieted background — it is a plain WSL2 setup, which means noise is larger than on a properly tuned machine. That happens to make "what does noise look like" easier to see. Your numbers will differ; the direction of the conclusions will not.
:::

## Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-compiler-ate-your-benchmark">The Compiler Lied to You: The #1 Microbenchmark Lie</ChapterLink>
  <ChapterLink href="02-noise-vs-bias">Noise You Can Suppress — Bias Is the Nightmare</ChapterLink>
  <ChapterLink href="03-branch-prediction-cheats">The Branch Predictor Is Helping You Cheat</ChapterLink>
  <ChapterLink href="04-latency-throughput-cycles">Latency, Throughput, Cycles: What Are You Actually Measuring</ChapterLink>
  <ChapterLink href="05-correlation-and-discipline">A Faster Microbenchmark Is Not a Faster Program</ChapterLink>
</ChapterNav>
