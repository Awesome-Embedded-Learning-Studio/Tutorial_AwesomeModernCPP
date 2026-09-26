---
title: "Why 99% of C++ Microbenchmarks Lie"
description: "CppCon 2025 talk notes — Kris Jusiak on the compiler optimization, noise, bias, branch prediction, and correlation traps hiding inside microbenchmarks, with local GCC 16.1.1 measurements"
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
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/07-microbenchmarks-that-lie/index.md
  source_hash: ea04ecb86940f395ce24b8e5f423494a6d2d07a80417e57a7485761710a54c44
  translated_at: '2026-09-26T00:23:26+00:00'
  engine: anthropic
  token_count: 1000
---

<TalkInfoCard
  talkTitle="Why 99% of C++ Microbenchmarks Lie – and How to Write the 1% that Matter!"
  speaker="Kris Jusiak"
  conference="cppcon"
  :year="2025"
  videoYoutube="https://www.youtube.com/watch?v=s_cWIeo9r4I"
/>

These are notes from Kris Jusiak's CppCon 2025 talk. Kris is the author of [Boost].UT and has spent years tinkering with compile-time computation and testing frameworks. This time he locks onto a question that sends your blood pressure through the roof: you write a benchmark, it prints a beautiful nanosecond number, you optimize against it, merge the code, ship — and in production nothing moves, or things even get slower. Kris's answer stings: that benchmark of yours is, with high probability, lying — and not in just one place, but in several spots chained together.

The notes are split into five parts, in the order of exposing the liar layer by layer: first how the compiler optimizes the very loop you wanted to measure out of existence; then noise and bias, two kinds of error with completely different natures; next how the branch predictor and the cache conspire to hand you a fake number; then latency versus throughput, two dimensions that constantly get conflated; and finally the deadliest one of all — a faster microbenchmark and a faster whole program are simply not the same thing.

::: warning About the local environment
Every experiment in this series was run on the same machine: **Arch Linux / WSL2, AMD Ryzen 7 9700X (Zen 5 architecture), GCC 16.1.1, `-std=c++20`**. Nothing is pinned, no clocks are locked, no background is silenced — it is just a WSL2 environment running loose. That means the noise is larger than on a properly tuned setup, but it happens to make "what noise looks like" easier to see. The numbers on your machine will differ; the direction of the conclusions will not.
:::

## Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-compiler-ate-your-benchmark">The Compiler Lied to You: The #1 Microbenchmark Lie</ChapterLink>
  <ChapterLink href="02-noise-vs-bias">Noise You Can Suppress — Bias Is the Nightmare</ChapterLink>
  <ChapterLink href="03-branch-prediction-cheats">The Branch Predictor Is Helping You Cheat</ChapterLink>
  <ChapterLink href="04-latency-throughput-cycles">Latency, Throughput, Cycles: What Are You Actually Measuring</ChapterLink>
  <ChapterLink href="05-correlation-and-discipline">A Faster Microbenchmark Is Not a Faster Program</ChapterLink>
</ChapterNav>
