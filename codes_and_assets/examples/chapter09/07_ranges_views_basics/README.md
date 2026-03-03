# C++20 Ranges and Views Examples

This directory contains examples demonstrating C++20 Ranges library and Views.

## Examples

| Example | Description |
|---------|-------------|
| `view_basics.cpp` | Basic view concepts and lazy evaluation |
| `filter_views.cpp` | Using std::views::filter for data filtering |
| `transform_views.cpp` | Using std::views::transform for data transformation |
| `take_drop_views.cpp` | Using std::views::take and std::views::drop |
| `split_view.cpp` | Using std::views::split for parsing |
| `sensor_pipeline.cpp` | Complete sensor data processing pipeline |

## Building

```bash
mkdir build && cd build
cmake ..
make
```

## Running

```bash
./bin/view_basics
./bin/filter_views
./bin/transform_views
./bin/take_drop_views
./bin/split_view
./bin/sensor_pipeline
```

## Key Concepts

- **Views**: Lazy, non-owning ranges with O(1) copy
- **Lazy evaluation**: Computation happens on iteration
- **Composability**: Views can be chained with `|` operator
- **Zero copy**: No temporary allocations when composing views
- **Embedded friendly**: Predictable memory usage, no heap allocation
