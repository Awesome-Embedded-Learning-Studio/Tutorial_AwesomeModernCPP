---
title: "I2C: sensor driver design"
description: "Designing a clean peripheral driver around an I2C sensor: wrapping bus timing, propagating errors and timeouts, owning resource lifetimes"
chapter: 5
order: 0
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 外设管理
difficulty: intermediate
platform: stm32f1
---

# I2C: sensor driver design

> Status: planned

## Overview

Hanging sensors off an I2C bus is everyday embedded work. This station uses it to practice driver design: how to wrap bus timing, how errors and timeouts propagate upward, and how the sensor's resource lifetime is managed.

## Chapter Navigation

> Content in progress — stay tuned.
