---
chapter: 13
cpp_standard:
- 11
- 14
- 17
description: 'Templates instantiate implicitly by default, and every translation unit generates its own copy of the same code. How explicit instantiation definitions and extern template declarations concentrate instantiation in one place, with an honest look at the real compile-time payoff, which small projects cannot measure but large ones accumulate.'
difficulty: intermediate
order: 7
platform: host
prerequisites:
- 'TMP Core Techniques: The World Before Concepts'
- 'Concepts: Putting Constraints in the Signature'
reading_time_minutes: 14
related:
- 'Static Reflection Basics: The Reflection Operator and Splice'
- 'Templates and Exception Safety: move_if_noexcept and Reallocation'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 编译期计算
- 工具链
title: 'Template Instantiation Control: extern template and Compile Time'
---
# Template Instantiation Control: extern template and Compile Time

The last piece ended by saying we'd come back to something you can use today. Templates gave C++ zero-cost abstraction, but they brought a less glamorous side effect: compile time. If a template gets used with the same type argument in a dozen translation units, the compiler may faithfully instantiate it in all dozen. C++11 gave us a tool to manage this: `extern template`. This piece explains the two ways to control template instantiation (explicit instantiation definitions and extern template declarations), and gives an honest look at how much they actually help compile time.

## Implicit instantiation: generated on use, once per translation unit

Templates default to **implicit instantiation**. You use `Heavy<int>` somewhere, and the compiler generates the members of `Heavy<int>` you used, right there in that translation unit. The mechanism is "on demand," and unused members aren't generated. So far so good. The problem is that it happens **once per translation unit**.

Picture a project with `use_a.cpp` and `use_b.cpp`, both using `Heavy<int>`. Compiling `use_a.cpp` instantiates a copy of `Heavy<int>` into `use_a.o`. Compiling `use_b.cpp` instantiates another copy into `use_b.o`. At link time, the linker sees `Heavy<int>::compute` defined in both `.o` files. It relies on the ODR (one definition rule) and the "weak symbol" status of templates to merge them into one. At runtime there's only one copy, no problem. But **the compile work was done twice**. That's the itch `extern template` wants to scratch.

## Explicit instantiation definition: concentrate instantiation in one place

To manage this, you first need an **explicit instantiation definition**. The syntax starts with `template`, followed by a concrete template instance:

```cpp
#include "heavy_template.h"

template struct Heavy<int>;   // instantiate every member of Heavy<int> in this translation unit
```

This line says: "in this `.cpp`, please instantiate all of `Heavy<int>`'s member functions, properly." It usually lives in a file like `explicit_inst.cpp`, dedicated to "centralized instantiation."

## extern template: tell other translation units "don't generate"

Centralized instantiation alone isn't enough. Other translation units don't know about it and keep instantiating on their own. So you pair it with an **explicit instantiation declaration**, which is `extern template`:

```cpp
#include "heavy_template.h"

extern template struct Heavy<int>;   // Heavy<int> is instantiated elsewhere, don't generate here
```

This line tells the compiler: "`Heavy<int>` is already instantiated in another translation unit. Don't generate code here, just use it." That translation unit skips the instantiation work, and at link time it finds the definition in `explicit_inst.o`.

Used together, "instantiate in every TU" collapses into "instantiate in one TU, everyone else references it." Let's verify this mechanism by running it.

## In practice: how the mechanism runs

A minimal multi-file project. `heavy_template.h` defines the template. `use_a.cpp` does it the old way, implicit instantiation. `use_b.cpp` uses `extern template`. `explicit_inst.cpp` provides the explicit instantiation definition. `main.cpp` ties it together:

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
// use_b.cpp: extern template suppresses instantiation
#include "heavy_template.h"
#include <iostream>
extern template struct Heavy<int>;   // instantiated elsewhere, don't generate here
void use_b() {
    Heavy<int> h{99};
    std::cout << "use_b: " << h.compute(3) << "\n";
}
```

```cpp
// explicit_inst.cpp: centralized explicit instantiation
#include "heavy_template.h"
template struct Heavy<int>;
```

Compile, link, run (`use_a.cpp` is structured the same as `use_b.cpp` but without the extern line):

```bash
$ g++ -std=c++20 -Wall -Wextra -c use_a.cpp use_b.cpp explicit_inst.cpp main.cpp
$ g++ use_a.o use_b.o explicit_inst.o main.o -o demo && ./demo
use_a: 85974
use_b: 8768727
```

Four object files compile cleanly, the link passes, the program runs. The mechanism works.

More interesting is "what happens if you don't provide the explicit instantiation definition." Drop `explicit_inst.cpp`, leaving only `use_b.cpp` (with its extern declaration) and `main.cpp`:

```text
/usr/bin/ld: use_b.o: in function `use_b()':
undefined reference to `Heavy<int>::Heavy(int)'
undefined reference to `Heavy<int>::compute(int) const'
```

