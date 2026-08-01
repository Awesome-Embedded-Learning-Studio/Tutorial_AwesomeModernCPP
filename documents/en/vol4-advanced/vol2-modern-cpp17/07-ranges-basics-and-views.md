---
chapter: 12
cpp_standard:
- 20
description: 'C++20 Ranges lifts "a pair of iterators" into a range, so algorithms take a whole container; views build on that with lazy evaluation, reference semantics, and O(1) copy, letting you chain filter/transform/take into a zero-allocation pipeline.'
difficulty: intermediate
order: 7
platform: host
prerequisites:
- 'Designated Initializers'
reading_time_minutes: 14
related:
- 'Pipelines and Ranges in Practice'
- 'Designated Initializers'
tags:
- host
- cpp-modern
- intermediate
- Ranges
title: 'C++20 Ranges: Ranges and Views'
---
# C++20 Ranges: Ranges and Views

Processing one frame of sensor data is usually a chain: drop the anomalies, convert the raw code into engineering units, take the first few and ship them out. The old way means opening two temporary `vector`s, one `copy_if` followed by one `transform`, a few `back_inserter`s in between; the logic reads like a chopped-up list. C++20 Ranges offers a smoother path: write the whole chain as one pipeline, allocate nothing in the middle, and the intent reads top to bottom. The key is the **view**, which is lazy, holds no data, and is cheap to copy, exactly the kind of abstraction embedded work wants most.

This article splits two concepts that are easy to conflate (Range and View), pins down the three core properties of a view with real measurements, and lands on a temperature-data pipeline.

## Range: anything you can iterate

C++20's definition of a Range is plain: **anything that can hand you a pair of iterators (begin/end)**. `std::vector`, `std::array`, raw arrays — all Ranges.

The first thing you feel is that algorithms stop forcing you to type a `begin()/end()` pair. Used to be:

```cpp
std::sort(vec.begin(), vec.end());
```

C++20 takes the whole container:

```cpp
std::ranges::sort(vec);   // pass the whole range
```

That's the surface sugar; the real work is in the view factories inside `<ranges>`. First, draw the line between two concepts:

- **Range**: the general name for anything iterable (`vector`/`array`/raw arrays qualify), and it **owns its data**.
- **View**: a special kind of Range that **holds no data**, just "another angle" on existing data, and evaluates **lazily**.

The next few sections are about views. This is the foundation of the whole piece.

## Views are lazy: building one computes nothing

A view is lazy. The moment you build a `filter` view, no computation happens; the predicate is not called until you start iterating. Let's prove it by counting calls inside the predicate:

```cpp
std::vector<int> data = {1, 2, 3, 4, 5};
int pred_calls = 0;

auto v = data | std::views::filter([&](int x) {
    ++pred_calls;
    return x > 2;
});
std::cout << "pred calls after building (no iteration): " << pred_calls << "\n";

for (int x : v) {
    std::cout << "got " << x << ", pred calls so far: " << pred_calls << "\n";
}
```

<OnlineCompilerDemo allow-run
  title="View laziness, reference semantics, O(1) copy"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_laziness.cpp"
  description="A counting predicate shows the filter view calls nothing when built and fires per element only during iteration; mutating the source changes what the next iteration sees; copying a view does not copy any elements."
/>

Run it:

```text
建视图后(还没遍历)谓词调用次数=0
取到 3, 此刻谓词已调用 3 次
取到 4, 此刻谓词已调用 4 次
取到 5, 此刻谓词已调用 5 次
改 data[2]=300 后重新遍历: 300 4 5
拷贝视图后 v2 首元素=300
```

`pred_calls` starts at 0: building the view cost zero predicate calls. Once iteration begins, `filter` has to scan past `1`, `2`, and `3` to find the first element greater than 2, so by the time we hand you `3` the count has already jumped to 3. That is the proof of laziness: the predicate runs only when an element is actually demanded.

One more property rides along. After we change `data[2]` from `3` to `300`, **a fresh iteration of the view sees the new value**. The view holds no copy; it references the source container, and when the source changes the view follows.

## Views hold no data; copying is O(1)

A view only "looks at" the underlying data, it does not own it. So copying a view copies a few iterators and a predicate, none of the underlying elements. For embedded, that means you can pass views around freely without worrying that you silently copied a whole buffer.

