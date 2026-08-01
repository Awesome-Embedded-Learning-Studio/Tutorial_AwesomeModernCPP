---
chapter: 12
cpp_standard:
- 20
description: 'C++20 pipe operator | chains view adaptors like filter and transform into a lazy pipeline that is left-associative and equivalent to nested function calls, evaluated element by element on iteration. Views do not own data and dangle once the source dies, custom types plug in by providing begin/end, and materializing into a container uses the iterator-pair constructor.'
difficulty: intermediate
order: 8
platform: host
prerequisites:
- 'C++20 Ranges Library Basics and Views'
reading_time_minutes: 14
related:
- 'C++20 Ranges Library Basics and Views'
- 'Designated Initializers'
tags:
- host
- cpp-modern
- intermediate
- Ranges
title: 'Pipeline Operations and Ranges in Practice'
---
# Pipeline Operations and Ranges in Practice

In the previous chapter we saw that views are lazy, lightweight handles that do not own data. A single view does one thing, though. What really pays off is chaining several views into a pipeline, where each step's output feeds straight into the next. The Unix pipeline `cat data | grep pattern | sort` is exactly this idea — each program does one job, and together they get a full task done. C++20 brought this style into the language, and the mechanism is the overloaded pipe operator `|`.

This chapter pins down the semantics of the pipe (it is just nested function calls in another notation), runs two practical scenarios (ADC sample processing and protocol byte parsing), then shows how to plug a custom type into a pipeline. Along the way it flags the one trap that catches everyone: views do not own data.

## The pipe `|`: nested calls in another notation

Let's get the semantics right first. These two snippets are equivalent:

```cpp
std::vector<int> data{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

// pipe form: read top to bottom, like a sentence
auto pipe = data
    | std::views::filter(is_even)
    | std::views::transform(times10);

// nested call form: read inside out, gets ugly with more layers
auto nested = std::views::transform(std::views::filter(data, is_even), times10);
```

The pipe `|` is left-associative: `a | f | g` parses as `(a | f) | g`, which is exactly `g(f(a))`. It really is function application, just written horizontally so the data flow reads better. Let's run it to confirm both produce the same result:

<OnlineCompilerDemo allow-run
  title="Pipe form and nested call form are equivalent"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_pipe_basics.cpp"
  description="The same filter+transform written as a pipe and as nested calls produces identical output, confirming | is left-associative and equivalent to function nesting."
/>

```text
pipe:  20 40 60 80 100
nested:20 40 60 80 100
```

## The whole pipeline is lazy

Last chapter we said a single view is lazy. A pipeline keeps that property: building the pipeline does nothing; only when you iterate the result does data actually flow through the chain. We can prove it with a counting lambda.

```cpp
std::vector<int> data{1, 2, 3, 4, 5};
int filter_calls = 0, transform_calls = 0;

auto pipe = data
    | std::views::filter([&](int x){ ++filter_calls; return x % 2 == 0; })
    | std::views::transform([&](int x){ ++transform_calls; return x * 10; });
```

After the line that builds `pipe`, both counters are still 0. Only once `for (int x : pipe)` actually starts iterating do the filter and transform lambdas run.

<OnlineCompilerDemo allow-run
  title="Laziness proof: nothing runs at build time, only on iteration"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_pipe_lazy_and_dangling.cpp"
  description="Counting lambdas show filter and transform call counts are both 0 during pipeline construction, and only go non-zero once the for loop begins."
/>

```text
[before build]  filter=0 transform=0
[after build, before iterate] filter=0 transform=0
[after full iterate]  filter=5 transform=2 elements=2
```

Notice the filter ran 5 times (it walked all 5 elements through the predicate), but transform only ran 2 times (only 2 elements passed the filter). The whole pipeline is a **single pass**: an element starts at the source, flows through filter and then transform all the way to the end, never landing in an intermediate vector. That is also why it saves memory compared to "copy_if first, then transform."

## Views do not own data: this is the main trap

This one has to be called out on its own. Views like `filter` and `transform` **do not copy or hold the source data**: they store only a reference to it. As long as the source container lives, the view is valid; the moment the source dies, the view dangles.

