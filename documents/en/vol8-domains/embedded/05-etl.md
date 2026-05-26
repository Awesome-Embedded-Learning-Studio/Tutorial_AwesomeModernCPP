---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: Embedded ETL library
difficulty: intermediate
order: 5
platform: stm32f1
prerequisites:
- 'Chapter 6: RAII与智能指针'
reading_time_minutes: 3
tags:
- cpp-modern
- intermediate
- stm32f1
title: ETL Embedded Template Library
translation:
  source: documents/vol8-domains/embedded/05-etl.md
  source_hash: f625b319cb10975cd459dd75feb3f9634450a6ea836c8cbd11e1f98d6936c42b
  translated_at: '2026-05-26T12:20:41.469631+00:00'
  engine: anthropic
  token_count: 473
---
# Embedded C++ Tutorial — ETL (Embedded Template Library)

Curiosity: Why do people in the embedded world treat `new` like a "hazardous material" and handle it with gloves? The answer is simple: the heap is unpredictable. ETL (Embedded Template Library) was born to solve this problem: it brings familiar container and algorithm concepts to embedded scenarios, but strips out dynamic allocation, making everything predictable, measurable, and auditable.

------

## What is ETL

ETL is a C++ template library designed for embedded systems (authored and maintained on GitHub under the MIT license). It provides many STL-like containers and utilities, but all containers have a **fixed capacity** or a **maximum capacity** — they never call `malloc/new`, making them ideal for systems with strict memory controllability requirements. It is compatible with older compilers and embedded toolchains (designed to support C++03 and later environments).

------

## Why Use ETL

You can think of ETL as an "STL clone without dynamic allocation": it retains the familiar API style (developer-friendly) while allocating memory upfront in the form of static, stack, or pre-allocated blocks. This brings two direct benefits:

- **Determinism**: No more worrying about heap fragmentation, allocation failures, or unpredictable latency.
- **Performance and cache friendliness**: Container storage is typically allocated contiguously, making traversal much more cache-friendly.

In short: if we want the convenience of STL but need bare-metal-level predictability, ETL is a great fit.

------

## Core Features

ETL's design focus can be summed up in a few sentences: fixed/maximum-capacity containers (variants of vectors, queues, linked lists, maps, etc.), no heap allocation, STL-style API compatibility (where possible), and MIT-licensed open-source with active maintenance on GitHub. We can find ports or wrappers for it in the Arduino, PlatformIO, and various other embedded ecosystems.

------

## Getting Started

Here is a minimal example: a fixed-capacity vector (note: the actual header file names and namespaces depend on the ETL version we use):

```cpp
#include <etl/vector.h>
#include <iostream>

int main() {
    // 最大容量 8 的静态向量，内存事先分配好（无动态分配）
    etl::vector<int, 8> v;

    for (int i = 0; i < 6; ++i) {
        v.push_back(i * 10); // 如果超过容量，会有 safe variant 报错或返回错误（取决于配置）
    }

    for (auto it = v.begin(); it != v.end(); ++it) {
        std::cout << *it << "\n";
    }

    // 指定位置插入/删除等 API 很像 STL
    v.insert(v.begin() + 2, 99);
}

```

Tip: ETL containers typically specify their capacity via template parameters (or manage object pools using the `etl::pool` category), so memory usage is known at compile time.

------

## Limitations of ETL

ETL is not intended to replace STL in all scenarios — it is a "tailor-made alternative/supplement for embedded systems." If objects in our project come from third-party libraries and cannot be modified, or if we genuinely need dynamic growth to an uncertain size, STL (or the heap) remains the more convenient choice. ETL's static allocation can also lead to an increase in binary size (due to template instantiation), so we need to weigh the issue of code bloat at compile time.

So ETL is not magic, but rather an engineering trade-off — it uses templates to combine the two desires of "static memory + familiar API" into one. For systems that require determinism, low memory overhead, and real-time guarantees (such as bootloaders, RTOS task queues, or driver-layer buffer management), ETL is an excellent tool. Using it to fix everyday memory issues is often far more reliable than doing yoga on the board with `malloc`.
