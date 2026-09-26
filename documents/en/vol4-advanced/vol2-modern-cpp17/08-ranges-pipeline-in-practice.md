---
chapter: 12
cpp_standard:
- 20
description: 'C++20 pipe operator | chains view adaptors such as filter/transform into one lazy pipeline: left-associative and equivalent to nested function calls, with elements flowing through one by one on iteration; views do not own data and dangle once the source dies, custom types plug in just by providing begin/end, and materializing into a container uses the iterator-pair constructor.'
difficulty: intermediate
order: 8
platform: host
prerequisites:
- 'C++20 Ranges: Ranges and Views'
reading_time_minutes: 14
related:
- 'C++20 Ranges: Ranges and Views'
- 'Designated Initializers'
tags:
- host
- cpp-modern
- intermediate
- Ranges
title: 'C++20 Ranges: Pipelines in Practice'
translation:
  source: documents/vol4-advanced/vol2-modern-cpp17/08-ranges-pipeline-in-practice.md
  source_hash: 354c3dc5cd404b3a730c2d5aecf36527811b6f985251678cacf3ee443901080c
  translated_at: '2026-09-26T03:31:33+00:00'
  engine: anthropic
  token_count: 5500
---
# C++20 Ranges: Pipelines in Practice

In the previous piece we saw that views are lazy, lightweight handles that own no data. But a single view does only one thing; what really makes them handy is chaining several views into one pipeline, where each step's output feeds straight into the next. The Unix pipeline `cat data | grep pattern | sort` is exactly this idea — every program does one job, and strung together they get a whole task done. C++20 brought this style into the language, and the mechanism is the overloaded pipe operator `|`.

In this piece we pin down the pipe's semantics (it is just nested function calls written another way), demonstrate it in two practical scenarios — ADC sample processing and protocol byte parsing — and finally explain how to plug custom types into a pipeline, along with the trap that is easiest to step into: views do not own data.

## The pipe `|`: nested calls in another notation

Let's get the semantics precise first. These two snippets are equivalent:

```cpp
std::vector<int> data{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

// Pipe style: read top to bottom, like a sentence
auto pipe = data
    | std::views::filter(is_even)
    | std::views::transform(times10);

// Nested-call style: read inside out, gets ugly as layers pile up
auto nested = std::views::transform(std::views::filter(data, is_even), times10);
```

The pipe `|` is left-associative: `a | f | g` parses as `(a | f) | g`, which corresponds exactly to `g(f(a))`. Under the hood it is just function application, only written as a horizontal pipeline that reads better. Let's run it and confirm both produce the same result:

<OnlineCompilerDemo allow-run
  title="Pipe style and nested-call style are equivalent"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_pipe_basics.cpp"
  description="The same filter+transform written as a pipe and as nested calls produces identical output, verifying that left-associative | is equivalent to function nesting."
/>

```text
pipe:  20 40 60 80 100
nested:20 40 60 80 100
```

## The whole pipeline is lazy

Last time we said a single view is lazy, and a pipeline keeps that property: building the pipeline executes nothing; only when you iterate the result does data flow through the chain element by element. Let's prove it with a counting lambda.

```cpp
std::vector<int> data{1, 2, 3, 4, 5};
int filter_calls = 0, transform_calls = 0;

auto pipe = data
    | std::views::filter([&](int x){ ++filter_calls; return x % 2 == 0; })
    | std::views::transform([&](int x){ ++transform_calls; return x * 10; });
```

Once the `pipe` line finishes, both counters are still 0. Only when `for (int x : pipe)` actually starts iterating do filter and transform get called.

<OnlineCompilerDemo allow-run
  title="Laziness proof: nothing runs at build time, only on iteration"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_pipe_lazy_and_dangling.cpp"
  description="Counting lambdas show filter/transform are called zero times while the pipeline is being built, and only go non-zero once the for loop starts."
/>

```text
[构建前]  filter=0 transform=0
[构建后,迭代前] filter=0 transform=0
[迭代后]  filter=5 transform=2 产出=2 个元素
```

Notice that `filter` ran 5 times (all 5 elements passed through the predicate), while `transform` ran only 2 times (only 2 elements survived the filter). The whole pipeline is a **single pass**: an element starts at the source, flows through filter and then transform all the way to the end, never landing anywhere in between and never being stored in an intermediate vector. That is also why it saves memory over "`copy_if` first, then `transform`".

## Views do not own data: the easiest trap to step into

This one has to be called out on its own. Views like `filter` and `transform` **do not copy or hold the source data** — they store only a reference to the source. While the source container lives, the view is valid; the moment the source is gone, the view becomes a dangling reference.

