---
title: "What is a Tensor — take the name off its pedestal"
description: "Demystified: in our Lab, a Tensor is just a fixed-size 2D table of floats — a contiguous std::array underneath, a 2D-access shell around it. Nothing mysterious."
chapter: 8
order: 7
platform: host
difficulty: beginner
cpp_standard: [23]
reading_time_minutes: 4
related:
  - "The fixed-dimension Tensor — the inference engine's data foundation"
  - "What a Tensor holds in a neural network — four kinds of data, one container"
tags:
  - host
  - cpp-modern
  - beginner
  - 基础
translation:
  source: documents/vol8-domains/ai/tiny_ml/stage1/01-what-is-tensor.md
  source_hash: a4d1c24106c74ed1839df6af06bf5bfb1fdec2836cc6193ec7b5da16584c961d
  translated_at: '2026-09-26T03:49:25+00:00'
  engine: anthropic
  token_count: 2100
---

# What is a Tensor — take the name off its pedestal

Oh, you mean Tensor? We looked around, and maybe the simple answer is — an N-dimensional array.

Stop, stop, stop! What do you mean, N-dimensional array? Don't make your Tensor laugh.

Alright — math does have a thing called a "tensor", and it comes with a scary-looking progression. Allow us to flip through our linear algebra textbook for a moment: zero dimensions is a scalar (a single number), one dimension is a vector (a column of numbers), two dimensions is a matrix (a table of numbers), and only from three dimensions up is it officially called a tensor. So strictly speaking, a two-dimensional thing should be called a matrix, not a tensor.

But we've noticed the deep learning community doesn't stand on that kind of ceremony. PyTorch and TensorFlow call every "multi-dimensional array" a Tensor, no matter how many dimensions it has. Once that usage caught on, Tensor became shorthand for "the container that holds the numbers in a neural network". Our Lab keeps the word purely because it's the most widely spoken industry vocabulary — when you read other materials or pick up other frameworks later, the terms will line up.

## What it actually is, in our Lab

Tear off all the halos, and a Tensor is just a **fixed-size 2D table**, with a float in every cell.

What, that takes a bit of effort to swallow? Hmm — maybe you need to go back and shore up the earlier basics first (I mean volume one of this project). We find it easiest to picture as an Excel sheet: Rows rows, Cols columns, one floating-point number per cell. Want to look at row 2, column 3? Just locate that cell. That's the whole thing. If Rows=1, it degenerates into a single row — that's a vector, and as we'll see later, inputs, biases, and outputs all take this "one row" form.

Peel it down to the bottom layer, and it's a contiguous array of floats, `Rows * Cols` long, lined up obediently in a single row in memory. The Tensor layer wrapped around it is, in essence, a shell: you write `t(i, j)` to access row i, column j, and it converts that into "which number along this line", then fetches it for you. The conversion rule (row-major) is the business of [04](./04-row-major.md), so we won't expand on it here. For now, all you need to accept is one picture: **two-dimensional logical coordinates on top, one-dimensional contiguous storage underneath, joined in the middle by a conversion formula.**

So "building a Tensor", put plainly, means building that shell: a fixed-size contiguous storage, plus a set of 2D-access syntax on top. For fixed-size contiguous storage, the C++ side has a natural match in `std::array<float, Rows*Cols>`; the remaining work is wiring the conversion into `operator()(i, j)`. That's exactly what the second half of Stage 1 does.

## Then why not just use a ready-made 2D array

At this point, the C and C++ veterans burst out laughing: Hah? You're kidding me — we've always had the 2D array `float[4][3]`, and if you call that not modern enough, we have a second line of defense: `std::array<std::array<float, 3>, 4>`. Can't we just use those directly? Do we really have to build our own Tensor? We'd say you just love reinventing wheels.

Stop — our answer is: you can use them, but they're awkward to work with, and they don't line up with the hard constraints we'll hold ourselves to later (no heap allocation, diffing against NumPy, shape visible at compile time). Exactly where the awkwardness lies and how the misalignment shows up is what [03](./03-why-not-built-in.md) takes apart, piece by piece.

Leave this piece holding one picture: **a Tensor is a fixed-size 2D table, with a contiguous float array underneath and a 2D-access shell wrapped around it.** With that in hand, in the [next piece](./02-tensor-in-neural-network.md) we'll look at what a Tensor actually holds inside a neural network — what numbers sit in each of those cells.
