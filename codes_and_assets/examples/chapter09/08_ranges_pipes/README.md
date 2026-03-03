# Ranges Pipeline Operations Examples

This directory contains examples demonstrating advanced Ranges pipeline operations using the pipe operator.

## Examples

| Example | Description |
|---------|-------------|
| `pipeline_basics.cpp` | Basic pipeline operations with `|` operator |
| `adc_pipeline.cpp` | ADC data multi-stage processing pipeline |
| `protocol_parser.cpp` | Protocol parsing using ranges pipelines |
| `event_manager.cpp` | Event filtering and processing |
| `ring_buffer_pipeline.cpp` | Custom range (ring buffer) with pipelines |
| `performance_compare.cpp` | Performance: traditional vs ranges |

## Building

```bash
mkdir build && cd build
cmake ..
make
```

## Running

```bash
./bin/pipeline_basics
./bin/adc_pipeline
./bin/protocol_parser
./bin/event_manager
./bin/ring_buffer_pipeline
./bin/performance_compare
```

## Key Concepts

- **Pipe operator `|`**: Unix-style composition of operations
- **Lazy evaluation**: Computation happens only on iteration
- **Zero overhead**: Compiler optimizes to efficient code
- **Composable**: Easy to add/remove processing stages
- **Embedded friendly**: No heap allocation for view pipelines
