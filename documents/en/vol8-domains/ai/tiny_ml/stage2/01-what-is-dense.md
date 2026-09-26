---
title: "What Dense computes — one multiply-add, broken down per output"
description: "Tear apart the Dense layer's y = W·x + b: each output is a weighted sum of all inputs plus one bias, so one Dense layer equals Out such weighted sums; activation waits for Stage 3 — here we only do the affine half"
chapter: 8
order: 13
platform: host
difficulty: beginner
cpp_standard: [23]
reading_time_minutes: 5
prerequisites:
  - "The fixed-dimension Tensor — the inference engine's data foundation"
  - "What a Tensor holds in a neural network — four kinds of data, one container"
related:
  - "Why weights are [Out, In] — the cache ledger under row-major"
  - "The Dense layer — span views and weight layout"
tags:
  - host
  - cpp-modern
  - beginner
  - 基础
translation:
  source: documents/vol8-domains/ai/tiny_ml/stage2/01-what-is-dense.md
  source_hash: 9dfbf349bed3c2af07f788f49a7b42cb4a9c9922130f3a6e482284ade9e492f9
  translated_at: '2026-09-25T08:48:34+00:00'
  engine: anthropic
  token_count: 1800
---

# What Dense computes — one multiply-add, broken down per output

When [Stage 1's piece 02](../stage1/02-tensor-in-neural-network.md) tore down the four kinds of Tensor sitting around a Dense layer, it deliberately left a hole: how exactly the weights and the input get multiplied together and summed. That's Stage 2's job. This piece fills that hole.

First, let's take the name Dense off its pedestal. In Chinese it's called the "fully-connected layer"; the English Dense translates literally as "dense" — both say the same thing: every output of this layer is connected to every input. "Fully connected" means "all connected" — not a single input left out. So you can guess what it does: blend all the inputs together and knead them into a few outputs.

The exact kneading recipe is a single formula:

```text
y = W·x + b
```

Seen as a whole it's a bit abstract; break it down to a single output and it becomes clear.

## Taking output 0 apart

Take the first layer of our Lab, `Dense(3, 4)`, as the example: 3 inputs (temperature, humidity, light), kneaded into 4 intermediate values. The input x is a vector of length 3, the output y a vector of length 4, and the weights W a 4×3 table — [Stage 1's piece 02](../stage1/02-tensor-in-neural-network.md) already laid out these shapes, so we won't repeat them here.

Look at just how the 0th output y[0] comes about. It multiplies each of the 3 inputs by its own coefficient, adds them up, and finally throws in one constant:

```text
y[0] = W[0,0]·x[0] + W[0,1]·x[1] + W[0,2]·x[2] + b[0]
```

Here `W[0,0]`, `W[0,1]`, `W[0,2]` are the three numbers in **row 0** of the weight table — exactly "the recipe used to compute output 0" that [Stage 1's piece 02](../stage1/02-tensor-in-neural-network.md) talked about; `b[0]` is the 0th constant of the bias, that fader that "lifts the whole thing up after mixing". Mix the three inputs each at its own ratio, add them up, then lift the whole thing — that's y[0].

The remaining y[1], y[2], y[3] are exactly the same, except each uses rows 1, 2, 3 of the weight table:

```text
y[1] = W[1,0]·x[0] + W[1,1]·x[1] + W[1,2]·x[2] + b[1]
y[2] = W[2,0]·x[0] + W[2,1]·x[1] + W[2,2]·x[2] + b[2]
y[3] = W[3,0]·x[0] + W[3,1]·x[1] + W[3,2]·x[2] + b[3]
```

The pattern is on the table now: each output corresponds to one row of the weight table; each number in that row multiplies its corresponding input, the products are added up, and the output's own bias is thrown in. One `Dense(3, 4)` is just 4 such weighted sums computed in parallel.

## In one sentence: Dense is Out weighted sums

Let's distill the pattern above. A `Dense(In, Out)` produces Out outputs; the o-th output is:

```text
y[o] = Σ W[o,i]·x[i]  +  b[o]        (i from 0 to In-1)
```

That's the whole line. Row o of the weights, `W[o,:]`, decides "at what ratio each input gets mixed in when computing output o"; the o-th bias `b[o]` decides "how much the whole thing gets lifted after mixing". Out outputs means Out sets of such recipes, each computing its own.

So a Dense layer, put plainly, is a "batch weighted-sum" machine: you hand it In inputs and an Out×In recipe table, and it hands you back Out weighted sums. What a neural network "learns" is precisely the numbers in that recipe table — during training it keeps adjusting W and b until the kneaded outputs correctly judge the device state.

## Not touching activation yet

You may have noticed that we never mentioned ReLU, or any of that "clamp negatives to zero" business. That's deliberate. A complete Dense layer in a neural network is usually two steps, "affine + activation": first `W·x + b` (the affine part — the multiply-add this piece is about), then the result goes to an activation function (we use ReLU, which zeroes out negatives).

We split those two steps apart: Stage 2 builds only the affine half — the `Dense` class takes care of `y = W·x + b` and nothing else; activation waits for Stage 3, where we build a ReLU on its own. That way each step deals with only one new concept, and the Dense implementation doesn't get muddied by activation details. So the `Dense::forward` you're about to see spits out the raw values "after affine, before activation" — negatives still in place, to be dealt with in Stage 3.

## What to take away

This piece has taken apart what Dense computes; three things are enough to remember: a Dense layer produces Out outputs, each a weighted sum of all inputs (one row of the weights times the input — multiply element by element, then add); each output then adds its own bias; and activation is not this layer's job — it waits for Stage 3.

The formula `y = W·x + b` shouldn't be a blob of abstract symbols anymore — it's shorthand for Out weighted sums. But there's a detail we glossed over: "row o" of the weight table — how does it actually sit in memory? Why must it be Out rows by In columns, and not the other way around, In rows by Out columns? This looks like nitpicking, but it actually touches cache performance and whether Stage 5's diff against NumPy lines up. [The next piece](./02-weight-shape.md) tears exactly that apart.
