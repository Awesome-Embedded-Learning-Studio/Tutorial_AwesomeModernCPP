---
chapter: 12
cpp_standard:
- 20
description: 'C++20 Ranges abstracts "a pair of iterators" into a range, so algorithms take a whole container directly; views build on that with lazy evaluation, reference semantics, and O(1) copies, chaining filter/transform/take into an allocation-free pipeline'
difficulty: intermediate
order: 7
platform: host
prerequisites:
- 'Designated Initializers'
reading_time_minutes: 14
related:
- 'C++20 Ranges: Pipelines in Practice'
- 'Designated Initializers'
tags:
- host
- cpp-modern
- intermediate
- Ranges
title: 'C++20 Ranges: Ranges and Views'
translation:
  source: documents/vol4-advanced/vol2-modern-cpp17/07-ranges-basics-and-views.md
  source_hash: 99b8287145b4dbb3100c666744484c732941d2aa906389cb56751652f55af273
  translated_at: '2026-09-26T03:28:19+00:00'
  engine: anthropic
  token_count: 6000
---
# C++20 Ranges: Ranges and Views

Processing one frame of sensor data is usually a whole chain of steps: filter out the anomalies, convert the raw codes into engineering units, take the first few and send them out. The old way of writing it opens two temporary `vector`s, runs one `copy_if` then one `transform`, with a few `back_inserter`s wedged in between — the code reads like a chopped-up checklist. C++20 Ranges offers a smoother path: write the whole chain as one pipeline, allocate nothing along the way, and follow the logic at a glance. The key to all of it is the **view**: it is lazy, holds no data, and copies cheaply — exactly the kind of abstraction embedded work wants most.

In this piece we first pry apart the two most easily confused concepts — Range and View — then pin down each of the view's three core properties with measured evidence, and finally wire it all into a temperature-data pipeline.

## Range: Anything You Can Iterate Over

C++20's definition of a Range is plain: **anything that can provide a pair of iterators (begin/end)**. `std::vector`, `std::array`, raw arrays — all of them are Ranges.

The most tangible change is that algorithms no longer force you to write out a `begin()/end()` pair. Before:

```cpp
std::sort(vec.begin(), vec.end());
```

C++20 just takes the whole container:

```cpp
std::ranges::sort(vec);   // the whole range goes in
```

That is only the surface sugar; the real firepower sits in the set of view factories inside the `<ranges>` header. Let's draw the two concepts apart first:

- **Range**: the umbrella term for anything iterable — `vector`/`array`/raw arrays all count — and it **owns its own data**.
- **View**: a special kind of Range that **holds no data**; it just looks at existing data from a different angle, and it evaluates **lazily**.

The next few sections revolve around the View — it is the foundation of this whole piece.

## Views Are Lazy: Nothing Is Computed at Construction

Views are lazy. The moment you build a `filter` view, no computation has happened yet; the predicate only gets called once iteration starts. Let's prove it by slipping a counter into the predicate:

```cpp
std::vector<int> data = {1, 2, 3, 4, 5};
int pred_calls = 0;

auto v = data | std::views::filter([&](int x) {
    ++pred_calls;
    return x > 2;
});
std::cout << "建视图后(还没遍历)谓词调用次数=" << pred_calls << "\n";

for (int x : v) {
    std::cout << "取到 " << x << ", 此刻谓词已调用 " << pred_calls << " 次\n";
}
```

<OnlineCompilerDemo allow-run
  title="View laziness, reference semantics, and O(1) copy"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_laziness.cpp"
  description="A counting predicate shows the filter view makes zero calls when built and fires per element during iteration; after mutating the source container, re-iterating sees the new values; copying the view does not copy the underlying elements."
/>

Output:

```text
建视图后(还没遍历)谓词调用次数=0
取到 3, 此刻谓词已调用 3 次
取到 4, 此刻谓词已调用 4 次
取到 5, 此刻谓词已调用 5 次
改 data[2]=300 后重新遍历: 300 4 5
拷贝视图后 v2 首元素=300
```

`pred_calls` starts at 0: building the view itself cost zero predicate calls. Once iteration starts, `filter` has to scan past `1`, `2`, and `3` to find the first element `>2`, so by the time the first `3` comes out, the count has already jumped to 3. That is laziness in evidence: the predicate runs only when an element is actually wanted.

One more property, just as important, comes along for free. Above, after changing `data[2]` from `3` to `300`, **re-iterating the view shows the new value**. A view copies no data; it merely references the source container, and when the source changes, the view changes with it.

## Views Hold No Data; Copying Is O(1)

