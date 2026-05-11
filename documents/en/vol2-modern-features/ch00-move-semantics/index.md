# Move Semantics and Rvalue References

Move semantics is one of the most important features in C++11—it makes "transferring resource ownership" a first-class citizen, fundamentally changing how we handle copy overhead. In this chapter, we start from the value category system to understand what lvalues and rvalues are and why we need rvalue references. We then dive into the implementation principles of move constructors and move assignments, see how much the compiler's RVO optimization saves you, and finally tie everything together with perfect forwarding. Move semantics is not just a performance optimization; it is the cornerstone of understanding modern C++ resource management.

## Chapter Contents

- [Rvalue References: From Copy to Move](01-rvalue-reference)
- [Move Construction and Move Assignment](02-move-semantics)
- [RVO and NRVO: Compiler Return Value Optimization](03-rvo-nrvo)
- [Perfect Forwarding: Preserving Value Categories in Argument Passing](04-perfect-forwarding)
- [Move Semantics in Practice: From STL to Custom Types](05-move-in-practice)
