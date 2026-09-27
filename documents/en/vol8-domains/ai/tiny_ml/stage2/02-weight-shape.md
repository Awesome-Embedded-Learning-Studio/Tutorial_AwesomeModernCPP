---
title: "Why weights are [Out, In] — the cache ledger under row-major"
description: "Why Dense weights are [Out, In] and not [In, Out]: computing output o reads row o of W, and under row-major a row is contiguous and cache-friendly, while the other way around you read a column and fetch with a stride. Includes a runnable online 1024x1024 cache measurement (column order ~7-9x slower than row order), plus alignment with PyTorch nn.Linear's weight (out, in) so the Stage 5 diff is zero-friction."
chapter: 8
order: 14
platform: host
difficulty: intermediate
cpp_standard: [23]
reading_time_minutes: 7
prerequisites:
  - "Row-major — how a 2D coordinate lands in 1D memory"
  - "What Dense computes — one multiply-add, broken down per output"
related:
  - "The Dense layer — span views and weight layout"
  - "The fixed-dimension Tensor — the inference engine's data foundation"
tags:
  - host
  - cpp-modern
  - intermediate
  - 内存管理
translation:
  source: documents/vol8-domains/ai/tiny_ml/stage2/02-weight-shape.md
  source_hash: 226d40eba1c1354cbfc46445be0e2d789e6584bc48485810b5b18fc3298eb9c1
  translated_at: '2026-09-25T08:48:33+00:00'
  engine: anthropic
  token_count: 4500
---

# Why weights are [Out, In] — the cache ledger under row-major

While [Part 01](./01-what-is-dense.md) was tearing apart the Dense formula, one line kept repeating: "to compute output o, use row o of the weights." Which raises a question: why should that weight table be Out rows by In columns (`[Out, In]`) and not the other way around, In rows by Out columns (`[In, Out]`)?

You might think it's just an arrangement — mathematically a transpose patches either choice, so what difference does it make? The math does come out identical (a transpose can undo it), but how the numbers sit in memory directly decides whether the cache cooperates while you compute, and whether the Stage 5 diff against NumPy lines up. We are nailing this line down here, and no changing our minds later — same spirit as when [Stage 1 Part 04](../stage1/04-row-major.md) nailed down row-major.

## First, a refresher: how row-major lays things out

[Stage 1 Part 04](../stage1/04-row-major.md) settled it: our 2D Tensor is row-major — fill row 0 completely, then row 1, one row after another. A weight matrix W of type `Tensor<Out, In>` is a single line in memory that looks like this:

```text
index:  0       1       ...  In-1     In      In+1   ...
      [ W[0,0]  W[0,1]  ... W[0,In-1] W[1,0]  W[1,1] ... ]
        └──── row 0 (the recipe for output 0) ───┘ └─ row 1 ─┘
```

W[o, i] lands at index `o * In + i`. The row-o group — the recipe used to compute output o — sits between indices `o*In` and `o*In + In - 1`: **packed together in memory, contiguous**.

## Computing one output reads exactly one row, in order

Here is the formula from [Part 01](./01-what-is-dense.md) again:

```text
y[o] = Σ W[o,i]·x[i]   (i from 0 to In-1)
```

Computing output o means reading that entire row — `W[o,0]`, `W[o,1]`, ..., `W[o,In-1]` — multiplying each element against the input and summing. And that row, as we just said, sits contiguously in memory.

The CPU does not read memory one number at a time; it hauls it into the cache in cache-line chunks (64 bytes on this machine, 16 floats). When you read `W[o,0]`, `W[o,1]`, `W[o,2]`, ... have already been dragged into the cache along with it, so every later fetch is a cache hit — blazing fast. That is the dividend that row-major + `[Out, In]` delivers to your door: the inner loop for each output walks straight along a row, and the cache cooperates the whole way.

## What happens the other way around, [In, Out]

Suppose we had picked `[In, Out]` instead (In rows, Out columns). Now the recipe for output o is scattered across **column o**: `W[0,o]`, `W[1,o]`, ..., `W[In-1,o]`. Reading a column under row-major means consecutive elements are separated by a full row (stride = Out floats). In other words, after fetching `W[0,o]`, the next element you need, `W[1,o]`, is Out numbers away — most likely not in the same cache line. Every single fetch drags a fresh chunk over from memory and uses exactly one number out of it. Cache utilization is a horror show.