There is a counter-intuitive trap to flag early. The view **references** the source — literally. What it stores is a pointer/iterator to the source, not a snapshot of the values. So **the source must outlive the view**. The moment the source is destroyed, the view dangles. We will demonstrate this with a whole section later; for now, keep the rule.

## Common view factories

`<ranges>` ships a set of "view factories." Here are the ones most used in embedded, one minimal example each.

```cpp
std::vector<int> data = {120, 45, 230, 67, 340, 89, 56, 180};

// filter: keep only readings in [50,300]
auto valid = data | std::views::filter([](int v){ return v >= 50 && v <= 300; });

// transform: 12-bit ADC raw code to voltage (mV scale)
auto mv = std::views::transform(data, [](int adc){ return adc * 3300 / 4095; });

// take / drop: slice the head/tail of a data frame
auto seq     = std::views::iota(0, 10);
auto first3  = seq | std::views::take(3);                                  // 0 1 2
auto rest    = std::views::iota(0, 10) | std::views::drop(3);              // 3..9
auto middle  = std::views::iota(0, 10) | std::views::drop(2) | std::views::take(4);  // 2 3 4 5

// iota: produce ADC channel numbers 0..15, with no storage at all
auto adc_channels = std::views::iota(0, 16);
```

`iota` deserves a pause: it increments on demand, allocates nothing, stores nothing, a natural fit for "I just need a sequence of indices," like enumerating channel numbers or generating loop indices.

For string parsing there is also `split`, which cuts by a delimiter. NMEA sentences, key-value pairs, anything "comma or equals separated," becomes a one-liner:

```cpp
std::string raw = "sensor1=25,sensor2=30,sensor3=28";
for (auto sub : raw | std::views::split(',')) {
    std::string_view sv{sub.begin(), sub.end()};   // sub is not a string; wrap it
    // [sensor1=25] [sensor2=30] [sensor3=28]
}
```

<OnlineCompilerDemo allow-run
  title="View factories: filter / transform / take / drop / iota / split"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_factories.cpp"
  description="One minimal example for each of the six most common view factories, covering filtering, mapping, slicing, sequence generation, and string splitting."
/>

Run it:

```text
filter [50,300]: 120 230 67 89 56 180
transform->mV: 96 36 185 53 273 71 45 145
take 3: 0 1 2
drop 3: 3 4 5 6 7 8 9
drop 2 | take 4: 2 3 4 5
iota ADC 通道: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
split(','): [sensor1=25] [sensor2=30] [sensor3=28]
```

## Combining into a pipeline

A single view is modest; stringing them together is where Ranges earns its keep. The pipe `|` connects views into a chain that evaluates lazily, **and as you iterate, data flows through element by element** (the pipe operator gets a full treatment in the next article; here we build intuition).

```cpp
std::vector<int> readings = {120, 45, 230, 67, 340, 89, 56, 180};
int tf_calls = 0;

auto pipeline = readings
    | std::views::filter([](int v){ return v >= 50 && v <= 300; })
    | std::views::transform([&](int v){ ++tf_calls; return v * 3.3f / 4095; })
    | std::views::take(3);
```

This reads like a sentence: "from `readings`, keep the valid ones, convert to voltage, take the first three." No intermediate `vector`, no chopped-up logic. Let's use a counter to see how lazy this really is.

<OnlineCompilerDemo allow-run
  title="Pipeline laziness: take cuts the chain short once it has enough"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_pipeline.cpp"
  description="The whole filter|transform|take pipeline calls transform zero times when built; over iteration it fires exactly 3 times, because take stops the upstream early."
/>

Run it:

```text
建好管道(没遍历) transform 调用次数=0
前 3 个有效读数的电压:
  0.096703
  0.185348
  0.053993
遍历完 transform 总调用次数=3(take 在取够 3 个后掐断了管道)
```

`transform` is called 3 times — exactly the `take(3)` count. The whole pipeline advances **element by element, on demand**: once `take` has 3, the upstream `filter` and `transform` stop, and the remaining elements are never touched. Of the 8 original readings, `340`, `56`, and `180` were never tested by filter, never computed by transform. That is the core payoff of a lazy pipeline: you pay only for the results you actually consume.

## Embedded in practice: a temperature pipeline

