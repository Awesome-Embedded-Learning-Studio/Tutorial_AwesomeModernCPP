# Functional Error Handling Examples

This directory contains examples demonstrating functional error handling patterns using std::optional and std::expected.

## Examples

| Example | Description |
|---------|-------------|
| `basic_result.cpp` | Basic Result type and error handling |
| `combinators.cpp` | Monadic combinators (map, and_then) |
| `config_loader.cpp` | Configuration loading with error propagation |
| `retry.cpp` | Retry logic with functional error handling |
| `peripheral_init.cpp` | Peripheral initialization using Result types |

## Building

```bash
mkdir build && cd build
cmake ..
make
```

## Running

```bash
./bin/basic_result
./bin/combinators
./bin/config_loader
./bin/retry
./bin/peripheral_init
```

## Key Concepts

- **Error as value**: Type-safe error handling without exceptions
- **Monadic operations**: map, and_then for error propagation
- **Composability**: Chain operations that fail fast
- **TRY macro**: Rust-like error propagation
- **No heap allocation**: Stack-based error handling for embedded
