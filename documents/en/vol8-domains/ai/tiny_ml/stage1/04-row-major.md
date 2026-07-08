---
title: "Row-major — how a 2D coordinate lands in 1D memory"
description: "Tear apart the math behind operator()(i,j): a 2D coordinate lands in 1D memory as i*Cols+j, one row at a time. This order matches NumPy's default C order and C++ native arrays — the foundation of the Stage 6 diff."
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
---

# Row-major — how a 2D coordinate lands in 1D memory

[The previous piece](./03-why-not-built-in.md) set the direction: storage is a flat `std::array<float, Rows*Cols>`, with an `operator()(i, j)` shell on top doing coordinate conversion. This piece tears that conversion apart: **given a 2D coordinate (i, j), how do you compute its position in that 1D memory?**

You might think, it's just an index calculation, how hard can it be. It's not hard, but it has a name you need to meet first: row-major. The name sounds intimidating, but at bottom it's just a convention about "what order to lay things out in". Get this convention wrong and your NumPy diff blows up in your face later.

## Copy it down one row at a time

First make the problem concrete. Say there's a 2-row, 3-column matrix; logically you see a 2D table:

```text
        col0   col1   col2
row0  [  a00    a01    a02  ]
row1  [  a10    a11    a12  ]
```

But memory is one-dimensional — a single line, it doesn't know rows from columns. How does this table fit into a line?

The most natural approach, and what you'd do hand-copying a table: **copy the first row end to end, then the second row.** So in memory it looks like this:

```text
index:    0     1     2     3     4     5
      [ a00,  a01,  a02,  a10,  a11,  a12 ]
        └──── row 0 ────┘ └──── row 1 ────┘
```

That's row-major — in plain words, "row-first": finish one row, then the next. `a00` lands at index 0, `a02` at index 2, `a10` right after `a02` at index 3, because it's at the very start of the second row and has to wait for the three numbers of the first row to settle before it gets its turn.

## The formula: i * Cols + j

Picture in hand, now write the conversion. Given coordinate (i, j), count how many slots are already taken ahead of it.

Ahead of row i, i full rows are already packed in, each Cols slots, so that's `i * Cols` slots. Inside row i, you walk right another j slots to reach column j. Add both ends:

```text
flat_index = i * Cols + j
```

That's the whole line. The entire secret of `operator()(i, j)` is right here: `return data_[i * Cols + j];`.

Verify it with our Lab's `Tensor<4, 3>` — Rows=4, Cols=3. Accessing `t(2, 1)`, i.e. row 2, column 1: ahead of it sit 2 full rows, 3 slots each, 6 slots total; then inside row 2 walk right 1 more slot, add 1, landing at index 7. So `t(2, 1)` is `data_[7]`. Count against the picture above — index 7 does sit at row 2, column 1 (counting from 0), checks out.

## Why row-major and not column-major

There's more than one way to store it. You could copy column by column too — fill all rows of column 0, then column 1 — that's column-major, with the formula `j * Rows + i`. Fortran, MATLAB, and some BLAS implementations are column-major; the old hands of scientific computing use it a lot.

So why did we pick row-major? Two reasons, both hard.

First, C and C++ native 2D arrays are row-major to begin with. `float w[4][3]` in memory is `w[0][0], w[0][1], w[0][2], w[1][0], ...`, one row after another. C++ programmers know this order best — no mental transposition needed.

Second, and more importantly, NumPy's default C order is row-major. `np.array([[1,2,3],[4,5,6]])` with a `.flatten()` call yields `[1,2,3,4,5,6]`, identical to our `data_[i*Cols+j]` order. That means when Stage 5 exports weights from NumPy, Python's `W[i, j]` and C++'s `t(i, j)` point at the same number, and the Stage 6 diff can compare digit by digit.

If either side sneakily used the other order, the diff would be wrong end to end — you'd take C++'s row-major `data_[7]` to compare against some position in NumPy's column-major layout, and nothing would match, debug-into-doubt-your-life. So this layout gets nailed down right here, and every later stage is forbidden from changing its mind.

## What to take away

Row-major is just this much. Don't let its simplicity fool you — it's the alignment foundation of the whole Lab; whether Python and C++ can match up hinges entirely on it. Remember two things: a 2D coordinate (i, j) sits in 1D memory at `i * Cols + j`, one row filled before the next; and this order matches NumPy's default and C++ native arrays.

[The next piece](./05-shape-in-type.md) is about dimensions: why Rows and Cols go into the template parameters — baked into the type — instead of being passed into a constructor.
