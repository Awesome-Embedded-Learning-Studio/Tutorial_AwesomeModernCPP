---
chapter: 13
cpp_standard:
- 11
- 14
- 17
description: 'Templates are instantiated implicitly by default, and every translation unit generates its own copy of the same code. How an explicit instantiation definition plus an extern template declaration concentrates instantiation in one place, with an honest assessment of the real compile-time payoff (unmeasurable in small projects; the savings only accumulate in large ones).'
difficulty: intermediate
order: 7
platform: host
prerequisites:
- 'TMP Core Techniques: The World Before Concepts'
- 'Concepts: Putting Constraints in the Signature'
reading_time_minutes: 11
related:
- 'Static Reflection Basics: The Reflection Operator and Splice Recomposition'
- 'Templates and Exception Safety: move_if_noexcept and Reallocation'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 编译期计算
- 工具链
title: 'Template Instantiation Control: extern template and Compile Times'
translation:
  source: documents/vol4-advanced/vol3-metaprogramming-cpp20-23/07-template-instantiation-control.md
  source_hash: 5babe3d0f51e25c4a61a2659bf42972eb04fe25739827f08eee002ac44ab0e5f
  translated_at: '2026-09-26T04:52:14+00:00'
  engine: anthropic
  token_count: 1800
---
# Template Instantiation Control: extern template and Compile Times

The last piece closed by promising that this one would come back to something you can use today. Templates gave C++ zero-cost abstraction, but they also brought along a less glamorous side effect: compile time. If a template gets used with the same type arguments in a dozen-plus translation units, the compiler may dutifully instantiate it once in each and every one of them. C++11 handed us a tool for exactly this: `extern template`. This piece works through the two mechanisms that control template instantiation (the explicit instantiation definition and the extern template declaration), then honestly assesses how much they really help compile time.

## Implicit instantiation: generated on demand, once per translation unit

Templates default to **implicit instantiation**: you use `Heavy<int>` somewhere, and the compiler generates exactly the members of `Heavy<int>` that you used, right there in that translation unit. The mechanism is on demand — members you never touch never get generated, which is nice. The problem is that it runs **once per translation unit**.

Picture a project with `use_a.cpp` and `use_b.cpp`, both using `Heavy<int>`. When `use_a.cpp` compiles, the compiler instantiates a copy of the `Heavy<int>` code and drops it into `use_a.o`; when `use_b.cpp` compiles, another copy lands in `use_b.o`. At link time, the linker sees `Heavy<int>::compute` defined in both `.o` files, and leans on the ODR (one definition rule) plus templates' "weak symbol" status to merge them back into one. At runtime only a single copy of the code exists — no problem — but **the compile-phase work was done twice**. That is exactly the ailment `extern template` is meant to treat.

## Explicit instantiation definition: concentrating instantiation in one place

To bring this under control, you first reach for the **explicit instantiation definition**. The syntax starts with `template`, followed by a concrete instance of the template:

```cpp
#include "heavy_template.h"

template struct Heavy<int>;   // Instantiate every member of Heavy<int> in this translation unit
```

This one line says: "in this `.cpp`, please instantiate all of `Heavy<int>`'s member functions, properly and completely." It usually lives in a file of its own, something like `explicit_inst.cpp`, whose sole job is "centralized instantiation."

## extern template: telling the other translation units to stop generating

Centralized instantiation alone is not enough: the other translation units know nothing about it and will still implicitly instantiate their own copies. So you pair it with the **explicit instantiation declaration**, better known as `extern template`:

```cpp
#include "heavy_template.h"

extern template struct Heavy<int>;   // Heavy<int> is instantiated elsewhere; don't generate it here
```

This line tells the compiler: "`Heavy<int>` has already been instantiated in some other translation unit — don't generate code here, just use it." The translation unit is spared the instantiation work; at link time it simply picks up the definitions from `explicit_inst.o`.

With the two working as a pair, "every TU instantiates its own copy" collapses into "only one TU instantiates, the rest just reference it." Let's put the mechanism through a real run and see.

## Hands-on: how the mechanism runs

A minimal multi-file project: `heavy_template.h` defines the template, `use_a.cpp` goes the old route of implicit instantiation, `use_b.cpp` uses extern template, `explicit_inst.cpp` supplies the explicit instantiation definition, and `main.cpp` ties everything together:

```cpp
// heavy_template.h
#pragma once
template <typename T>
struct Heavy {
    T value;
    explicit Heavy(T v) : value(v) {}
    T compute(T x) const {
        T acc = value;
        for (int i = 0; i < 10; ++i) acc = acc * x + value;
        return acc;
    }
};
```

```cpp
// use_b.cpp — extern template suppresses instantiation
#include "heavy_template.h"
#include <iostream>
extern template struct Heavy<int>;   // Instantiated elsewhere; don't generate here
void use_b() {
    Heavy<int> h{99};
    std::cout << "use_b: " << h.compute(3) << "\n";
}
```

