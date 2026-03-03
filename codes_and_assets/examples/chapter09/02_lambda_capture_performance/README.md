# Lambda Capture and Performance Examples

This directory contains examples demonstrating lambda capture mechanisms and their performance implications.

## Examples

| Example | Description |
|---------|-------------|
| `value_capture.cpp` | Value capture semantics and behavior |
| `reference_capture.cpp` | Reference capture and lifetime considerations |
| `full_capture.cpp` | Full capture with `[=]` and `[&]` |
| `init_capture.cpp` | Init capture with C++14 generalized capture |
| `mutable_capture.cpp` | Mutable lambdas with value capture |
| `performance_demo.cpp` | Performance comparison and inline demonstration |
| `capture_size.cpp` | Lambda object size analysis |

## Building

```bash
mkdir build && cd build
cmake ..
make
```

## Running

```bash
./bin/value_capture
./bin/reference_capture
./bin/full_capture
./bin/init_capture
./bin/mutable_capture
./bin/performance_demo
./bin/capture_size
```

## Key Concepts

- **Value capture**: Copies the variable, safe but has copy overhead
- **Reference capture**: References the original, zero copy but lifetime sensitive
- **Init capture**: C++14 feature for move capture and computed captures
- **mutable**: Allows modifying captured-by-value variables
- **Performance**: Captures are stored as member variables, calls can be fully inlined