The most common way this goes wrong is building a local vector inside a function and returning a view of it:

```cpp
auto make_dangling_view() {
    std::vector<int> local{1, 2, 3, 4, 5};   // destroyed when the function returns
    return local | std::views::filter([](int x){ return x > 2; });
}
```

This code compiles (the view type can be deduced), but the view you get points into freed vector memory, and iterating it is **undefined behavior**. By the same token, building a view on a temporary dangles too:

```cpp
auto bad = std::vector<int>{1, 2, 3}        // temporary, destroyed at the end of this line
    | std::views::filter([](int x){ return x > 1; });
```

::: warning A view is just a reference — never let it outlive its source
A view returned from a pipeline owns no data; its lifetime follows the source container. If you need to keep the result long-term, materialize it into a container that actually owns the data with an iterator-pair constructor such as `std::vector<float>(pipe.begin(), pipe.end())`. This is the standard C++20 approach, and we will bring it up again below.
:::

## In practice: multi-stage ADC sample processing

The most natural landing spot for the pipeline style is multi-stage cleaning of sensor data. Raw ADC samples typically need out-of-range noise dropped, conversion to voltage, and then a calibration curve applied. Three steps, three transform/filter stages, chained into one pipeline:

```cpp
struct AdcSample { std::uint16_t raw; };

std::vector<AdcSample> samples = fetch_samples();   // one frame of samples

auto pipeline = samples
    | std::views::filter([](const AdcSample& s){
        return s.raw >= 64 && s.raw <= 4000;       // drop out-of-range noise
    })
    | std::views::transform([](const AdcSample& s){
        return s.raw * 3.3f / 4095.0f;             // raw value -> voltage
    })
    | std::views::transform([](float v){
        return 1.001f * v + 0.0002f * v * v;        // second-order calibration curve
    });
```

Each step has a single responsibility: adding a stage is one more line attached to the pipeline, and to skip calibration while debugging you just comment out that one transform. To hold the result long-term (say, store it into a buffer), materialize it with the iterator-pair constructor:

```cpp
std::vector<float> kept(pipeline.begin(), pipeline.end());
```

<OnlineCompilerDemo allow-run
  title="Multi-stage ADC pipeline: filter -> voltage -> calibration"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_pipe_adc.cpp"
  description="A simulated frame of ADC samples (with out-of-range noise) goes through a three-stage pipeline, then materialized into a vector with the iterator-pair constructor."
/>

```text
校准后电压: 0.8262 2.4212 1.6526 2.8249
物化进 vector 的样本数:4
```

Of the 8 raw samples, 4 were filtered out as out-of-range, and the remaining 4 went all the way through calibration.

## In practice: parsing a protocol byte stream

Another common job is assembling 16-bit words out of a byte stream. One C++20 boundary has to be made clear first: the way you often see it written is `std::views::chunk(2)` to group the bytes two at a time, but `chunk` only entered the standard in **C++23** and does not compile under `-std=c++20` (on GCC 16 it actually reports `'chunk' is not a member of 'std::views'`). In C++20 we switch to a form that does not depend on `chunk`: generate indices with `iota` and take them two at a time:

```cpp
std::vector<std::uint8_t> bytes = receive_spi_data();   // big-endian byte stream

// Pair up adjacent bytes into 16-bit words (C++20-friendly, no chunk)
auto words = std::views::iota(std::size_t{0}, bytes.size() / 2)
    | std::views::transform([&](std::size_t i){
        std::uint16_t hi = bytes[i * 2];
        std::uint16_t lo = bytes[i * 2 + 1];
        return static_cast<std::uint16_t>((hi << 8) | lo);
    });

// Drop the 0xFFFF padding word
auto valid = words | std::views::filter([](std::uint16_t w){ return w != 0xFFFF; });
```

The indices `iota` generates are themselves lazy and take no memory; once the pipeline is attached, "take an index" and "assemble a word" are strung together.

::: warning views::chunk / slide / stride are C++23
Grouping and sliding-window adaptors like these need `-std=c++23`. To use them in a C++20 project, roll your own with `iota + transform`, or upgrade to C++23. Also, before using an adaptor, first confirm it is fully implemented on your target compiler — ranges only landed in GCC 10, and some adaptors were filled in by later versions.
:::

## How to plug a custom type into a pipeline

In embedded work we often have our own container classes (ring buffers, sampling windows, register maps). Getting them to work with `data | views::filter(...)` has a lower bar than you might think: **as long as the type is a range — meaning it provides `begin()` and `end()` — it can appear directly on the left side of a pipe**. Internally the pipeline wraps it in a `views::all` and grabs the begin and end iterators.

