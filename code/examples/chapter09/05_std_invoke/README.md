# std::invoke Examples

This directory contains examples demonstrating std::invoke and uniform callable object invocation.

## Examples

| Example | Description |
|---------|-------------|
| `invoke_basics.cpp` | Basic std::invoke usage with different callable types |
| `universal_caller.cpp` | Universal caller using std::invoke |
| `wrapper.cpp` | Wrapper patterns using std::invoke |
| `command_parser.cpp` | Command parser using std::invoke |
| `invoke_result.cpp` | Compile-time return type deduction with invoke_result_t |

## Building

```bash
mkdir build && cd build
cmake ..
make
```

## Running

```bash
./bin/invoke_basics
./bin/universal_caller
./bin/wrapper
./bin/command_parser
./bin/invoke_result
```

## Key Concepts

- **std::invoke**: Uniform invocation syntax for all callable types
- **Member functions**: Automatically handles object parameters
- **Perfect forwarding**: Preserves value categories
- **invoke_result_t**: Compile-time return type deduction
- **Type safety**: Better than C-style function pointers
