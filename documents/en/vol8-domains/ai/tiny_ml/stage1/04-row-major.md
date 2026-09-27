---
title: "Row-major — how a 2D coordinate lands in 1D memory"
description: "Taking apart the arithmetic behind operator()(i, j): a 2D coordinate lands in 1D memory at i*Cols+j, one full row stored before the next; this order matches both NumPy's default C order and C++ native arrays, and it is what the Stage 5 diff is built on"
chapter: 8
order: 10
platform: host
difficulty: intermediate
cpp_standard: [23]
reading_time_minutes: 5
prerequisites:
  - "Why not use what's already there — three suspects on trial"
related:
  - "The fixed-dimension Tensor — the inference engine's data foundation"
  - "Shape baked into the type — why dimensions are template parameters"
tags:
  - host
  - cpp-modern
  - intermediate
  - 内存管理
translation:
  source: documents/vol8-domains/ai/tiny_ml/stage1/04-row-major.md
  source_hash: 34480ab9db77b6f0f1b30d07ed5591878232180018e0f26a53fd58837ccb040d
  translated_at: '2026-09-26T04:09:11+00:00'
  engine: anthropic
  token_count: 900
---

# Row-major — how a 2D coordinate lands in 1D memory

[The previous piece](./03-why-not-built-in.md) settled the direction: storage is one flat `std::array<float, Rows*Cols>`, wrapped in an `operator()(i, j)` that maps coordinates to positions. This piece takes that mapping apart: **given a 2D coordinate (i, j), how do you compute where it lands in that one-dimensional stretch of memory?**

You might be thinking, it's just an index computation, how hard can it be. Hard it isn't — but it has a name you need to know first: row-major. The name sounds intimidating, yet boiled down it's just a convention about "in what order things get laid out". Get this convention straight, though, or the NumPy diff later on will blow up in no time.

## Copy it one row at a time

First let's make the problem concrete. Say we have a matrix with 2 rows and 3 columns; logically, what you see is a 2D table:

```text
        col0   col1   col2
row0  [  a00    a01    a02  ]
row1  [  a10    a11    a12  ]
```

But memory is one-dimensional — a single line that recognizes no rows and no columns. How do you cram this table into one line?

The most natural way — and exactly what you'd do transcribing a table by hand — is: **copy the first row from head to tail, then the second row**. So in memory it ends up like this:

```text
Index:     0     1     2     3     4     5
      [ a00,  a01,  a02,  a10,  a11,  a12 ]
        └──── row 0 ────┘ └──── row 1 ────┘
```

That is row-major, which in plain words means "rows first": finish storing one row, then move on to the next. `a00` lands at index 0, `a02` lands at index 2, and `a10` takes index 3, right after `a02` — it's the head of the second row, so it has to wait until all three numbers of the first row have settled into place.

## The formula: i * Cols + j

With the picture in hand, now we write the mapping. Given a coordinate (i, j), count how many slots are already taken in front of it.

Ahead of row i sit i full rows, Cols slots each, so that's `i * Cols` slots taken. Once inside row i, you still walk j slots to the right to reach column j. Add the two ends together:

```text
flat_index = i * Cols + j
```

That one line is it. The whole secret of `operator()(i, j)` is right here: `return data_[i * Cols + j];`.

Check it against the Lab's `Tensor<4, 3>`: Rows=4, Cols=3. Accessing `t(2, 1)` — that is, row 2, column 1 — there are 2 full rows pressing ahead of it, 3 slots each, 6 slots in all; then inside row 2 you walk 1 slot to the right, add 1, and land on index 7. So `t(2, 1)` is `data_[7]`. Count it off against the diagram above: the slot at index 7 really does sit in row 2, column 1 (counting from 0). It checks out.

## Why row-major and not column-major

This storage order isn't the only option. You could just as well copy column by column — fill all rows of column 0, then column 1 — which is called column-major, and the formula becomes `j * Rows + i`. Fortran, MATLAB, and a chunk of the BLAS implementations are column-major; it's what the old hands of scientific computing mostly grew up on.

So why do we insist on row-major? Two reasons, both rock solid.

First, native 2D arrays in C and C++ are row-major to begin with. In memory, `float w[4][3]` is exactly `w[0][0], w[0][1], w[0][2], w[1][0], ...` — one row after another. C++ programmers know this order best; no mental transposing required.

Second, and weightier: NumPy's default C order is row-major. Call `.flatten()` on `np.array([[1,2,3],[4,5,6]])` and out comes `[1,2,3,4,5,6]` — the exact same order as our `data_[i*Cols+j]`. That means when Stage 5 exports weights from NumPy, `W[i, j]` on the Python side and `t(i, j)` on the C++ side point at the same number, which is what lets the Stage 5 diff compare element by element.

If either side quietly used the other order, the diff would be wrong across the board — you'd be comparing C++'s row-major `data_[7]` against whatever number sits at that spot in NumPy's column-major layout, nothing would ever match, and you could debug it until you question your life choices. So right here we pin this layout down, and no later stage is allowed to change its mind.

## What to take away

That's all row-major is. Don't let its simplicity fool you — it is the alignment foundation of the whole Lab; whether the Python side and the C++ side line up rests entirely on it. Two things to remember: a 2D coordinate (i, j) lives at `i * Cols + j` in 1D memory, one full row stored before the next; and this order matches NumPy's default as well as C++ native arrays.

[The next piece](./05-shape-in-type.md) covers dimensions: why Rows and Cols, these two numbers, go into template parameters, baked into the type, instead of being passed to a constructor.
