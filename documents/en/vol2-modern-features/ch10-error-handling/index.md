# Modern Error Handling

Error handling is a core challenge every C++ programmer must face—C-style error codes can be silently ignored, exceptions are restricted in embedded environments, and raw pointers lack clear semantics for representing "no value." Modern C++ provides type-safe alternatives like `optional`, `variant`, and `expected`. In this chapter, we review the evolution of error handling, master the use cases for each approach, and finally provide a scenario-based selection guide.

## Chapter Contents

- [Error Handling Evolution: From Error Codes to Type Safety](01-error-handling-evolution)
- [Using optional for Error Handling](02-optional-error)
- [std::expected<T, E>: Type-Safe Error Propagation](03-expected-error)
- [Error Handling Patterns Summary: Selection Guide and Best Practices](04-error-patterns)
