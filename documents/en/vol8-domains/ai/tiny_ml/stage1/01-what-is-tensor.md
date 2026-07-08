---
title: "What is a Tensor — take the name off its pedestal"
description: "Demystify: in our Lab a Tensor is just a fixed-size 2D table of floats, backed by a contiguous std::array with a 2D-access shell around it. Nothing mystical."
chapter: 8
order: 7
platform: host
difficulty: beginner
cpp_standard: [23]
reading_time_minutes: 4
related:
  - "The fixed-dimension Tensor — the inference engine's data foundation"
  - "What a Tensor holds in a neural network"
tags:
  - host
  - cpp-modern
  - beginner
  - 基础
---

# What is a Tensor — take the name off its pedestal

Oh, you mean Tensor? I asked around, and maybe we can just say — an N-dimensional array.

Hold on, hold on! What "N-dimensional array"? Don't make your Tensor laugh.

Alright, in math there really is such a thing as a "tensor", and it comes with a scary progression — allow me to flip through my linear-algebra textbook: zero-dimensional is a scalar (one number), one-dimensional is a vector (a column of numbers), two-dimensional is a matrix (a table of numbers), and only from three dimensions up is it officially called a tensor. So strictly speaking, a two-dimensional thing should be called a matrix, not a tensor.

But I noticed the deep-learning crowd isn't nearly that pedantic. PyTorch and TensorFlow call any "multi-dimensional array" a Tensor, regardless of how many dimensions. Once that usage caught on, "Tensor" became the generic term for "the container that holds numbers in a neural network." Our Lab keeps the word purely because it's the most common industry vocabulary — when you read other materials or other frameworks later, it'll match up.

## What it actually is, in our Lab

Strip away all the halo, and a Tensor is just a **fixed-size 2D table**, with a float in every cell.

Huh — if that's a bit of a stretch for you, hmm, maybe you need to go back to the basics (I mean volume one of this project) and shore them up. I find it helpful to think of it as an Excel sheet: `Rows` rows, `Cols` columns, one float per cell. Want to look at row 2, column 3? Just locate that cell. That's the whole thing. If `Rows = 1`, it collapses into a single row — that's a vector, and as we'll see, inputs, biases, and outputs all take this "one row" form.

Strip it down to the bottom and it's just a contiguous array of floats of length `Rows * Cols`, laid out obediently in a single line in memory. The Tensor shell wrapped around it is, in essence, a wrapper: you write `t(i, j)` to access row i, column j, and it converts that into "which position along this line", then fetches it for you. The conversion rule (row-major) is the business of [04](./04-row-major.md), so we won't expand on it here. For now, just hold one picture in mind: **two-dimensional logical coordinates, a one-dimensional contiguous storage underneath, connected by a conversion formula.**

So "build a Tensor", put plainly, means building that shell: a fixed-size contiguous storage, plus a 2D-access syntax around it. A fixed-size contiguous storage maps naturally to `std::array<float, Rows*Cols>` on the C++ side; the rest of the work is wiring the conversion into `operator()(i, j)`. The second half of Stage 1 does exactly that.

## Then why not just use a ready-made 2D array

At this point the C and C++ veterans burst out laughing: Hah? You're kidding me — we've *always* had the 2D array `float[4][3]`, and if you say it's not modern enough, we've got a second line of defense: `std::array<std::array<float, 3>, 4>`. Can't we just use those directly? Do we have to roll our own Tensor? I'd say you just love reinventing wheels.

Stop. My answer is: you *could* use them, but they're awkward to work with, and they don't line up with the hard constraints we'll keep later (no heap, diff against NumPy, shape visible at compile time). Exactly where they're awkward and how they don't line up is what [03](./03-why-not-built-in.md) takes apart.

Leave this piece with one picture: **a Tensor is a fixed-size 2D table, with a contiguous float array underneath, wrapped in a 2D-access shell.** Hold onto that, and in the [next piece](./02-tensor-in-neural-network.md) we'll see what a Tensor actually holds inside a neural network — what numbers sit in those cells.