The following defines an `IntWindow` whose internals are nothing more than a pointer to a contiguous run of ints plus a length:

```cpp
class IntWindow {
public:
    IntWindow(const int* p, std::size_t n) : p_(p), n_(n) {}
    const int* begin() const { return p_; }      // these two are enough
    const int* end()   const { return p_ + n_; }
    std::size_t size() const { return n_; }
private:
    const int* p_;
    std::size_t n_;
};

int raw[] = {10, 15, 20, 25, 30, 35, 40};
IntWindow window(raw, 7);

// A custom type plugs straight into the pipeline
auto out = window
    | std::views::filter([](int x){ return x > 18; })
    | std::views::transform([](int x){ return x / 5; });
```

This "bolt begin/end onto the type" style is enough for the vast majority of embedded scenarios. If you also want to write your own adaptor (something that sits on the right side of `|` the way `filter` does), you need to implement a Range Adaptor Object, which involves considerably more template machinery — a topic we'll leave for when it is actually needed.

<OnlineCompilerDemo allow-run
  title="Custom type into a pipeline, compared with a hand-written loop"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_pipe_custom_and_perf.cpp"
  description="Once IntWindow provides begin/end it plugs straight into the pipeline; the pipeline produces the same output as a hand-written loop, and stays lazy and single-pass with no intermediate vector."
/>

## Performance: the pipeline genuinely does not slow you down

Given that it is lazy and single-pass and stores only lightweight handles, how expensive is the pipeline style compared with a hand-written loop? We ran a 2-million-element comparison: the old style does `copy_if` into an intermediate vector first, then `transform`s and accumulates; the pipeline style goes straight through one chain and accumulates.

```text
手写循环累加 = 3999997997450
管道写法累加 = 3999997997450
两者一致:是
手写循环:23133 us(20 次平均)
管道写法:12812 us(20 次平均)
管道更快:老写法建了中间 vector,惰性单遍省了分配和拷贝
```

The two results are exactly identical, and the pipeline style is actually faster. The reason: at `-O2` the compiler inlines every lambda along the pipeline and the data flows through in a single pass, while the old style pays for an extra intermediate vector (one allocation plus one copy). It compiles clean under `-Wall -Wextra`, without a single warning.

There is a precondition, of course: the pipeline is single-pass, and with modest data volumes the compiler can see the whole chain. If you insist on materializing some segment into a vector midway and continuing from there, the cost of that allocation comes straight back. So our rule of thumb is simple: **if a pipeline can run end to end, don't land it midway**; when landing is unavoidable, one materialization is enough — don't store an intermediate result at every step.

## A few pointers for avoiding the pitfalls

**Don't expect iterating the same pipeline to "cache" anything.** In the laziness experiment above, once the for loop finished one pass, the filter counter had climbed from 0 to 5; run a second pass and the filter runs again, the counter keeps climbing. Most views (`filter`, `transform`, `take`, and friends) **do not cache results** — every iteration re-evaluates. With a stable source, several passes give the same result (you just recompute each time), but with generator-style views like `iota` or adaptors carrying internal state, mind the semantics when you repeatedly take `begin/end`.

**A view must not outlive its source.** Already stressed above: returning a view of a function-local vector, or hanging a view off a temporary, both dangle. If you need to keep it long-term, materialize it into a container.

**The compiler must be new enough.** C++20 ranges need GCC 10 or later; the GCC 16.1.1 on this machine was tested and supports them fully. Adaptors like `chunk`/`slide`/`stride` are C++23 and unavailable under `-std=c++20`.

**Error messages will be long.** A pipeline is all templates; one lambda return type mismatch and GCC can spit out dozens of lines of constraint failures. When you hit one, read the innermost constraint error first and check that the range's `value_type` matches the parameter type your lambda accepts.

## That wraps up this volume

The pipe operator plus Ranges lets you write "filter, transform, collect" data-processing pipelines as smoothly as natural language, while keeping single-pass lazy performance. Combined with what the earlier pieces covered — `if constexpr`, variadic templates, perfect forwarding, CTAD, type-safe `any`/`variant`, designated initializers — the modern C++ toolbox in our hands now covers the vast majority of embedded scenarios: compile-time dispatch, zero-overhead abstraction, type-safe data carriers, self-documenting configuration, and composable data processing.

This volume has been about "what the language gives you". What comes next is "how to organize an engineering project with these tools", including RAII resource management, smart-pointer ownership, concurrency models, and more. The tools themselves are not complicated; the hard part is picking the right one in a real project and putting each in its right place.