The linker can't find the constructor or `compute` for `Heavy<int>` and reports undefined reference. This error states the `extern template` contract plainly. You declared "the definition is elsewhere," so you'd better actually instantiate that definition in some translation unit, or it's an empty promise. One more thing to remember when using them together: any translation unit that doesn't carry the extern declaration (like `use_a.cpp` here) still implicitly instantiates its own copy. `extern template` means "this TU doesn't generate," not "generate only once globally."

## The compile-time payoff: don't buy the "optimizes compile time" slogan without checking

Whenever `extern template` comes up, almost everyone says it "reduces compile time." True in principle. But how much it actually saves is worth measuring. GCC has a `-ftime-report` flag that prints per-phase timings after compiling, including a dedicated `template instantiation` line. First, a small file:

```text
$ g++ -std=c++20 -c -ftime-report use_b_noextern.cpp   # implicit instantiation version
 template instantiation             :   0.08 ( 26%)    14M ( 23%)
```

Template instantiation eats about a quarter of total compile time. Sounds like `extern template` should help. Let's compare: the same `use_b.cpp`, one version with the extern declaration (no instantiation of `Heavy<int>`), one without (implicit), three runs each, looking at the `template instantiation` line.

| File | 3 runs of template instantiation |
|---|---|
| `use_b.cpp` (extern, no instantiation) | 0.08 / 0.07 / 0.05 |
| `use_b_noextern.cpp` (implicit instantiation) | 0.07 / 0.05 / 0.05 |

The difference is entirely inside the noise. Nothing to measure. To rule out "the template is too light," let's make it heavier. Three groups of 80-deep recursive metafunctions (Fibonacci, triangular, Lucas) inside the template. Instantiating `Big<int>` drags in about 240 template specializations. Three runs each again:

| File | 3 runs of template instantiation |
|---|---|
| `big_b.cpp` (extern) | 0.08 / 0.07 / 0.05 |
| `big_b_noextern.cpp` (240 specializations dragged in) | 0.07 / 0.05 / 0.05 |

Still can't measure it. Modern compilers instantiate this kind of "pure type computation" template so fast that the work, in the tens of microseconds, drowns in the noise of parsing and optimization.

So when does `extern template` actually save time? In **large projects, where dozens of translation units repeatedly instantiate the same heavy template**. Heavy here doesn't mean the pure TMP recursion in our example. It means templates whose instantiation drags in a big slice of the standard library, say a generic component that uses `std::variant` and a pile of algorithms, used with the same type argument across twenty `.cpp` files. Twenty repeated instantiations add up to something visible. There, `extern template` compresses twenty into one and the payoff is real. So the decision to use `extern template` hinges on "absolute cost of one instantiation" times "number of repeating translation units." Both have to be large for it to matter. Adding `extern template` to a light template that gets used two or three times in a small project is pure boilerplate. Call it off.

## One word on other ways to treat compile time

If your goal is "make builds faster," `extern template` is rarely the highest-leverage move. The tactics that pay off more often: use forward declarations instead of unnecessary `#include`s, split a template's declaration and definition into separate headers to shrink the instantiation surface, use precompiled headers (PCH), and C++20 modules. Modules redefine "how translation units share code" from the ground up. They're the root fix, though toolchain support is still being polished. `extern template` is a small wrench in this toolkit. It has its place, but it isn't the main tool.

In the next piece we see how templates and exceptions get tangled up: why `vector` cares about the element type's `noexcept` during reallocation, and how `move_if_noexcept` mediates between "performance" and "exception safety."