The most common way to wreck this is to build a local vector inside a function and return its view:

```cpp
auto make_dangling_view() {
    std::vector<int> local{1, 2, 3, 4, 5};   // destroyed on return
    return local | std::views::filter([](int x){ return x > 2; });
}
```

This compiles (the view type can be deduced), but the view you get points at freed vector memory, and iterating it is **undefined behavior**. The same thing happens when the view is built on a temporary:

```cpp
auto bad = std::vector<int>{1, 2, 3}        // temporary, destroyed at end of line
    | std::views::filter([](int x){ return x > 1; });
```

::: warning A view is just a reference — don't let it outlive the source
A pipeline returns a view that does not own data; its lifetime follows the source container. If you need to keep the result around, materialize it into a container that actually owns data, using the iterator-pair constructor: `std::vector<float>(pipe.begin(), pipe.end())`. This is the standard C++20 approach, and we'll see it again below.
:::

## In practice: multi-stage ADC processing

The most natural place for the pipeline style is multi-stage sensor data cleaning. Raw ADC samples typically need out-of-range noise removed, conversion to voltage, and a calibration curve applied. Each step is a transform or filter, and they chain into one pipeline:

```cpp
struct AdcSample { std::uint16_t raw; };

std::vector<AdcSample> samples = fetch_samples();   // one frame

auto pipeline = samples
    | std::views::filter([](const AdcSample& s){
        return s.raw >= 64 && s.raw <= 4000;       // drop out-of-range noise
    })
    | std::views::transform([](const AdcSample& s){
        return s.raw * 3.3f / 4095.0f;             // raw value -> voltage
    })
    | std::views::transform([](float v){
        return 1.001f * v + 0.0002f * v * v;        // second-order calibration
    });
```

Each step has a single responsibility. Adding a stage means appending one line to the pipeline; to skip calibration while debugging, comment out that one transform. When you need to hold the result long-term (say, to store it in a buffer), materialize with the iterator-pair constructor:

```cpp
std::vector<float> kept(pipeline.begin(), pipeline.end());
```

<OnlineCompilerDemo allow-run
  title="Multi-stage ADC pipeline: filter -> voltage -> calibration"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_pipe_adc.cpp"
  description="A simulated ADC frame (with out-of-range noise) goes through a three-stage pipeline, then materialized into a vector with the iterator-pair constructor."
/>

```text
校准后电压: 0.8262 2.4212 1.6526 2.8249
物化进 vector 的样本数:4
```

Of 8 raw samples, 4 were filtered out as out-of-range, and the remaining 4 went through calibration.

## In practice: parsing a protocol byte stream

Another common job is assembling 16-bit words out of a byte stream. One C++20 boundary has to be cleared up first: a lot of writeups group bytes two at a time using `std::views::chunk(2)`, but `chunk` only entered the standard in **C++23**, so it does not compile under `-std=c++20` (GCC 16 reports `'chunk' is not a member of 'std::views'`). In C++20 we use a `chunk`-free form, generating indices with `iota` and pairing them up:

```cpp
std::vector<std::uint8_t> bytes = receive_spi_data();   // big-endian byte stream

// pair adjacent bytes into 16-bit words (C++20-friendly, no chunk)
auto words = std::views::iota(std::size_t{0}, bytes.size() / 2)
    | std::views::transform([&](std::size_t i){
        std::uint16_t hi = bytes[i * 2];
        std::uint16_t lo = bytes[i * 2 + 1];
        return static_cast<std::uint16_t>((hi << 8) | lo);
    });

// drop the 0xFFFF padding word
auto valid = words | std::views::filter([](std::uint16_t w){ return w != 0xFFFF; });
```

The indices produced by `iota` are themselves lazy and take no memory, and the pipeline ties "take an index" and "assemble a word" together.

::: warning views::chunk / slide / stride are C++23
Grouping and sliding adaptors require `-std=c++23`. In a C++20 project, roll your own with `iota + transform`, or upgrade to C++23. Before reaching for any adaptor, confirm it is fully implemented on your target compiler — ranges landed in GCC 10, and several adaptors were filled in by later versions.
:::

## Plugging a custom type into a pipeline