```cpp
// explicit_inst.cpp — centralized explicit instantiation
#include "heavy_template.h"
template struct Heavy<int>;
```

Compile, link, and run (`use_a.cpp` has the same structure as `use_b.cpp`, minus the extern line):

```bash
$ g++ -std=c++20 -Wall -Wextra -c use_a.cpp use_b.cpp explicit_inst.cpp main.cpp
$ g++ use_a.o use_b.o explicit_inst.o main.o -o demo && ./demo
use_a: 85974
use_b: 8768727
```

All four object files compile without complaint, the link passes, and the program runs normally. The mechanism itself is sound.

The more interesting question is what happens when you don't provide the explicit instantiation definition. Drop `explicit_inst.cpp`, leaving only `use_b.cpp` (which carries the extern declaration) and `main.cpp`:

```text
/usr/bin/ld: use_b.o: in function `use_b()':
undefined reference to `Heavy<int>::Heavy(int)'
undefined reference to `Heavy<int>::compute(int) const'
```

The linker cannot find the definitions of `Heavy<int>`'s constructor and `compute`, and reports undefined reference errors. That error message lays out the extern template contract quite bluntly: if you declare "the definition lives elsewhere," you had better actually instantiate that definition in some translation unit — otherwise the declaration is a bounced check. And while using the pair, don't forget: any translation unit that doesn't carry the extern declaration (like `use_a.cpp` here) will still implicitly instantiate its own copy — `extern template` means "this TU of mine won't generate it," not "generate it once globally."

## Compile-time payoff: don't be fooled by the "optimizes compile time" slogan

Bring up extern template, and almost everyone will say it "reduces compile time." In principle that holds, but how much it actually saves is worth measuring yourself. GCC has a `-ftime-report` flag that prints per-phase timings after compilation, including a dedicated `template instantiation` line. Start with a small file:

```text
$ g++ -std=c++20 -c -ftime-report use_b_noextern.cpp   # implicit-instantiation version
 template instantiation             :   0.08 ( 26%)    14M ( 23%)
```

Template instantiation eats roughly a quarter of the total compile time — sounds like a job for extern template. Let's compare: the same `use_b.cpp`, in one version carrying the extern declaration (no instantiation of `Heavy<int>`) and in the other not (implicit instantiation), three runs each, watching the `template instantiation` line.

| File | template instantiation, 3 runs |
|---|---|
| `use_b.cpp` (extern, no instantiation) | 0.08 / 0.07 / 0.05 |
| `use_b_noextern.cpp` (implicit instantiation) | 0.07 / 0.05 / 0.05 |

The differences sit entirely inside the noise; nothing measurable comes out. To rule out the "template too lightweight" suspicion, we made the template heavier — inside it are three groups of recursive metafunctions, 80 levels each (Fibonacci, triangular numbers, Lucas numbers), so instantiating `Big<int>` cascades into roughly 240 template specializations. Three runs each again:

| File | template instantiation, 3 runs |
|---|---|
| `big_b.cpp` (extern) | 0.08 / 0.07 / 0.05 |
| `big_b_noextern.cpp` (240 specializations cascading) | 0.07 / 0.05 / 0.05 |

Still unmeasurable. Modern compilers instantiate this kind of "pure type computation" template absurdly fast — a job of a few tens of nanoseconds — and the noise from the parsing and optimization phases swallows it whole.

So when does extern template genuinely save time? The answer: **in large projects, where dozens of translation units repeatedly instantiate the same "heavy" template**. Heavy does not mean the pure TMP recursion we built here; it means templates whose instantiation drags in a large swath of standard library code — say a generic component built on `std::variant` plus a pile of algorithms, used with the same arguments across twenty `.cpp` files. Only then do twenty duplicate instantiations accumulate into something visible to the naked eye. In that scenario, extern template compresses twenty passes into one, and the payoff is real and solid. So the decision of whether to reach for extern template comes down to "absolute cost of one instantiation" multiplied by "number of duplicate translation units" — both have to be large for it to matter. Slapping extern on a lightweight template that gets used two or three times in a small project is pure boilerplate bloat; call a stop to it.

## A side note: other ways to tackle compile time

If your goal is "make compilation faster," extern template is usually not the biggest lever in the toolbox. The moves that pay off more often: replacing unnecessary `#include`s with forward declarations, splitting a template's declaration and definition into separate headers to shrink the number of instantiation entry points, precompiled headers (PCH), and C++20 modules — modules redefine, at the mechanism level, "how translation units share code," so they treat the root cause, though toolchain support is still being polished even today. extern template is a small wrench in that toolbox: it has its moments, but it is not the main tool.

In the next piece we'll watch templates and exceptions get tangled together: why `vector` has to care about its element type's `noexcept` during reallocation, and how the `move_if_noexcept` machinery brokers a compromise between "performance" and "exception safety."