Let's assemble the pieces into a real embedded scenario. A batch of temperature sensors returns readings, with anomalies mixed in (999 when a sensor drops, -200 on an open circuit). The job: filter the anomalies, convert Celsius to Fahrenheit, average, ship it out.

```cpp
std::vector<int> readings = {23, 999, 25, -200, 27, 22, 999, 26};

auto processed = readings
    | std::views::filter([](int t){ return t >= -50 && t <= 150; })
    | std::views::transform([](int t){ return t * 9.0 / 5.0 + 32.0; });
```

The whole chain has no `filtered` or `calibrated` intermediate. The data is walked once, memory cost is constant.

<OnlineCompilerDemo allow-run
  title="Temperature sensor data pipeline"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_sensor_pipeline.cpp"
  description="One frame of temperature readings with anomalies; filter drops them, transform converts to Fahrenheit, take the average, with no temporary containers at all."
/>

Run it:

```text
有效读数(F): 73.4 77.0 80.6 71.6 78.8
平均温度: 76.3 F
```

Note that the `999` and `-200` never appear in any intermediate buffer — filter simply skipped them. Under the old style those values would have been `push_back`'d into the raw `vector` first, only to be discarded during filtering.

## Pitfall: the lifetime of a view

A view holds no data. Flip that around and you get the biggest trap: **when the source container dies, the view dangles**. The most common shape is returning a view from a function, where the view references a local inside the function:

```cpp
// Bad: local dies when the function returns; the returned view dangles at once
auto make_bad_view() {
    std::vector<int> local = {1, 2, 3, 4, 5};
    return local | std::views::filter([](int x){ return x > 2; });
}
```

This is a use-after-free. Inside, the view is a `ref_view` holding a pointer to `local`; the moment the function returns, `local` is destroyed, the memory goes back to the stack/heap, and the view is a wild pointer. Let's catch it with AddressSanitizer: compile with `g++ -std=c++20 -DDANGLING -fsanitize=address ranges_dangling.cpp`.

<OnlineCompilerDemo allow-run
  title="View dangling: correct form by default; add -DDANGLING with ASan to reproduce"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_dangling.cpp"
  description="Default mode shows the correct form where the data source outlives the view; add -DDANGLING and run under ASan to reproduce the use-after-free when a view references a temporary container returned from a function."
/>

The core of what ASan reports:

```text
ERROR: AddressSanitizer: stack-use-after-return on address 0x...
READ of size 8 ... in std::ranges::ref_view<...>::end() const
This frame has 4 object(s):
  [96, 120) 'local' (line 8) <== Memory access at offset 104 is inside this variable
```

The complaint is `ref_view::end()` reading the already-destroyed `local`. The correct form is to make the source outlive the view: store the data in a class member, and have the view reference that member; or pass the source in as an argument. In the example, `SensorBuffer` keeps `data_` as a member, and the view returned by `valid()` is safe for as long as the `SensorBuffer` object lives.

::: warning Don't reshape the source while a view is alive
A view references the source; if the source's contents change, the view reflects it, and that is usually fine. But **changing the source's structure** (insert, erase, grow, anything that invalidates iterators) is a different matter. Views like filter also cache `begin`; if the source invalidates that cached iterator, the view's behavior is undefined. The rule: while a view is alive, read its source but don't reshape it. If you need to reshape, materialize into a container first.
:::

## View vs. container: which when

Views are not a panacea. Whether to use a view or commit to a container falls out of two questions:

- **Use a view**: read-only, single-pass, you want to compose operations with zero copy, and the source lives long enough.
- **Use a container**: you need to mutate, you need to traverse the same result several times, the source is about to die, or you genuinely need ownership.

Because a view stores no data, "traverse the same result repeatedly" is usually better served by materializing once into a container rather than re-running the pipeline each time. The materializer is `std::ranges::to<std::vector<int>>(...)`, but it entered the standard in **C++23**, and this article is C++20; in C++20 you just iterate the view once and push into a `vector`. We'll come back to `ranges::to` in the next article on the pipe operator.

One last note on types: a view's type is a long nested template (`filter_view<transform_view<ref_view<vector<int>>, ...>, ...>`). Don't write it by hand — always use `auto`.

The next article takes the pipe operator `|` apart, shows how it stitches views together, and goes deeper into Ranges in practice.
