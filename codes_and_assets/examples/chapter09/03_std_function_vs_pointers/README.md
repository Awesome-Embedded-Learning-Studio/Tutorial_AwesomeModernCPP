# std::function vs Function Pointers Examples

This directory contains examples comparing function pointers and std::function.

## Examples

| Example | Description |
|---------|-------------|
| `function_pointer.cpp` | Function pointer basics and usage |
| `std_function_demo.cpp` | std::function usage and capabilities |
| `performance_comparison.cpp` | Performance benchmarking comparison |
| `state_machine.cpp` | Table-driven state machine with function pointers |
| `event_loop.cpp` | Event loop using std::function for callbacks |
| `table_driven.cpp` | Table-driven parsing with function pointers |

## Building

```bash
mkdir build && cd build
cmake ..
make
```

## Running

```bash
./bin/function_pointer
./bin/std_function_demo
./bin/performance_comparison
./bin/state_machine
./bin/event_loop
./bin/table_driven
```

## Key Concepts

- **Function pointers**: Zero overhead, ROM-friendly, no context
- **std::function**: Type-erased, can capture context, may heap allocate
- **Trade-offs**: Performance vs flexibility, memory vs convenience