In embedded code we often have our own container types (ring buffers, sample windows, register maps). Getting them to work as `data | views::filter(...)` is easier than it looks: **as long as the type is a range (that is, it provides `begin()` and `end()`), it can go straight on the left side of a pipe**. The pipeline wraps it in a `views::all` and grabs the begin and end iterators.

Here is an `IntWindow` that holds just a pointer plus a length over a contiguous run of ints:

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

// custom type plugs straight into the pipeline
auto out = window
    | std::views::filter([](int x){ return x > 18; })
    | std::views::transform([](int x){ return x / 5; });
```

This "bolt begin/end onto the type" approach is enough for most embedded cases. If you also want to write your own adaptor (something that sits on the right side of `|`, like `filter` does), that takes a Range Adaptor Object and a good deal more template machinery — leave it for when you actually need it.

<OnlineCompilerDemo allow-run
  title="Custom type into a pipeline, compared with a hand-written loop"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_pipe_custom_and_perf.cpp"
  description="IntWindow plugs into the pipeline after providing begin/end; the pipeline produces identical output to a hand-written loop, and stays lazy and single-pass with no intermediate vector."
/>

## Performance: the pipeline does not slow you down

Since the pipeline is lazy, single-pass, and stores only lightweight handles, how does it compare to a hand-written loop? Let's run a 2-million-element comparison: the old style does `copy_if` into an intermediate vector, then `transform`-accumulates; the pipeline accumulates straight through one chain.

```text
手写循环累加 = 3999997997450
管道写法累加 = 3999997997450
两者一致:是
手写循环:23133 us(20 次平均)
管道写法:12812 us(20 次平均)
管道更快:老写法建了中间 vector,惰性单遍省了分配和拷贝
```

Both produce identical results, and the pipeline is actually faster here. The reason is that at `-O2` the compiler inlines the entire chain of lambdas and the data flows through in a single pass, while the old style pays for an extra intermediate vector (one allocation plus one copy). It compiles clean under `-Wall -Wextra`, with no warnings at all.

There is a caveat, of course: the pipeline wins because it is single-pass and the compiler can see the whole chain at modest data volume. If you force a materialization to a vector somewhere in the middle and then keep going, you pay for that allocation. So the rule is simple: **when a pipeline can go end to end, don't materialize midway**. When you must, materialize once — don't save an intermediate result at every step.

## A few pitfalls to remember

**Don't expect iterating the same pipeline to "cache."** In the laziness experiment above, after the first for loop the filter counter went from 0 to 5; a second pass runs the filter again and the counter keeps climbing. Most views (`filter`, `transform`, `take`, ...) **do not cache**: every iteration recomputes. As long as the source is stable, multiple passes give the same result (just recompute each time), but with generator-style views like `iota` or stateful adaptors, watch the semantics when you repeatedly take `begin` and `end`.

**A view cannot outlive its source.** Already emphasized above: returning a view over a local vector, or hanging a view off a temporary, both dangle. Materialize into a container if you need to keep it.

**Compiler needs to be new enough.** C++20 ranges need GCC 10 or later; this machine's GCC 16.1.1 supports them fully. Adaptors like `chunk`/`slide`/`stride` are C++23 and are not available under `-std=c++20`.

**Error messages get long.** Pipelines are all templates, and one mismatched lambda return type can produce dozens of lines of constraint failure. When that happens, look at the innermost constraint error first and confirm the range's `value_type` matches the parameter type your lambda accepts.

## That wraps this sub-volume

The pipe operator plus ranges lets you write "filter, transform, collect" data-processing pipelines almost like natural language, while keeping single-pass laziness for performance. Combined with the tools from earlier chapters (`if constexpr`, variadic templates, perfect forwarding, CTAD, type-safe `any`/`variant`, designated initializers), the modern C++ toolkit we now have covers the vast majority of embedded use cases: compile-time dispatch, zero-overhead abstraction, type-safe data carriers, self-documenting configuration, and composable data processing.

This volume has been about "what the language gives you." What comes next is "how to organize an engineering project with these tools": RAII resource management, smart-pointer ownership, concurrency models, and more. The tools themselves are not the hard part; the hard part is picking the right one in a real project and putting it in the right place.
