# Zero Overhead Callback Implementation Examples

This directory contains examples demonstrating zero-overhead callback mechanisms for embedded systems.

## Examples

| Example | Description |
|---------|-------------|
| `zero_callback.cpp` | Manual type-erasure callback with function pointer vtable |
| `small_callback.cpp` | Fixed-size callback using virtual functions |
| `fast_callback.cpp` | Template-based callbacks for compile-time polymorphism |
| `event_system.cpp` | Complete zero-overhead event system |
| `multicast.cpp` | Multicast event system with multiple listeners |

## Building

```bash
mkdir build && cd build
cmake ..
make
```

## Running

```bash
./bin/zero_callback
./bin/small_callback
./bin/fast_callback
./bin/event_system
./bin/multicast
```

## Key Concepts

- **Manual type erasure**: Avoid std::function overhead
- **Fixed-size storage**: No heap allocation
- **VTable pattern**: Function pointers instead of virtual functions
- **Template callbacks**: Zero overhead when type is known at compile time
- **Suitable for**: Interrupt handlers, event systems, callbacks in memory-constrained environments