Mathematically the two layouts can produce the same result (it is just a transpose), but at runtime the speed can differ by multiples. Once the matrix gets big, that gap is very real.

## Really? Let's run it

Talk is cheap. Take a 1024×1024 matrix, accumulate it once traversing in row order (`[r,c]` with the inner loop walking c) and once in column order (`[r,c]` with the inner loop walking r, jumping across rows), and time both (`-O2`, median of 5 runs). The code is short — open the demo below and run it yourself:

<OnlineCompilerDemo
  title="The cache difference: row-major vs column-major"
  source-path="code/examples/vol8/cache_layout.cpp"
  description="Timing comparison of row-order vs column-order traversal over a 1024x1024 matrix — hit run to execute on the Compiler Explorer cloud"
  allow-run
  run-compiler="g152"
  run-options="-O2 -std=c++23"
/>

Click "动手试一试" ("try it yourself"), then "运行" ("run") — after a few seconds the results appear. The numbers pasted below are from our local WSL2; measured runs on Compiler Explorer's cloud servers go past 9x, and your machine will produce yet another number — but the trend "row order is far faster than column order" always holds:

```text
N=1024, -O2, 5 次取中位数:
  行序(row-major): 0.39 ms
  列序(col-major): 2.83 ms
  列序 / 行序 = 7.2 倍
```

More than 7x apart. Credit in that number goes partly to the cache (row order hits, column order misses) and partly to the compiler's auto-vectorization (row loops vectorize nicely, column loops resist it), but the bulk is cache. The 2×2 and 2×3 matrices in our Lab tests cannot feel any of this — but once a layout is set, every later stage marches to it. Pick wrong now, and it comes back to bite you at Stage 7's embedded review and Stage 8 on the MCU (don't ask — AAAH! pain, pain, pain, pain).

## One more reason: lining up with PyTorch

Cache friendliness alone is not enough — the weight layout carries another job: Stage 5 has to export trained weights from NumPy/PyTorch into C++. If our arrangement differs from theirs, the export has to transpose and reshuffle, which is fertile ground for mistakes.

As luck would have it, the industry's de facto standard is exactly `[Out, In]`. PyTorch's `torch.nn.Linear(in_features, out_features)` stores its `weight` with shape `(out_features, in_features)`, and the official docs spell the forward computation as `y = xWᵀ + b` (see [the PyTorch nn.Linear docs](https://docs.pytorch.org/docs/stable/generated/torch.nn.Linear.html)). Unroll that for a single sample: `y[o] = Σ_i x[i]·Wᵀ[i,o] = Σ_i x[i]·W[o,i]` — identical to our formula from [Part 01](./01-what-is-dense.md). In other words, PyTorch's `weight[o, i]` and our `W[o, i]` point at the same number, lying at the same spot in memory. At Stage 5's export, the order Python's `W.flatten()` produces lines up digit for digit with our `weight_[o*In + i]` — no transposing anywhere.

That is no coincidence. When we chose `[Out, In]`, we were aiming to get two things done at once: cache-friendly + aligned with the industry. Two reasons pointing at the same choice — that is a bargain.

## What to take away

Weights go `[Out, In]`, standing on two legs: first, computing each output reads exactly one row in order, contiguous under row-major, so the cache cooperates (measured on a 1024×1024 matrix, row order beats column order by 7x); second, alignment with PyTorch `nn.Linear`'s `(out, in)` convention means zero friction at the Stage 5 diff. The reverse `[In, Out]` loses on both counts.

With the layout settled, we can finally start writing Dense. But before that, one storage-strategy trade-off needs a verdict: should `Dense` store its own copy of the weight numbers, or just hold a view pointing at them? It looks like a small thing, but it tugs on v0.1's no-heap-allocation hard constraint and on Stage 5's weight export — it even decides whether Stage 5 will have to come back and change the interface. [Part 03](./03-dense.md) tears that question apart, and along the way lays out Dense's full interface.
