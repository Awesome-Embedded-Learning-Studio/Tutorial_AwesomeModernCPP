---
title: "Buttons: debouncing, state machines, variant"
description: "Read GPIO input and debounce it in software, manage the button lifecycle with a state machine, and express button events with variant"
chapter: 2
order: 0
tags:
  - stm32f1
  - intermediate
  - 状态机
  - variant
difficulty: intermediate
platform: stm32f1
translation:
  source: documents/vol8-domains/embedded/f103/02-button/index.md
  source_hash: 88a0f7240eb5b54c5381c848f04e12df4d8a66db28a1db0fb0e6b0c8a4db510d
  translated_at: '2026-09-26T04:18:00+00:00'
  engine: anthropic
  token_count: 122
---

# Buttons: debouncing, state machines, variant

> Status: planned

## Overview

Buttons look simple — the traps are all in the timing: how to debounce mechanical bounce, how to tell a short press from a long one, how states transition. This stop manages the button lifecycle with a state machine and expresses button events with variant, so the type system watches every branch.

## Chapter Navigation

> Content in progress — stay tuned.