A view merely "looks at" the underlying data; it does not own it. So copying a view copies a few iterators and a predicate — not a single underlying element is duplicated. For embedded work, this means you can pass views around as parameters without worrying about silently copying a big chunk of buffer.

There is a counterintuitive pitfall worth flagging early. Saying a view **references** its source data is literal: what it stores is pointers/iterators to the source, not a snapshot of the values. So **the source container must outlive the view**. Once the source is destroyed, the view becomes a dangling reference. A later section demonstrates this pitfall in detail; for now, just keep the conclusion.

## Common View Factories

`<ranges>` ships a set of "view factories"; let's pick the ones most used in embedded work and see one minimal example of each.

```cpp
std::vector<int> data = {120, 45, 230, 67, 340, 89, 56, 180};

// filter: keep only readings within [50,300]
auto valid = data | std::views::filter([](int v){ return v >= 50 && v <= 300; });

// transform: convert a 12-bit ADC raw value to voltage (mV scale)
auto mv = std::views::transform(data, [](int adc){ return adc * 3300 / 4095; });

// take / drop: slice the head/tail of a data frame
auto seq = std::views::iota(0, 10);
auto first3 = seq | std::views::take(3);             // 0 1 2
auto rest    = std::views::iota(0, 10) | std::views::drop(3);   // 3..9
auto middle  = std::views::iota(0, 10) | std::views::drop(2) | std::views::take(4);  // 2 3 4 5

// iota: generate ADC channel numbers 0..15, using no storage
auto adc_channels = std::views::iota(0, 16);
```

The `iota` line deserves a pause: it generates values by `+1` on demand, allocating nothing and storing nothing — a good fit whenever you just want a run of sequence numbers, such as enumerating a set of channel IDs or producing index subscripts.

For string parsing there is also `split`, which cuts on a delimiter. "Comma- or equals-separated" protocols — NMEA sentences, key-value pairs — can be sliced into sub-ranges in one line:

```cpp
std::string raw = "sensor1=25,sensor2=30,sensor3=28";
for (auto sub : raw | std::views::split(',')) {
    std::string_view sv{sub.begin(), sub.end()};   // sub is not a string; convert to string_view to use it
    // [sensor1=25] [sensor2=30] [sensor3=28]
}
```

<OnlineCompilerDemo allow-run
  title="View factories: filter / transform / take / drop / iota / split"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_factories.cpp"
  description="One minimal example for each of the six most-used view factories, covering filtering, mapping, slicing, sequence generation, and string splitting."
/>

Output:

```text
filter [50,300]: 120 230 67 89 56 180
transform->mV: 96 36 185 53 273 71 45 145
take 3: 0 1 2
drop 3: 3 4 5 6 7 8 9
drop 2 | take 4: 2 3 4 5
iota ADC 通道: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
split(','): [sensor1=25] [sensor2=30] [sensor3=28]
```

## Composing Pipelines: Chaining Views Together

A single view has limited power; chaining them is where Ranges starts to taste like Ranges. The pipe operator `|` links several views into one chain, the whole chain evaluates lazily, and **data flows through one element at a time as you iterate** (the full mechanics of the pipe operator come in the next piece; here we just build the intuition).

```cpp
std::vector<int> readings = {120, 45, 230, 67, 340, 89, 56, 180};
int tf_calls = 0;

auto pipeline = readings
    | std::views::filter([](int v){ return v >= 50 && v <= 300; })
    | std::views::transform([&](int v){ ++tf_calls; return v * 3.3f / 4095; })
    | std::views::take(3);
```

This reads like a sentence: "from `readings`, keep the valid values, convert them to voltage, take the first 3". No intermediate `vector`, no chopped-up logic. Now let's use a counter to see exactly how lazy this laziness gets.

<OnlineCompilerDemo allow-run
  title="Pipeline laziness: take cuts the whole chain short once it has enough"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_pipeline.cpp"
  description="When the whole filter|transform|take pipeline is built, transform has been called zero times; iterating to fetch 3 elements calls transform exactly 3 times — take terminated the upstream early."
/>

Output:

```text
建好管道(没遍历) transform 调用次数=0
前 3 个有效读数的电压:
  0.096703
  0.185348
  0.053993
遍历完 transform 总调用次数=3(take 在取够 3 个后掐断了管道)
```

`transform` was called exactly 3 times — precisely the count requested by `take(3)`. That shows the whole pipeline advances **element by element, on demand**: once `take` has its 3, the upstream `filter` and `transform` stop, and the elements after them are never touched at all. Of the 8 raw readings, `340`, `56`, and `180` were neither tested by filter nor computed by transform. This is the core value of a lazy pipeline: you pay only for the results you actually consume.

## Embedded in Practice: A Temperature Data Pipeline

Let's assemble the pieces into a real embedded scenario. A batch of temperature sensor readouts arrives with anomalies mixed in (999 when a sensor drops off the bus, -200 on an open circuit), and the job is: filter the anomalies out, convert Celsius to Fahrenheit, average the results, and send them on.

```cpp
std::vector<int> readings = {23, 999, 25, -200, 27, 22, 999, 26};

auto processed = readings
    | std::views::filter([](int t){ return t >= -50 && t <= 150; })
    | std::views::transform([](int t){ return t * 9.0 / 5.0 + 32.0; });
```

The whole processing chain involves no intermediate containers like `filtered` or `calibrated`; the data is traversed exactly once, and memory usage stays constant.

<OnlineCompilerDemo allow-run
  title="A temperature-sensor data-processing pipeline"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_sensor_pipeline.cpp"
  description="Simulates a frame of temperature readings containing anomalies: filter drops them, transform converts to Fahrenheit, then averages — with zero temporary containers throughout."
/>

Output:

```text
有效读数(F): 73.4 77.0 80.6 71.6 78.8
平均温度: 76.3 F
```

Notice that `999` and `-200` never appeared in any intermediate buffer at any point — `filter` simply skipped them. Written the old way, those values would at least have been `push_back`ed into the raw `vector` first, only to be discarded during filtering.

## Pitfall: The Lifetime of a View

A view holds no data — flip that advantage over and you get its biggest pitfall: **once the source container is gone, the view dangles**. The most common way to hit it is returning a view from a function while the view references a local variable of that function:

```cpp
// Counter-example: local is destroyed when the function returns; the returned view dangles immediately
auto make_bad_view() {
    std::vector<int> local = {1, 2, 3, 4, 5};
    return local | std::views::filter([](int x){ return x > 2; });
}
```

This is a use-after-free. Internally the view is a `ref_view` holding a pointer to `local`; the moment the function returns, `local` is destroyed, that memory goes back to the stack/heap, and the view becomes a wild pointer. Let's catch it with AddressSanitizer, compiled with `g++ -std=c++20 -DDANGLING -fsanitize=address ranges_dangling.cpp`.

<OnlineCompilerDemo allow-run
  title="Dangling view: the default demo shows the correct pattern; -DDANGLING reproduces it under ASan"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_dangling.cpp"
  description="The default mode shows the correct pattern where the data source and the view share a lifetime; adding -DDANGLING reproduces, under ASan, the use-after-free of a view referencing a temporary container after the function returns."
/>

The key lines from ASan:

```text
ERROR: AddressSanitizer: stack-use-after-return on address 0x...
READ of size 8 ... in std::ranges::ref_view<...>::end() const
This frame has 4 object(s):
  [96, 120) 'local' (line 8) <== Memory access at offset 104 is inside this variable
```

The report says `ref_view::end()` read the already-destroyed `local`. The correct pattern is to make the data source outlive the view: store the data in a class member and have the view reference only that member, or pass the data source in as a parameter. In the example, the `SensorBuffer` class stores `data_` as a member, and the view returned by `valid()` stays safe for as long as the `SensorBuffer` object lives.

::: warning Don't touch a view's source while the view is alive
A view references its source, so when the source's contents change, the view changes too — usually fine. But beware: **mutating the source's structure** (insertions, erasures, growth that invalidates iterators) is a different matter. Views like filter also cache `begin`; once the source invalidates that cached iterator, the view's behavior is undefined. The rule: while a view is alive, treat its source as read-only; if you must modify it, materialize into a container first.
:::

## Views vs Containers: When to Use Which

Views are not a cure-all. Whether to use a view or settle into a container can be drawn along these two lines:

- **Use a view**: the data is read-only, you iterate it once, you want to compose operations with zero copies, and the data source lives long enough.
- **Use a container**: you need to modify the data, traverse the same result multiple times, the data source is about to be destroyed, or you genuinely need to own the data.

A view stores no data, so for a "traverse the same result multiple times" need, instead of re-running the pipeline every time, run it once and materialize it into a container. The materialization tool is `std::ranges::to<std::vector<int>>(...)`, but that only entered the standard in **C++23**, while this piece is about C++20; in C++20 you can just iterate the view once and push the results into a `vector`. We will come back to `ranges::to` in the next piece when we discuss the pipe operator.

One last note on types: a view's type is a long chain of nested templates (`filter_view<transform_view<ref_view<vector<int>>, ...>, ...>`); don't write it by hand — use `auto`, always.

In the next piece we crack open the mechanics of the pipe operator `|` — how it joins views pairwise, plus more hands-on Ranges techniques.
